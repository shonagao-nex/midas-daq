#!/usr/bin/env python3
"""Configure the daq-dev standard MIDAS JSON runlog through port 8181."""

import json
import urllib.request


URL = "http://127.0.0.1:8181/?mjsonrpc"
RUN_PARAMETERS = "/Experiment/Run Parameters"
JSON_ROOT = "/Logger/Runlog/JSON"


def rpc(method, params):
    request = urllib.request.Request(
        URL, json.dumps({"jsonrpc": "2.0", "method": method,
                         "params": params, "id": 1}).encode(),
        {"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=5) as response:
        result = json.load(response)
    if "error" in result:
        raise RuntimeError(f"{method}: {result['error']}")
    return result["result"]


def get(path):
    result = rpc("db_get_values", {"paths": [path]})
    return result["status"][0], result["data"][0]


def ensure_key(path, tid, **options):
    status, value = get(path)
    if status == 1:
        return value
    result = rpc("db_create", [{"path": path, "type": tid, **options}])
    if result["status"] != [1]:
        raise RuntimeError(f"Cannot create {path}: {result}")
    return None


def set_value(path, value):
    result = rpc("db_paste", {"paths": [path], "values": [value]})
    status, actual = get(path)
    if result["status"] != [1] or status != 1 or str(actual) != str(value):
        raise RuntimeError(f"Cannot set {path}: {result}")


def ensure_string_size(path, minimum):
    result = rpc("db_key", {"paths": [path]})
    if result["status"] != [1] or result["keys"][0]["type"] != 12:
        raise RuntimeError(f"{path} is not an ODB STRING")
    if result["keys"][0]["item_size"] >= minimum:
        return
    resized = rpc("db_resize_string", {"paths": [path],
               "new_lengths": [1], "new_string_lengths": [minimum]})
    if resized["status"] != [1]:
        raise RuntimeError(f"Cannot resize {path}: {resized}")


def ensure_link(name, target, phase):
    path = f"{JSON_ROOT}/Links {phase}/{name}"
    status, _ = get(path)
    if status == 1:
        return
    result = rpc("db_link", {"new_links": [path], "target_paths": [target]})
    if result["status"] != [1]:
        raise RuntimeError(f"Cannot link {path}: {result}")


def remove_link(name, phase):
    path = f"{JSON_ROOT}/Links {phase}/{name}"
    if get(path)[0] != 1:
        return
    result = rpc("db_delete", {"paths": [path]})
    if result["status"] != [1] or get(path)[0] == 1:
        raise RuntimeError(f"Cannot remove {path}: {result}")


def reorder_eor_links(links):
    """MIDAS preserves ODB insertion order in the generated JSON."""
    path = f"{JSON_ROOT}/Links EOR"
    result = rpc("db_get_values", {"paths": [path]})
    if result["status"] != [1]:
        raise RuntimeError(f"Cannot inspect {path}")
    present = [value for key, value in result["data"][0].items()
               if key.endswith("/name")]
    if [name.lower() for name in present] == [name.lower() for name, _ in links]:
        return
    expected = set(name.lower() for name, _ in links)
    stale = {"durationsec", "vmeevents", "easirocevents",
             "daqstatus", "daqsummary"}
    unexpected = set(name.lower() for name in present) - expected - stale
    if unexpected:
        raise RuntimeError(f"Unexpected EOR links: {sorted(unexpected)}")
    for name in present:
        if name.lower() in stale and get(f"{path}/{name}") != (1, {}):
            raise RuntimeError(f"Cannot remove nonempty legacy EOR key {name}")
    for name in present:
        remove_link(name, "EOR")
    for name, target in links:
        ensure_link(name, target, "EOR")


def main():
    if get("/Experiment/Name") != (1, "daq-dev"):
        raise RuntimeError("Port 8181 is not daq-dev")
    if get("/Custom/Path") != (
            1, "/home/nagao/midas/midas/online/daq_monitor/web"):
        raise RuntimeError("Development Custom/Path does not match")
    if get("/Runinfo/State") != (1, 1) or get(
            "/Runinfo/Transition in progress") != (1, 0):
        raise RuntimeError("Configure only while STOPPED")
    if get("/Logger/Write data") != (1, True):
        raise RuntimeError("Logger/Write data must already be enabled")
    if get("/Equipment/HUL")[0] == 1:
        raise RuntimeError("HUL equipment exists; implement participation and event count first")

    ensure_key(RUN_PARAMETERS, 15)
    if ensure_key(f"{RUN_PARAMETERS}/Type", 12, string_length=32) is None:
        set_value(f"{RUN_PARAMETERS}/Type", "Data")
    # UTF-8 can use four bytes per character, plus the terminating NUL.
    ensure_key(f"{RUN_PARAMETERS}/Comment", 12, string_length=4097)
    ensure_string_size(f"{RUN_PARAMETERS}/Comment", 4097)
    ensure_key(f"{RUN_PARAMETERS}/ExperimentLabel", 12, string_length=1024)
    ensure_string_size(f"{RUN_PARAMETERS}/ExperimentLabel", 1024)

    root = "/DAQ/Status/Runlog"
    ensure_key(root, 15)
    for name, tid, length in (
            ("DurationSec", 18, 0), ("VMEEvents", 17, 0),
            ("EASIROCEvents", 17, 0), ("HULEvents", 17, 0),
            ("EventSlipCount", 18, 0),
            ("DAQStatus", 12, 512), ("DAQSummary", 12, 512)):
        ensure_key(f"{root}/{name}", tid,
                   **({"string_length": length} if length else {}))
    set_value(f"{root}/HULEvents", -1)

    for name, target in (
            ("Run number", "/Runinfo/Run number"),
            ("Start time", "/Runinfo/Start time"),
            ("Type", f"{RUN_PARAMETERS}/Type"),
            ("experiment_label", f"{RUN_PARAMETERS}/ExperimentLabel")):
        ensure_link(name, target, "BOR")
    eor_links = (
            ("Stop time", "/Runinfo/Stop time"),
            ("Comment", f"{RUN_PARAMETERS}/Comment"),
            ("Duration", f"{root}/DurationSec"),
            ("VME events", f"{root}/VMEEvents"),
            ("EASIROC events", f"{root}/EASIROCEvents"),
            ("HUL events", f"{root}/HULEvents"),
            ("EventSlipCount", f"{root}/EventSlipCount"),
            ("DAQ Status", f"{root}/DAQStatus"),
            ("DAQ Summary", f"{root}/DAQSummary"),
            ("Scaler 64ch", f"{root}/Scaler64ch"))
    # No scaler count is published today. An empty directory serializes as
    # an empty object; future real ch00..ch63 keys can be added without a
    # change to the Runlog link or a fabricated channel value.
    ensure_key(f"{root}/Scaler64ch", 15)
    reorder_eor_links(eor_links)
    remove_link("Comment", "BOR")
    set_value(f"{JSON_ROOT}/Write data", True)
    print("Configured daq-dev JSON Runlog on port 8181")


if __name__ == "__main__":
    main()
