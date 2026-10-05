#!/usr/bin/env python3
"""Converge only the explicitly owned live ODB schema on the DAQ host."""

import json
import os
from pathlib import Path
import sys
import urllib.request


ROOT = Path("/home/daq/midas/midas/online")
RUNTIME = Path("/home/daq/midas/midas")
URL = "http://127.0.0.1:8081/?mjsonrpc"
RUN_PARAMETERS = "/Experiment/Run Parameters"
JSON_ROOT = "/Logger/Runlog/JSON"
STATUS = "/DAQ/Status/Runlog"
ELOG = "/Experiment/Run Elog"
SUCCESS = 1
MISSING = 312
ANALYZER_PROGRAMS = {
    "ana": f"{ROOT}/analyzer/bin/midas_analyzer --no-profiler",
    "ana_hist_odb_init": f"{ROOT}/analyzer/bin/midas_analyzer --init-hist-odb",
}

# TID values in the installed MIDAS include/midas.h.
UINT32, BOOL, INT32, DOUBLE, STRING, KEY, LINK, INT64, UINT64 = (
    6, 8, 7, 10, 12, 15, 16, 17, 18)


class Rpc:
    def __init__(self, url=URL):
        self.url = url

    def call(self, method, params):
        request = urllib.request.Request(
            self.url,
            json.dumps({"jsonrpc": "2.0", "method": method,
                        "params": params, "id": 1}).encode("utf-8"),
            {"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(request, timeout=8) as response:
            result = json.load(response)
        if "error" in result:
            raise RuntimeError(f"{method}: {result['error']}")
        if "result" not in result:
            raise RuntimeError(f"{method}: missing result")
        return result["result"]

    def value(self, path, missing_ok=False):
        result = self.call("db_get_values", {"paths": [path]})
        status = result["status"][0]
        if status != SUCCESS:
            if missing_ok and status == MISSING:
                return None
            raise RuntimeError(f"Missing ODB value: {path} (status {status})")
        return result["data"][0]

    def key(self, path):
        result = self.call("db_key", {"paths": [path]})
        status = result["status"][0]
        if status == SUCCESS:
            return result["keys"][0]
        if status == MISSING:
            return None
        raise RuntimeError(f"Cannot inspect ODB key: {path} (status {status})")

    def copy(self, path):
        result = self.call("db_copy", {"paths": [path]})
        if result["status"][0] != SUCCESS:
            raise RuntimeError(f"Cannot inspect ODB subtree: {path}")
        return result["data"][0]

    def checked(self, method, params):
        result = self.call(method, params)
        if result.get("status") != [SUCCESS]:
            raise RuntimeError(f"{method} failed: {result}")


def require_live(rpc):
    if os.geteuid() != __import__("pwd").getpwnam("daq").pw_uid:
        raise RuntimeError("Run as daq on the DAQ host")
    if os.uname().nodename.split(".")[0] != "nexdaq1":
        raise RuntimeError("Unexpected DAQ host")
    if Path.cwd().resolve() != ROOT:
        raise RuntimeError(f"Working directory must be {ROOT}")
    if rpc.value("/Experiment/Name") != "daq":
        raise RuntimeError("Port 8081 is not experiment daq")
    require_stopped(rpc)


def require_stopped(rpc):
    if rpc.value("/Runinfo/State") != 1 or rpc.value(
            "/Runinfo/Transition in progress") != 0:
        raise RuntimeError("Live run must be STOPPED with no transition")


def specs():
    """(path, TID, minimum string bytes, initial value for absent keys)."""
    values = [
        (RUN_PARAMETERS, KEY, 0, None),
        (f"{RUN_PARAMETERS}/Type", STRING, 32, "Data"),
        (f"{RUN_PARAMETERS}/Comment", STRING, 4097, ""),
        (f"{RUN_PARAMETERS}/ExperimentLabel", STRING, 1024, ""),
        (STATUS, KEY, 0, None),
        (f"{STATUS}/DurationSec", UINT64, 0, 0),
        (f"{STATUS}/VMEEvents", INT64, 0, 0),
        (f"{STATUS}/EASIROCEvents", INT64, 0, 0),
        (f"{STATUS}/HULEvents", INT64, 0, -1),
        (f"{STATUS}/EventSlipCount", INT64, 0, 0),
        (f"{STATUS}/DAQStatus", STRING, 512, ""),
        (f"{STATUS}/DAQSummary", STRING, 512, ""),
        (f"{STATUS}/Scaler64ch", KEY, 0, None),
        (f"{STATUS}/EORCompleteRunNumber", INT32, 0, 0),
        (f"{STATUS}/CountRunNumber", INT32, 0, 0),
        (f"{STATUS}/ObservedVMEEvents", DOUBLE, 0, 0),
        (f"{STATUS}/ObservedEASIROCEvents", DOUBLE, 0, 0),
        (f"{STATUS}/StatusRunNumber", INT32, 0, 0),
        (ELOG, KEY, 0, None),
        (f"{ELOG}/Enabled", BOOL, 0, False),
        (f"{ELOG}/Web Port", INT32, 0, 8081),
        (f"{ELOG}/Author", STRING, 32, "DAQ"),
        (f"{ELOG}/Type", STRING, 32, "Routine"),
        (f"{ELOG}/System", STRING, 32, "DAQ"),
        (f"{ELOG}/Last Run", INT32, 0, 0),
        (f"{ELOG}/Last Attempt Run", INT32, 0, 0),
        (f"{ELOG}/Last Status", STRING, 64, "DISABLED"),
        (f"{ELOG}/Last Error", STRING, 1024, ""),
        (JSON_ROOT, KEY, 0, None),
        (f"{JSON_ROOT}/Write data", BOOL, 0, True),
        (f"{JSON_ROOT}/Subdir", STRING, 256, "../runlogs"),
        (f"{JSON_ROOT}/Links BOR", KEY, 0, None),
        (f"{JSON_ROOT}/Links EOR", KEY, 0, None),
        ("/Programs/fevme", KEY, 0, None),
        ("/Programs/feeasiroc", KEY, 0, None),
        ("/Custom", KEY, 0, None),
        ("/Custom/Path", STRING, 256, str(ROOT / "daq_monitor/web")),
        ("/Custom/DAQ", STRING, 256, "daq.html"),
        ("/Custom/Run Summary", STRING, 32, "run-summary.html"),
    ]
    for name in ("fevme", "feeasiroc"):
        prefix = f"/Programs/{name}"
        values.extend((
            (f"{prefix}/Required", BOOL, 0, True),
            (f"{prefix}/Watchdog timeout", INT32, 0, 10000),
            (f"{prefix}/Check interval", UINT32, 0, 180000),
            (f"{prefix}/Start command", STRING, 256,
             str(ROOT / f"scripts/start_{name}.sh")),
            (f"{prefix}/Auto start", BOOL, 0, False),
            (f"{prefix}/Auto stop", BOOL, 0, False),
            (f"{prefix}/Auto restart", BOOL, 0, False),
            (f"{prefix}/Alarm class", STRING, 32, ""),
        ))
    for name, command in ANALYZER_PROGRAMS.items():
        prefix = f"/Programs/{name}"
        values.extend((
            (prefix, KEY, 0, None),
            (f"{prefix}/Required", BOOL, 0, False),
            (f"{prefix}/Start command", STRING, 256, command),
            (f"{prefix}/Auto start", BOOL, 0, False),
            (f"{prefix}/Auto stop", BOOL, 0, False),
            (f"{prefix}/Auto restart", BOOL, 0, False),
        ))
    return values


BOR = (
    ("Run number", "/Runinfo/Run number"),
    ("Start time", "/Runinfo/Start time"),
    ("Type", f"{RUN_PARAMETERS}/Type"),
    ("experiment_label", f"{RUN_PARAMETERS}/ExperimentLabel"),
)
EOR = (
    ("Stop time", "/Runinfo/Stop time"),
    ("Comment", f"{RUN_PARAMETERS}/Comment"),
    ("Duration", f"{STATUS}/DurationSec"),
    ("VME events", f"{STATUS}/VMEEvents"),
    ("EASIROC events", f"{STATUS}/EASIROCEvents"),
    ("HUL events", f"{STATUS}/HULEvents"),
    ("EventSlipCount", f"{STATUS}/EventSlipCount"),
    ("DAQ Status", f"{STATUS}/DAQStatus"),
    ("DAQ Summary", f"{STATUS}/DAQSummary"),
    ("Scaler 64ch", f"{STATUS}/Scaler64ch"),
)


def inspect_key(rpc, path, tid, minimum):
    key = rpc.key(path)
    if key is None:
        return False
    if key["type"] != tid or key["num_values"] != 1:
        raise RuntimeError(f"Unexpected ODB type/array at {path}: {key}")
    if tid == STRING and key["item_size"] < minimum:
        raise RuntimeError(f"ODB string too short at {path}: {key['item_size']} < {minimum}")
    return True


def ensure_key(rpc, path, tid, minimum, initial):
    key = rpc.key(path)
    if key is None:
        require_stopped(rpc)
        args = {"path": path, "type": tid}
        if tid == STRING:
            args["string_length"] = minimum
        rpc.checked("db_create", [args])
        if initial is not None:
            rpc.checked("db_paste", {"paths": [path], "values": [initial]})
    elif key["type"] != tid or key["num_values"] != 1:
        raise RuntimeError(f"Unexpected ODB type/array at {path}: {key}")
    elif tid == STRING and key["item_size"] < minimum:
        require_stopped(rpc)
        rpc.checked("db_resize_string", {"paths": [path], "new_lengths": [1],
                                         "new_string_lengths": [minimum]})
    if not inspect_key(rpc, path, tid, minimum):
        raise RuntimeError(f"Cannot verify ODB key: {path}")


def ensure_signed_slips(rpc):
    """Convert only the Runlog slip key from legacy UINT64 to INT64."""
    path = f"{STATUS}/EventSlipCount"
    key = rpc.key(path)
    if key is None or key["type"] == INT64:
        ensure_key(rpc, path, INT64, 0, 0)
        return
    if key["type"] != UINT64 or key["num_values"] != 1:
        raise RuntimeError(f"Unexpected ODB type/array at {path}: {key}")
    raw = rpc.value(path)
    if isinstance(raw, str):
        value = int(raw, 16 if raw.lower().startswith("0x") else 10)
    elif isinstance(raw, int):
        value = raw
    else:
        raise RuntimeError(f"Unexpected ODB value at {path}: {raw!r}")
    if value < 0 or value > 0x7FFFFFFFFFFFFFFF:
        raise RuntimeError(f"Runlog slip count cannot be represented as INT64: {value}")
    require_stopped(rpc)
    rpc.checked("db_delete", {"paths": [path]})
    rpc.checked("db_create", [{"path": path, "type": INT64}])
    rpc.checked("db_paste", {"paths": [path], "values": [value]})
    if not inspect_key(rpc, path, INT64, 0) or not same_value(rpc.value(path), value):
        raise RuntimeError(f"Cannot verify converted ODB key: {path}")


def link_map(rpc, phase):
    raw = rpc.copy(f"{JSON_ROOT}/Links {phase}")
    links = []
    for name, value in raw.items():
        if name.endswith("/key"):
            continue
        metadata = raw.get(f"{name}/key", {})
        if metadata.get("type") != LINK or metadata.get("link") != value:
            raise RuntimeError(f"Invalid ODB link at Links {phase}/{name}")
        links.append((name, value))
    return links


def inspect_links(rpc, phase, expected):
    actual = link_map(rpc, phase)
    if actual != list(expected):
        raise RuntimeError(f"Unexpected {phase} links: {actual!r}")


def ensure_links(rpc, phase, expected):
    directory = f"{JSON_ROOT}/Links {phase}"
    actual = link_map(rpc, phase)
    expected_names = {name.casefold() for name, _ in expected}
    unexpected = [name for name, _ in actual if name.casefold() not in expected_names]
    if unexpected:
        raise RuntimeError(f"Unexpected {phase} link names: {unexpected}")
    for name, target in expected:
        found = next(((n, t) for n, t in actual if n.casefold() == name.casefold()), None)
        if found is None:
            require_stopped(rpc)
            rpc.checked("db_link", {"new_links": [f"{directory}/{name}"],
                                    "target_paths": [target]})
        elif found != (name, target):
            require_stopped(rpc)
            rpc.checked("db_delete", {"paths": [f"{directory}/{found[0]}"]})
            rpc.checked("db_link", {"new_links": [f"{directory}/{name}"],
                                    "target_paths": [target]})
    for index, (name, _) in enumerate(expected):
        actual = link_map(rpc, phase)
        if actual[index][0] != name:
            require_stopped(rpc)
            rpc.checked("db_reorder", {"paths": [f"{directory}/{name}"],
                                       "indices": [index]})
    inspect_links(rpc, phase, expected)


MANAGED_VALUES = {
    f"{JSON_ROOT}/Write data": True,
    f"{JSON_ROOT}/Subdir": "../runlogs",
    "/Custom/DAQ": "daq.html",
    "/Custom/Run Summary": "run-summary.html",
}
for _name in ("fevme", "feeasiroc"):
    _prefix = f"/Programs/{_name}"
    MANAGED_VALUES.update({
        f"{_prefix}/Required": True,
        f"{_prefix}/Watchdog timeout": 10000,
        f"{_prefix}/Check interval": 180000,
        f"{_prefix}/Start command": str(ROOT / f"scripts/start_{_name}.sh"),
        f"{_prefix}/Auto start": False,
        f"{_prefix}/Auto stop": False,
        f"{_prefix}/Auto restart": False,
    })
for _name, _command in ANALYZER_PROGRAMS.items():
    _prefix = f"/Programs/{_name}"
    MANAGED_VALUES.update({
        f"{_prefix}/Required": False,
        f"{_prefix}/Start command": _command,
        f"{_prefix}/Auto start": False,
        f"{_prefix}/Auto stop": False,
        f"{_prefix}/Auto restart": False,
    })


def same_value(actual, expected):
    if isinstance(actual, str) and isinstance(expected, int):
        return int(actual, 16 if actual.lower().startswith("0x") else 10) == expected
    return actual == expected


def inspect_live_paths(rpc):
    expected = {
        "/Custom/Path": str(ROOT / "daq_monitor/web"),
        "/Logger/Message dir": str(RUNTIME / "log"),
        "/Logger/Data dir": str(RUNTIME / "data"),
        "/Logger/Elog dir": str(RUNTIME / "elog"),
        f"{ELOG}/Web Port": 8081,
    }
    for path, value in expected.items():
        if rpc.value(path) != value:
            raise RuntimeError(f"Unexpected live-specific ODB value: {path}")


def inspect_schema(rpc):
    for path, tid, minimum, _ in specs():
        if not inspect_key(rpc, path, tid, minimum):
            raise RuntimeError(f"Missing schema key: {path}")
    inspect_links(rpc, "BOR", BOR)
    inspect_links(rpc, "EOR", EOR)
    for path, expected in MANAGED_VALUES.items():
        if not same_value(rpc.value(path), expected):
            raise RuntimeError(f"Managed ODB value differs: {path}")
    inspect_live_paths(rpc)


def configure(rpc):
    require_live(rpc)
    # These are site/runtime prerequisites, not values that deployment rewrites.
    for path, expected in (("/Logger/Message dir", str(RUNTIME / "log")),
                           ("/Logger/Data dir", str(RUNTIME / "data")),
                           ("/Logger/Elog dir", str(RUNTIME / "elog"))):
        if rpc.value(path) != expected:
            raise RuntimeError(f"Unexpected live runtime path: {path}")
    if rpc.key(f"{ELOG}/Enabled") is not None and rpc.value(f"{ELOG}/Enabled") is True:
        for name in ("Last Run", "Last Attempt Run"):
            if rpc.key(f"{ELOG}/{name}") is None:
                raise RuntimeError(f"Enabled Run Elog has missing history key: {name}")
    for path, tid, minimum, initial in specs():
        if path == "/Custom/Path" and rpc.key(path) is not None and \
                rpc.value(path) != str(ROOT / "daq_monitor/web"):
            raise RuntimeError("Existing /Custom/Path points elsewhere")
        if path == f"{ELOG}/Web Port" and rpc.key(path) is not None and \
                rpc.value(path) != 8081:
            raise RuntimeError("Existing Run Elog Web Port is not 8081")
        if path == f"{STATUS}/EventSlipCount":
            ensure_signed_slips(rpc)
        else:
            ensure_key(rpc, path, tid, minimum, initial)
    for path, expected in MANAGED_VALUES.items():
        if not same_value(rpc.value(path), expected):
            require_stopped(rpc)
            rpc.checked("db_paste", {"paths": [path], "values": [expected]})
    ensure_links(rpc, "BOR", BOR)
    ensure_links(rpc, "EOR", EOR)
    require_stopped(rpc)
    inspect_schema(rpc)


def main():
    try:
        configure(Rpc())
        print("Live ODB fixed schema verified")
    except Exception as error:
        print(f"deploy3: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
