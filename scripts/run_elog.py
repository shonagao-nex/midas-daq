#!/usr/bin/env python3
"""Post one completed daq-dev JSON Runlog to the MIDAS Built-in ELOG."""

import argparse
import ctypes
import datetime
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


EXPTAB = "/home/nagao/midas/midas/exptab"
LIBMIDAS = "/home/nagao/midas/midas_src/lib/libmidas-c-compat.so"
MELOG = "/home/nagao/midas/midas_src/bin/melog"
LOCK = "/tmp/midas-daq-dev-run-elog.lock"
ROOT = "/Experiment/Run Elog"
TIMEOUT_SECONDS = 10
RUNLOG_RETRY_INTERVAL_SECONDS = 0.2
RUNLOG_MAX_WAIT_SECONDS = 5.0


class Odb:
    def __init__(self):
        if os.environ.get("MIDAS_EXPTAB") != EXPTAB:
            raise RuntimeError("Refusing to connect outside daq-dev EXPTAB")
        self.lib = ctypes.CDLL(LIBMIDAS)
        self.lib.c_cm_connect_experiment.argtypes = [ctypes.c_char_p] * 3 + [ctypes.c_void_p]
        self.lib.c_cm_connect_experiment.restype = ctypes.c_int
        self.lib.c_cm_get_experiment_database.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_void_p]
        self.lib.c_cm_get_experiment_database.restype = ctypes.c_int
        self.lib.c_db_get_value.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_char_p,
                                             ctypes.c_void_p, ctypes.POINTER(ctypes.c_int),
                                             ctypes.c_uint, ctypes.c_int]
        self.lib.c_db_get_value.restype = ctypes.c_int
        self.lib.c_db_set_value.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_char_p,
                                             ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                             ctypes.c_uint]
        self.lib.c_db_set_value.restype = ctypes.c_int
        self.lib.c_cm_disconnect_experiment.restype = ctypes.c_int
        self.lib.c_cm_msg.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int,
                                      ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
        self.lib.c_cm_msg.restype = ctypes.c_int
        if self.lib.c_cm_connect_experiment(b"", b"daq-dev", b"run_elog", None) != 1:
            raise RuntimeError("Cannot connect to daq-dev ODB")
        self.handle = ctypes.c_int()
        if self.lib.c_cm_get_experiment_database(ctypes.byref(self.handle), None) != 1:
            self.close()
            raise RuntimeError("Cannot get daq-dev ODB handle")
        if self.string("/Experiment/Name") != "daq-dev":
            self.close()
            raise RuntimeError("Connected experiment is not daq-dev")

    def close(self):
        self.lib.c_cm_disconnect_experiment()

    def _get(self, path, value, tid):
        size = ctypes.c_int(ctypes.sizeof(value))
        status = self.lib.c_db_get_value(self.handle, 0, path.encode(),
                                          ctypes.byref(value), ctypes.byref(size), tid, 0)
        if status != 1:
            raise RuntimeError(f"Cannot read ODB {path}: status {status}")
        return value

    def string(self, path, optional=False):
        buffer = ctypes.create_string_buffer(8192)
        try:
            return self._get(path, buffer, 12).value.decode("utf-8")
        except RuntimeError:
            if optional:
                return ""
            raise

    def integer(self, path):
        return self._get(path, ctypes.c_int(), 7).value

    def boolean(self, path):
        return bool(self._get(path, ctypes.c_int(), 8).value)

    def _set(self, path, value, tid):
        status = self.lib.c_db_set_value(self.handle, 0, path.encode(),
                                          ctypes.byref(value), ctypes.sizeof(value), 1, tid)
        if status != 1:
            raise RuntimeError(f"Cannot write ODB {path}: status {status}")

    def set_string(self, path, value):
        encoded = value.encode("utf-8")
        if len(encoded) >= 1024:
            encoded = encoded[:1023].decode("utf-8", "ignore").encode("utf-8")
        self._set(path, ctypes.create_string_buffer(encoded, 1024), 12)

    def set_integer(self, path, value):
        self._set(path, ctypes.c_int(value), 7)

    def error_message(self, run, reason):
        message = f"Run {run} ELOG post failed: {reason}".encode("utf-8")[:850]
        self.lib.c_cm_msg(1, b"run_elog.py", 0, b"midas", b"run_elog",
                           b"%s", ctypes.c_char_p(message))


def parse_integer(value):
    if isinstance(value, int):
        return value
    text = str(value)
    return int(text, 16 if text.lower().startswith("0x") else 10)


def event_text(value):
    number = parse_integer(value)
    return "N/A (not participating)" if number == -1 else str(number)


def format_entry(run, record):
    bor, eor = record["BOR"], record["EOR"]
    if parse_integer(bor["Run number"]) != run:
        raise ValueError("Runlog BOR run number does not match requested run")
    run_type = bor["Type"]
    if run_type not in ("Data", "Clock", "Cosmic", "Test"):
        raise ValueError(f"Invalid Run Type: {run_type}")
    status = eor["DAQ Status"]
    if status not in ("OK", "WARNING", "ERROR"):
        raise ValueError(f"Invalid DAQ Status: {status}")
    comment = bor["Comment"] if str(bor["Comment"]).strip() else "(empty)"
    subject = f"Run #{run} - {run_type} - {status}"
    body = (f"Run #{run} Summary\n\n"
            f"Type       : {run_type}\n"
            f"Comment    : {comment}\n\n"
            f"Start      : {bor['Start time']}\n"
            f"Stop       : {eor['Stop time']}\n"
            f"Duration   : {parse_integer(eor['Duration'])} s\n\n"
            f"Events\n"
            f"  VME      : {event_text(eor['VME events'])}\n"
            f"  EASIROC  : {event_text(eor['EASIROC events'])}\n\n"
            f"DAQ Status : {status}\n"
            f"DAQ Summary: {eor['DAQ Summary']}\n"
            f"Event Slip : {parse_integer(eor['EventSlipCount'])}\n")
    if len(body.encode("utf-8")) >= 9999:
        raise ValueError("ELOG body exceeds melog 9999-byte limit")
    return subject, body


def elog_file(odb):
    directory = odb.string("/Logger/Elog dir", optional=True) or odb.string("/Logger/Data dir")
    path = Path(directory).resolve()
    if not str(path).startswith("/home/nagao/"):
        raise RuntimeError("Refusing ELOG storage outside /home/nagao")
    return path / datetime.datetime.now().strftime("%y%m%d.log")


def entry_exists(path, run, subject):
    if not path.exists():
        return False
    content = path.read_text(encoding="utf-8", errors="replace")
    return any(f"Run: {run}\n" in entry and f"Subject: {subject}\n" in entry
               for entry in content.split("$Start$")[1:])


def runlog_path(odb, run):
    directory = odb.string("/Logger/Message dir", optional=True) or odb.string("/Logger/Data dir")
    path = (Path(directory) / odb.string("/Logger/Runlog/JSON/Subdir") /
            f"runlog_{run:06d}.json").resolve()
    if not str(path).startswith("/home/nagao/"):
        raise RuntimeError("Refusing Runlog outside /home/nagao")
    return path


def read_complete_runlog(odb, run, max_wait_seconds=RUNLOG_MAX_WAIT_SECONDS,
                         retry_interval_seconds=RUNLOG_RETRY_INTERVAL_SECONDS):
    path = runlog_path(odb, run)
    deadline = time.monotonic() + max_wait_seconds
    while True:
        try:
            record = json.loads(path.read_text(encoding="utf-8"))
        except (FileNotFoundError, UnicodeDecodeError, json.JSONDecodeError):
            record = None

        if record is not None:
            if not isinstance(record, dict) or not isinstance(record.get("BOR"), dict):
                raise ValueError("Runlog BOR is missing or invalid")
            bor = record["BOR"]
            if parse_integer(bor["Run number"]) != run:
                raise ValueError("Runlog BOR run number does not match requested run")
            if "EOR" in record:
                if not isinstance(record["EOR"], dict):
                    raise ValueError("Runlog EOR is invalid")
                return record

        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(
                f"Logger JSON did not become complete within {max_wait_seconds:g} seconds")
        time.sleep(min(retry_interval_seconds, remaining))


def post_melog(run, subject, body, author, category, system):
    attrs = (f"Author={author}", f"Type={category}", f"System={system}",
             f"Subject={subject}", f"Run={run}")
    if any(len(attr.encode("utf-8")) >= 100 for attr in attrs):
        raise ValueError("ELOG attribute exceeds melog 99-byte limit")
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", delete=True) as message:
        message.write(body)
        message.flush()
        command = [MELOG, "-h", "127.0.0.1", "-p", "8181"]
        for attr in attrs:
            command.extend(("-a", attr))
        command.extend(("-m", message.name))
        result = subprocess.run(command, capture_output=True, text=True,
                                timeout=TIMEOUT_SECONDS, check=False)
    # This MIDAS melog returns exit status 1 for both success and failure.
    # Check its success message, then verify the saved Built-in ELOG entry below.
    if "Message successfully transmitted" not in result.stdout:
        detail = (result.stdout + " " + result.stderr).strip()[:400]
        raise RuntimeError(detail or f"melog returned {result.returncode}")


def process_run(odb, run, retry=False, *,
                runlog_wait_seconds=RUNLOG_MAX_WAIT_SECONDS,
                runlog_retry_interval_seconds=RUNLOG_RETRY_INTERVAL_SECONDS):
    eor_complete_run = odb.integer("/DAQ/Status/Runlog/EORCompleteRunNumber")
    if eor_complete_run != run:
        raise RuntimeError(
            f"EOR is not complete for run {run} (EORCompleteRunNumber={eor_complete_run})")
    if odb.integer(f"{ROOT}/Last Run") == run:
        return "ALREADY_POSTED"
    last_attempt = odb.integer(f"{ROOT}/Last Attempt Run")
    if last_attempt == run and not retry:
        return "ALREADY_ATTEMPTED"
    if run > last_attempt:
        odb.set_integer(f"{ROOT}/Last Attempt Run", run)
    if not odb.boolean(f"{ROOT}/Enabled"):
        odb.set_string(f"{ROOT}/Last Status", "DISABLED")
        odb.set_string(f"{ROOT}/Last Error", "")
        return "DISABLED"

    odb.set_string(f"{ROOT}/Last Status", "PENDING")
    odb.set_string(f"{ROOT}/Last Error", "")
    try:
        record = read_complete_runlog(odb, run, runlog_wait_seconds,
                                      runlog_retry_interval_seconds)
        subject, body = format_entry(run, record)
        target = elog_file(odb)
        if not entry_exists(target, run, subject):
            post_melog(run, subject, body, odb.string(f"{ROOT}/Author"),
                       odb.string(f"{ROOT}/Type"), odb.string(f"{ROOT}/System"))
        if not entry_exists(target, run, subject):
            raise RuntimeError("melog reported success but Built-in ELOG entry was not found")
        odb.set_integer(f"{ROOT}/Last Run", run)
        odb.set_string(f"{ROOT}/Last Status", "OK")
        odb.set_string(f"{ROOT}/Last Error", "")
        return "OK"
    except (OSError, ValueError, RuntimeError, KeyError,
            subprocess.TimeoutExpired) as error:
        reason = "melog timed out after 10 s" if isinstance(error, subprocess.TimeoutExpired) else str(error)
        odb.set_string(f"{ROOT}/Last Status", "ERROR")
        odb.set_string(f"{ROOT}/Last Error", reason)
        odb.error_message(run, reason)
        return "ERROR"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=int, required=True)
    parser.add_argument("--retry", action="store_true", help="explicit manual retry after an ambiguous failure")
    args = parser.parse_args()
    if args.run <= 0:
        parser.error("run number must be positive")
    with open(LOCK, "a+", encoding="ascii") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        odb = Odb()
        try:
            result = process_run(odb, args.run, args.retry)
        finally:
            odb.close()
        print(result)
        return 1 if result == "ERROR" else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"run_elog: {error}", file=sys.stderr)
        sys.exit(1)
