#!/usr/bin/env python3
"""Read-only verification of the deployed live checkout and MIDAS instance."""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import urllib.request


sys.dont_write_bytecode = True
module_path = Path(__file__).with_name("deploy3_configure_live_odb.py")
spec = importlib.util.spec_from_file_location("deploy_schema", module_path)
schema = importlib.util.module_from_spec(spec)
spec.loader.exec_module(schema)

ROOT = schema.ROOT
RUNTIME = schema.RUNTIME
FRONTENDS = ("fevme", "feeasiroc")
SERVICES = ("midas-mhttpd.service", "midas-mlogger.service",
            "midas-daq-monitor.service", "midas-analyzer.service")


def command(*args):
    return subprocess.run(args, text=True, capture_output=True, check=True).stdout.strip()


def frontend_running(rpc, name):
    result = rpc.call("cm_exist", {"name": name, "unique": True})
    status = result.get("status")
    if status not in (1, 103):
        raise RuntimeError(f"cm_exist({name}) returned {status}")
    return status == 1


def service_active(name):
    state = command("systemctl", "show", "--property=ActiveState", "--value", name)
    if state not in ("active", "inactive"):
        raise RuntimeError(f"Unexpected service state: {name} {state}")
    return state == "active"


def executable_matches(pid, expected):
    if pid <= 0:
        raise RuntimeError(f"Invalid process PID for {expected}: {pid}")
    actual = os.readlink(f"/proc/{pid}/exe")
    if actual != str(expected):
        raise RuntimeError(f"Running executable differs: pid={pid}, {actual}, expected={expected}")


def frontend_pids(rpc):
    clients = rpc.value("/System/Clients")
    found = {}
    for pid_text, record in clients.items():
        if not pid_text.isdigit() or not isinstance(record, dict):
            continue
        name = record.get("name")
        if name in FRONTENDS:
            found.setdefault(name, []).append(int(pid_text))
    return found


def page(url, marker=None):
    request = urllib.request.Request(url, headers={"Cache-Control": "no-cache"})
    with urllib.request.urlopen(request, timeout=8) as response:
        if response.status != 200:
            raise RuntimeError(f"HTTP {response.status}: {url}")
        body = response.read(2_000_000)
    if marker and marker not in body:
        raise RuntimeError(f"Unexpected page content: {url}")
    return body


def check(expected_commit=None, expected_frontends=None, expected_services=None):
    rpc = schema.Rpc()
    schema.require_live(rpc)
    if command("git", "-C", str(ROOT), "rev-parse", "--abbrev-ref", "HEAD") != "main":
        raise RuntimeError("Live checkout is not on main")
    commit = command("git", "-C", str(ROOT), "rev-parse", "HEAD")
    if expected_commit and commit != expected_commit:
        raise RuntimeError("Live Git commit differs from deployment target")
    for args in (("git", "-C", str(ROOT), "diff", "--quiet"),
                 ("git", "-C", str(ROOT), "diff", "--cached", "--quiet")):
        subprocess.run(args, check=True)
    for binary in ("vme_frontend/bin/fevme", "easiroc_frontend/bin/feeasiroc",
                   "daq_monitor/bin/daq_monitor", "analyzer/build/midas_analyzer"):
        path = ROOT / binary
        if not path.is_file() or not os.access(path, os.X_OK):
            raise RuntimeError(f"Missing production binary: {path}")
    for unit in SERVICES[2:]:
        installed = Path("/etc/systemd/system") / unit
        tracked = ROOT / "systemd" / unit
        if not installed.is_file() or installed.read_bytes() != tracked.read_bytes():
            raise RuntimeError(f"Installed unit differs from tracked unit: {unit}")
    for wrapper in ("scripts/start_fevme.sh", "scripts/start_feeasiroc.sh"):
        if not os.access(ROOT / wrapper, os.X_OK):
            raise RuntimeError(f"Frontend wrapper is not executable: {wrapper}")
    if (ROOT / "daq_monitor/web/runlogs").resolve() != RUNTIME / "runlogs":
        raise RuntimeError("Custom Runlog symlink points outside live runtime")
    for filename in ("runlog_index.json", "runlog_selection.json"):
        if not (RUNTIME / "runlogs" / filename).is_file():
            raise RuntimeError(f"Missing Runlog resource: {filename}")
    selection = json.loads((RUNTIME / "runlogs/runlog_selection.json").read_text())
    if not isinstance(selection, dict) or not isinstance(selection.get("runs"), list):
        raise RuntimeError("Invalid Runlog selection file")
    schema.inspect_schema(rpc)
    for name, expected_command in schema.ANALYZER_PROGRAMS.items():
        path = f"/Programs/{name}/Start command"
        if rpc.value(path) != expected_command:
            raise RuntimeError(f"Analyzer Programs start command differs: {path}")
    if rpc.value("/Logger/Write data") is not True:
        raise RuntimeError("Logger/Write data is disabled")
    if rpc.value("/Experiment/Prevent start on required progs") is not False:
        raise RuntimeError("Optional frontend policy has changed")
    if rpc.value("/Elog/External Elog") is not False:
        raise RuntimeError("Built-in ELOG is not selected")
    if not isinstance(rpc.value("/Runinfo/Run number"), int):
        raise RuntimeError("Run Number is unreadable")
    pids = frontend_pids(rpc)
    for name in FRONTENDS:
        running = frontend_running(rpc, name)
        if expected_frontends is not None and running != expected_frontends[name]:
            raise RuntimeError(f"Frontend state differs from deployment start: {name}")
        if running:
            if len(pids.get(name, [])) != 1:
                raise RuntimeError(f"Expected exactly one {name} client")
            binary = ("vme_frontend/bin/fevme" if name == "fevme" else
                      "easiroc_frontend/bin/feeasiroc")
            executable_matches(pids[name][0], ROOT / binary)
    for name in SERVICES:
        active = service_active(name)
        expected = True if name in SERVICES[:2] else (
            expected_services[name] if expected_services is not None else True)
        if active != expected:
            raise RuntimeError(f"Unexpected service state: {name} active={active}")
        if active and name in SERVICES[2:]:
            pid = int(command("systemctl", "show", "--property=MainPID", "--value", name))
            binary = ("daq_monitor/bin/daq_monitor" if name == SERVICES[2] else
                      "analyzer/build/midas_analyzer")
            executable_matches(pid, ROOT / binary)
            client = "daq_monitor" if name == SERVICES[2] else "ana"
            if rpc.call("cm_exist", {"name": client, "unique": True})["status"] != 1:
                raise RuntimeError(f"Service {name} has no MIDAS client {client}")
    page("http://127.0.0.1:8081/?cmd=custom&page=DAQ", b"daq.js")
    page("http://127.0.0.1:8081/?cmd=custom&page=Run%20Summary", b"run-summary.js")
    page("http://127.0.0.1:8081/daq.js")
    page("http://127.0.0.1:8081/run-summary.js")
    json.loads(page("http://127.0.0.1:8081/runlogs/runlog_index.json"))
    json.loads(page("http://127.0.0.1:8081/runlogs/runlog_selection.json"))
    schema.require_stopped(rpc)
    print(f"Live read-only smoke test passed: {commit}")


def main():
    expected_commit = os.environ.get("DEPLOY_TARGET_SHA") or None
    frontends = None
    if "DEPLOY_START_FEVME" in os.environ:
        frontends = {
            "fevme": os.environ["DEPLOY_START_FEVME"] == "1",
            "feeasiroc": os.environ["DEPLOY_START_FEEASIROC"] == "1",
        }
    services = None
    if "DEPLOY_START_MONITOR" in os.environ:
        services = {
            "midas-daq-monitor.service": os.environ["DEPLOY_START_MONITOR"] == "1",
            "midas-analyzer.service": os.environ["DEPLOY_START_ANALYZER"] == "1",
        }
    try:
        check(expected_commit, frontends, services)
    except Exception as error:
        print(f"deploy4: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
