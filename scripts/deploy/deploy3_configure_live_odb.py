#!/usr/bin/env python3
"""Converge only the explicitly owned live ODB schema on the DAQ host."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import subprocess
import urllib.request

sys.dont_write_bytecode = True
_module = importlib.util.spec_from_file_location("frontend_odb", Path(__file__).with_name("frontend_odb.py"))
frontend_odb = importlib.util.module_from_spec(_module)
_module.loader.exec_module(frontend_odb)


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
    def __init__(self, url=URL, readonly=False):
        self.url = url
        self.readonly = readonly

    def call(self, method, params):
        if self.readonly and method not in ("db_get_values", "db_key", "db_copy", "cm_exist"):
            raise RuntimeError("Read-only deployment preview rejected " + method)
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
        ("/Programs/fehul", KEY, 0, None),
        ("/Custom", KEY, 0, None),
        ("/Custom/Path", STRING, 256, str(ROOT / "daq_monitor/web")),
        ("/Custom/DAQ", STRING, 256, "daq.html"),
        ("/Custom/Run Summary", STRING, 32, "run-summary.html"),
        ("/Custom/EASI Slow Control", STRING, 32, "easiroc.html"),
    ]
    for name in ("fevme", "feeasiroc", "fehul"):
        prefix = f"/Programs/{name}"
        values.extend((
            (f"{prefix}/Required", BOOL, 0, name != "fehul"),
            (f"{prefix}/Watchdog timeout", INT32, 0, 10000),
            (f"{prefix}/Check interval", UINT32, 0, 180000),
            (f"{prefix}/Start command", STRING, 256,
             str(ROOT / f"scripts/start_{name}.sh")),
            (f"{prefix}/Auto start", BOOL, 0, False),
            (f"{prefix}/Auto stop", BOOL, 0, False),
            (f"{prefix}/Auto restart", BOOL, 0, False),
            (f"{prefix}/Alarm class", STRING, 32, ""),
            # cm_set_client_info() default; al_check() reads this even if not Required.
            # Runtime history: initialize only when absent, never put in MANAGED_VALUES.
            (f"{prefix}/First failed", UINT32, 0, 0),
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
for _name in ("fevme",):
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


TID_NAMES = {4: "WORD", UINT32: "DWORD", INT32: "INT", BOOL: "BOOL",
             DOUBLE: "DOUBLE", STRING: "STRING", KEY: "KEY"}


def daq_settings_specs():
    """Settings read by the frontends; (path, type, count, minimum bytes)."""
    specs = []

    def add(base, names, tid, count=1, minimum=0):
        specs.extend((f"{base}/{name}", tid, count, minimum) for name in names)

    vme = "/Equipment/VME/Settings"
    add(f"{vme}/RPV130", ("Enabled", "SingleEventBusyEnabled"), BOOL)
    add(f"{vme}/V792", ("Enabled", "ZeroSuppressionEnabled", "AllTriggerEnabled"), BOOL)
    add(f"{vme}/V792", ("Iped",), 4)
    add(f"{vme}/V1190", ("Enabled", "TriggerMatchingEnabled",
        "TriggerSubtractionEnabled", "TdcHeaderEnabled", "EmptyEventEnabled",
        "EventFifoEnabled", "ExtendedTriggerTimeEnabled"), BOOL)
    add(f"{vme}/V1190", ("WindowWidth", "ExtraSearchMargin", "RejectMargin",
        "EdgeMode", "ResolutionPs", "DeadTimeNs"), UINT32)
    add(f"{vme}/V1190", ("WindowOffset", "MaxHitsPerEvent"), INT32)
    add(f"{vme}/V1190", ("ChannelEnabled",), BOOL, 128)
    add(f"{vme}/V775", ("Enabled", "OverRangeEnabled", "LowThresholdEnabled",
        "CommonStop", "EmptyProgramEnabled", "ValidControlEnabled",
        "SlidingScaleEnabled", "AllTriggerEnabled"), BOOL)
    add(f"{vme}/V775", ("FullScaleRange",), 4)
    add(f"{vme}/V1720E", ("Enabled", "SoftwareTriggerEnabled",
        "ExternalTriggerEnabled"), BOOL)
    add(f"{vme}/V1720E", ("BufferOrganization", "RecordLengthSamples",
        "PostTrigger"), UINT32)
    add(f"{vme}/V1720E", ("ChannelSelfTriggerEnabled", "ChannelEnabled"), BOOL, 8)
    add(f"{vme}/V1720E", ("DCOffset",), 4, 8)

    easiroc = "/Equipment/EASIROC/Settings"
    add(easiroc, ("Enabled", "Acquisition/ADCEnabled",
        "Acquisition/TDCEnabled", "Acquisition/ScalerEnabled",
        "ASICSlowControl/ApplyAtBOR"), BOOL)
    add(easiroc, ("Network/IPAddress",), STRING, minimum=64)
    for asic in ("ASIC1", "ASIC2"):
        base = f"{easiroc}/{asic}"
        add(base, ("DiscriminatorDACCode", "DiscriminatorDACSlope",
            "HGFeedbackCapacitance", "LGFeedbackCapacitance",
            "HGShapingTime", "LGShapingTime"), INT32)
        add(base, ("InputDAC",), INT32, 32)
        add(base, ("ChannelEnabled",), BOOL, 32)
    return specs


def describe_daq_key(key):
    if key is None:
        return "missing"
    tid = key.get("type")
    name = TID_NAMES.get(tid, str(tid))
    description = f"{name}[{key.get('num_values', '?')}]"
    if tid == STRING:
        description += f" item_size={key.get('item_size', '?')}"
    return description


def require_daq_key(rpc, path, tid, count=1, minimum=0):
    key = rpc.key(path)
    expected = f"{TID_NAMES[tid]}[{count}]"
    if tid == STRING and minimum:
        expected += f" item_size>={minimum}"
    if (key is None or key.get("type") != tid or
            key.get("num_values") != count or
            (tid == STRING and key.get("item_size", 0) < minimum)):
        raise RuntimeError(f"DAQ ODB schema {path}: expected {expected}, "
                           f"actual {describe_daq_key(key)}")
    return key


def daq_children(rpc, path):
    """Enumerate directory names without treating any stored values as policy."""
    require_daq_key(rpc, path, KEY)
    copied = rpc.copy(path)
    if not isinstance(copied, dict):
        raise RuntimeError(f"DAQ ODB schema {path}: expected KEY[1], "
                           f"actual malformed directory {type(copied).__name__}")
    return [name for name in copied if not name.endswith("/key")]


def inspect_daq_settings_schema(rpc, allow_missing_easiroc=False):
    """Read-only validation, shared by deploy3 preflight and deploy4."""
    for spec in daq_settings_specs():
        if allow_missing_easiroc and spec[0].startswith("/Equipment/EASIROC/Settings/") and rpc.key(spec[0]) is None:
            continue
        require_daq_key(rpc, *spec)

    analyzer = rpc.key("/Analyzer")
    if analyzer is None:
        return  # The analyzer supports in-memory defaults for an absent tree.
    require_daq_key(rpc, "/Analyzer", KEY)

    histograms = "/Analyzer/Histograms"
    if rpc.key(histograms) is not None:
        for group in daq_children(rpc, histograms):
            base = f"{histograms}/{group}"
            require_daq_key(rpc, base, KEY)
            name = rpc.key(f"{base}/HistName")
            if name is None or name.get("type") != STRING or not isinstance(
                    name.get("num_values"), int) or name["num_values"] < 1:
                raise RuntimeError(f"DAQ ODB schema {base}/HistName: expected "
                                   f"STRING[N], N>=1, actual {describe_daq_key(name)}")
            count = name["num_values"]
            for field in ("HistName", "Title", "XTitle", "YTitle", "Type",
                          "Expression", "Cut"):
                require_daq_key(rpc, f"{base}/{field}", STRING, count, 1)
            for field, tid in (("Bins", INT32), ("Min", DOUBLE),
                               ("Max", DOUBLE), ("Enabled", BOOL)):
                require_daq_key(rpc, f"{base}/{field}", tid, count)

    pages = "/Analyzer/Pages"
    if rpc.key(pages) is not None:
        for page in daq_children(rpc, pages):
            base = f"{pages}/{page}"
            require_daq_key(rpc, base, KEY)
            require_daq_key(rpc, f"{base}/Rows", INT32)
            require_daq_key(rpc, f"{base}/Columns", INT32)
            # Missing PadNN fields are deliberately empty pads in the loader.

    if rpc.key("/Analyzer/OnlineHistogram") is not None:
        require_daq_key(rpc, "/Analyzer/OnlineHistogram", KEY)
        require_daq_key(rpc, "/Analyzer/OnlineHistogram/FillPrescale", INT32)


def inspect_schema(rpc):
    inspect_daq_settings_schema(rpc)
    for path, tid, minimum, _ in specs():
        if not inspect_key(rpc, path, tid, minimum):
            raise RuntimeError(f"Missing schema key: {path}")
    inspect_links(rpc, "BOR", BOR)
    inspect_links(rpc, "EOR", EOR)
    for path, expected in MANAGED_VALUES.items():
        if not same_value(rpc.value(path), expected):
            raise RuntimeError(f"Managed ODB value differs: {path}")
    inspect_live_paths(rpc)
    frontend_plan = frontend_odb.plan(rpc)
    if frontend_plan["add"]:
        raise RuntimeError("Missing deploy-owned frontend keys: " +
                           ", ".join(item["path"] for item in frontend_plan["add"]))


def check_checkpoint(journal_path):
    if journal_path is None or not journal_path.exists():
        return
    state = json.loads(journal_path.read_text())
    for path, item in state.get("frontend_initialization", {}).items():
        if item.get("phase") != "written":
            raise RuntimeError("Unfinished frontend initialization at " + path +
                               "; inspect ODB and saved backup before an explicit repair; no automatic overwrite")


def checkpoint_writer(journal_path):
    if journal_path is None:
        return None  # in-memory/unit callers; CLI always supplies the whole-deploy journal
    if not journal_path.is_file() or json.loads(journal_path.read_text()).get("phase") != "in_progress":
        raise RuntimeError("New frontend Settings require the active whole-deployment journal")

    def record(item, phase):
        state = json.loads(journal_path.read_text())
        if state.get("phase") != "in_progress":
            raise RuntimeError("Deployment journal changed during frontend initialization")
        state.setdefault("frontend_initialization", {})[item["path"]] = {**item, "phase": phase}
        temporary = journal_path.with_name(journal_path.name + f".frontend-{os.getpid()}.tmp")
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            with os.fdopen(fd, "w") as stream:
                json.dump(state, stream, indent=2, sort_keys=True)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, journal_path)
        finally:
            if temporary.exists():
                temporary.unlink()
    return record


def preflight(rpc, journal_path=None):
    require_live(rpc)
    check_checkpoint(journal_path)
    # Fail before changing deploy-owned keys if DAQ settings need intervention.
    inspect_daq_settings_schema(rpc, allow_missing_easiroc=True)
    frontend_plan = frontend_odb.plan(rpc)
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
        frontend_odb.validate(rpc, path, (tid, 1, minimum, initial))
        if rpc.key(path) is not None:
            inspect_key(rpc, path, tid, minimum)
        if path == "/Custom/Path" and rpc.key(path) is not None and rpc.value(path) != str(ROOT / "daq_monitor/web"):
            raise RuntimeError("Existing /Custom/Path points elsewhere")
        if path == f"{ELOG}/Web Port" and rpc.key(path) is not None and rpc.value(path) != 8081:
            raise RuntimeError("Existing Run Elog Web Port is not 8081")
    for name in ("feeasiroc", "fehul"):
        command = rpc.value(f"/Programs/{name}/Start command", missing_ok=True)
        if command is not None and command != str(ROOT / f"scripts/start_{name}.sh"):
            raise RuntimeError(f"Existing {name} start command needs review; not overwritten")
        for flag in ("Auto start", "Auto stop", "Auto restart"):
            value = rpc.value(f"/Programs/{name}/{flag}", missing_ok=True)
            if value not in (None, False):
                raise RuntimeError(f"Existing {name} {flag} needs review; not overwritten")
    page = rpc.value("/Custom/EASI Slow Control", missing_ok=True)
    if page is not None and page != "easiroc.html":
        raise RuntimeError("Existing EASI Slow Control registration differs; not overwritten")
    for phase, expected in (("BOR", BOR), ("EOR", EOR)):
        directory = f"{JSON_ROOT}/Links {phase}"
        if rpc.key(directory) is not None:
            actual = link_map(rpc, phase)
            allowed = {n.casefold() for n, _ in expected}
            if any(n.casefold() not in allowed for n, _ in actual):
                raise RuntimeError(f"Unexpected {phase} link names")
    fixed = [{"path": path, "type": tid, "string_length": minimum, "value": initial}
             for path, tid, minimum, initial in specs() if rpc.key(path) is None]
    managed = [{"path": path, "value": value} for path, value in MANAGED_VALUES.items()
               if not same_value(rpc.value(path, missing_ok=True), value)]
    clients = {}
    for name in ("fevme", "feeasiroc", "fehul"):
        status = rpc.call("cm_exist", {"name": name, "unique": True})["status"]
        if status not in (1, 103):
            raise RuntimeError(f"Cannot inspect frontend {name}: {status}")
        clients[name] = status == 1
    return {"frontend": frontend_plan, "fixed_additions": fixed, "legacy_managed_updates": managed,
            "current_frontend_clients": clients,
            "services": ["midas-daq-monitor.service", "midas-analyzer.service"],
            "frontends": "restore only originally running fevme/feeasiroc/fehul; no first HUL start"}


def configure(rpc, journal_path=None):
    planned = preflight(rpc, journal_path)
    checkpoint = checkpoint_writer(journal_path) if planned["frontend"]["add"] else None
    # The whole-deploy orchestrator stops these clients before calling step3.
    for name in (("feeasiroc", "fehul") if planned["frontend"]["add"] else ()):
        state = rpc.call("cm_exist", {"name": name, "unique": True})["status"]
        if state != 103:
            raise RuntimeError(f"Stop {name} before frontend Settings initialization (status {state})")
    frontend_odb.apply(rpc, planned["frontend"], require_stopped, checkpoint)
    for path, tid, minimum, initial in specs():
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


def preview_checkout():
    def git(*args):
        return subprocess.run(("git", "--no-optional-locks", "-C", str(ROOT), *args),
                              text=True, capture_output=True, check=True).stdout.strip()
    return {"tracked_local_changes": git("status", "--short", "--untracked-files=no"),
            "cached_incoming_files": git("diff", "--name-status", "HEAD", "origin/main"),
            "note": "read-only check does not fetch; deploy1 fetches and displays exact incoming files before fast-forward"}


def preview_files():
    files = ("vme_frontend/bin/fevme", "easiroc_frontend/bin/feeasiroc", "hul_frontend/bin/fehul",
             "daq_monitor/bin/daq_monitor", "analyzer/bin/midas_analyzer",
             "scripts/start_feeasiroc.sh", "scripts/start_fehul.sh", "daq_monitor/web/easiroc.html",
             "daq_monitor/web/easiroc.js", "daq_monitor/web/easiroc.css", "daq_monitor/web/easiroc-mock-data.js",
             "daq_monitor/web/daq.html", "daq_monitor/web/daq.js")
    return {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() if (ROOT / name).is_file()
            else "missing (build/checkout required)" for name in files}


def main():
    parser = argparse.ArgumentParser(description="Whole-deployment ODB configuration; --check is read-only")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        if args.check:
            rpc = Rpc(readonly=True)
            preview = {"checkout": preview_checkout(), "files_sha256_before_build": preview_files()}
            if preview["checkout"]["tracked_local_changes"]:
                print(json.dumps(preview, indent=2))
                raise RuntimeError("Tracked local changes prevent deployment; see listed files")
            preview["odb"] = preflight(rpc, RUNTIME / "deploy-state.json")
            print(json.dumps(preview, indent=2))
            return 0
        configure(Rpc(), RUNTIME / "deploy-state.json")
        print("Live ODB fixed schema verified")
    except Exception as error:
        print(f"deploy3: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
