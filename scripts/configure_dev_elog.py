#!/usr/bin/env python3
"""Create daq-dev automatic Built-in ELOG settings (disabled by default)."""

from configure_dev_runlog import get, rpc


ROOT = "/Experiment/Run Elog"


def create(path, tid, initial=None, string_length=0):
    status, _ = get(path)
    if status == 1:
        return
    definition = {"path": path, "type": tid}
    if string_length:
        definition["string_length"] = string_length
    result = rpc("db_create", [definition])
    if result["status"] != [1]:
        raise RuntimeError(f"Cannot create {path}: {result}")
    if initial is not None:
        written = rpc("db_paste", {"paths": [path], "values": [initial]})
        if written["status"] != [1]:
            raise RuntimeError(f"Cannot initialize {path}: {written}")


def main():
    if get("/Experiment/Name") != (1, "daq-dev") or get(
            "/Custom/Path") != (1, "/home/nagao/midas/midas/online/daq_monitor/web"):
        raise RuntimeError("Port 8181 is not the expected daq-dev instance")
    if get("/Runinfo/State") != (1, 1):
        raise RuntimeError("Configure only while STOPPED")
    if get("/Elog/External Elog") != (1, False):
        raise RuntimeError("Built-in ELOG is not selected")
    if "Routine" not in get("/Elog/Types")[1] or "DAQ" not in get("/Elog/Systems")[1]:
        raise RuntimeError("Routine/DAQ ELOG categories are unavailable")
    if get("/Logger/Runlog/JSON/Write data") != (1, True):
        raise RuntimeError("Standard JSON Runlog is not enabled")

    current_run = get("/Runinfo/Run number")[1]
    create(ROOT, 15)
    create(f"{ROOT}/Enabled", 8, False)
    create(f"{ROOT}/Author", 12, "DAQ", 100)
    create(f"{ROOT}/Type", 12, "Routine", 100)
    create(f"{ROOT}/System", 12, "DAQ", 100)
    create(f"{ROOT}/Last Run", 7, 0)
    create(f"{ROOT}/Last Attempt Run", 7, current_run)
    create(f"{ROOT}/Last Status", 12, "DISABLED", 64)
    create(f"{ROOT}/Last Error", 12, "", 1024)
    create(f"{ROOT}/Web Port", 7, 8181)
    create("/DAQ/Status/Runlog/EORCompleteRunNumber", 7, 0)
    print("Configured daq-dev Built-in ELOG settings")


if __name__ == "__main__":
    main()
