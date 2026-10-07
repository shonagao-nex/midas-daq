#!/usr/bin/env python3
"""Host-only controller for the four production MIDAS support services."""

import json
import os
import socket
import subprocess
import sys
import time
import urllib.request

UNITS = (
    ("midas-mhttpd.service", "mhttpd", "/home/daq/midas/midas_src/bin/mhttpd", 8081),
    ("midas-mlogger.service", "Logger", "/home/daq/midas/midas_src/bin/mlogger", None),
    ("midas-daq-monitor.service", "daq_monitor",
     "/home/daq/midas/midas/online/daq_monitor/bin/daq_monitor", None),
    ("midas-analyzer.service", "ana",
     "/home/daq/midas/midas/online/analyzer/bin/midas_analyzer", 8082),
)
URL = "http://127.0.0.1:8081/?mjsonrpc"
WAIT = 35


class Unsafe(RuntimeError):
    pass


def systemctl(*args):
    return subprocess.run(("systemctl", "--no-pager", *args),
                          text=True, capture_output=True, check=False)


def properties(unit):
    result = systemctl("show", unit, "-p", "ActiveState", "-p", "SubState",
                       "-p", "MainPID", "-p", "User", "-p", "Restart",
                       "-p", "TimeoutStopUSec", "-p", "KillMode",
                       "-p", "SendSIGKILL", "-p", "ControlGroup")
    if result.returncode:
        raise Unsafe(f"{unit}: systemctl show failed: {result.stderr.strip()}")
    return dict(line.split("=", 1) for line in result.stdout.splitlines()
                if "=" in line)


def rpc(method, paths):
    if method not in ("db_get_values", "db_copy"):
        raise Unsafe("non-read-only RPC refused")
    payload = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method,
                          "params": {"paths": paths}}).encode()
    request = urllib.request.Request(URL, payload,
                                     {"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=4) as reply:
        result = json.load(reply)["result"]
    if any(status != 1 for status in result["status"]):
        raise Unsafe(f"{method}: ODB read failed: {result['status']}")
    return result["data"]


def odb_snapshot():
    name, state, transition = rpc("db_get_values",
                                  ["/Experiment/Name", "/Runinfo/State",
                                   "/Runinfo/Transition in progress"])
    if name != "daq":
        raise Unsafe(f"unexpected experiment: {name}")
    clients = rpc("db_copy", ["/System/Clients"])[0]
    return state, transition, clients


def port_listening(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1):
            return True
    except OSError:
        return False


def proc_identity(pid):
    if not isinstance(pid, int) or pid <= 1:
        return None
    try:
        exe = os.readlink(f"/proc/{pid}/exe")
        with open(f"/proc/{pid}/cmdline", "rb") as file:
            cmd = file.read().replace(b"\0", b" ").decode(errors="replace").strip()
        with open(f"/proc/{pid}/cgroup", encoding="utf-8") as file:
            cgroup = file.read()
        return exe, cmd, cgroup
    except OSError:
        return None


def clients_for(clients, name):
    return [(int(pid), record) for pid, record in clients.items()
            if pid.isdecimal() and record.get("Name") == name]


def check_unit(entry, clients, require_active=True):
    unit, name, expected_exe, port = entry
    props = properties(unit)
    pid = int(props.get("MainPID") or "0")
    state = props.get("ActiveState")
    if require_active and (state != "active" or props.get("SubState") != "running"):
        raise Unsafe(f"{unit}: {state}/{props.get('SubState')}")
    matches = clients_for(clients, name)
    if len(matches) != 1 or matches[0][0] != pid:
        raise Unsafe(f"{unit}: expected one {name} client at MainPID {pid}, got "
                     f"{[p for p, _ in matches]}")
    identity = proc_identity(pid)
    if identity is None:
        raise Unsafe(f"{unit}: host /proc/{pid} unavailable")
    exe, cmd, cgroup = identity
    expected_cmd = expected_exe
    if name == "daq_monitor":
        expected_cmd += " -e daq"
    if name == "ana":
        expected_cmd += " --no-profiler"
    group = f"/system.slice/{unit}"
    if (exe != expected_exe or cmd != expected_cmd or
            props.get("ControlGroup") != group or group not in cgroup):
        raise Unsafe(f"{unit}: executable/cgroup does not match MainPID {pid}")
    if port and not port_listening(port):
        raise Unsafe(f"{unit}: port {port} is not listening")
    return pid


def host_guard():
    if os.geteuid() != 0:
        raise Unsafe("run control operations as root on the DAQ host")
    if not os.path.exists("/proc/1/exe") or not os.path.basename(
            os.readlink("/proc/1/exe")).startswith("systemd"):
        raise Unsafe("host PID namespace required")
    if not os.path.isdir("/home/daq/midas/midas/daq"):
        raise Unsafe("production experiment directory missing")


def stop_guard():
    for unit, _, _, _ in UNITS:
        props = properties(unit)
        if props.get("SendSIGKILL") != "no":
            raise Unsafe(f"{unit}: SendSIGKILL=no drop-in is not effective")
        if props.get("KillMode") != "control-group":
            raise Unsafe(f"{unit}: unexpected KillMode")


def stopped_snapshot():
    state, transition, clients = odb_snapshot()
    if state != 1 or transition != 0:
        raise Unsafe(f"Run State={state}, transition={transition}; STOPPED required")
    return clients


def wait_ready(entry):
    deadline = time.monotonic() + WAIT
    last = ""
    while time.monotonic() < deadline:
        try:
            _, _, clients = odb_snapshot()
            check_unit(entry, clients)
            return
        except (Unsafe, OSError, ValueError, KeyError) as error:
            last = str(error)
            time.sleep(1)
    raise Unsafe(f"{entry[0]}: not ready after {WAIT}s: {last}")


def start():
    host_guard()
    stop_guard()
    # The ODB HTTP endpoint is unavailable if mhttpd is stopped.
    clients = None
    if properties(UNITS[0][0]).get("ActiveState") == "active":
        clients = stopped_snapshot()
    for entry in UNITS:
        unit, _, expected_exe, port = entry
        props = properties(unit)
        if props.get("ActiveState") not in ("active", "inactive"):
            raise Unsafe(f"{unit}: unexpected pre-start state")
        if props.get("ActiveState") == "inactive":
            if props.get("MainPID") != "0" or proc_identity_for_exe(expected_exe):
                raise Unsafe(f"{unit}: residual or unmanaged process")
            if port and port_listening(port):
                raise Unsafe(f"{unit}: port {port} already occupied")
    for entry in UNITS:
        unit, name, expected_exe, port = entry
        props = properties(unit)
        registered = clients_for(clients, name) if clients is not None else []
        if props.get("ActiveState") == "active":
            if clients is None:
                raise Unsafe(f"{unit}: ODB is unavailable")
            check_unit(entry, clients)
            continue
        if props.get("ActiveState") != "inactive" or props.get("MainPID") != "0":
            raise Unsafe(f"{unit}: unexpected pre-start state {props.get('ActiveState')}")
        if registered or (port and port_listening(port)):
            raise Unsafe(f"{unit}: stale client or occupied port")
        result = systemctl("start", unit)
        if result.returncode:
            raise Unsafe(f"{unit}: start failed: {result.stderr.strip()}")
        wait_ready(entry)
        if clients is None:
            clients = stopped_snapshot()
        else:
            clients = odb_snapshot()[2]
    print("Four production services are running; RUN was not started.")


def proc_identity_for_exe(expected):
    for item in os.listdir("/proc"):
        if item.isdecimal():
            identity = proc_identity(int(item))
            if identity and identity[0] == expected:
                return True
    return False


def cgroup_pids(unit):
    group = properties(unit).get("ControlGroup", "")
    if not group.startswith("/system.slice/") or group != f"/system.slice/{unit}":
        raise Unsafe(f"{unit}: unexpected control group {group!r}")
    path = f"/sys/fs/cgroup{group}/cgroup.procs"
    try:
        with open(path, encoding="ascii") as file:
            return [int(line) for line in file if line.strip().isdecimal()]
    except FileNotFoundError:
        return []
    except OSError as error:
        raise Unsafe(f"{unit}: cannot inspect control group: {error}") from error


def stop_one(entry, old_pid, clients):
    unit, name, _, _ = entry
    result = systemctl("stop", unit)
    if result.returncode:
        raise Unsafe(f"{unit}: stop failed: {result.stderr.strip()}")
    props = properties(unit)
    if props.get("ActiveState") != "inactive" or props.get("MainPID") != "0":
        raise Unsafe(f"{unit}: stop incomplete: {props.get('ActiveState')}")
    if proc_identity(old_pid) or cgroup_pids(unit):
        raise Unsafe(f"{unit}: original PID {old_pid} remains")
    if name != "mhttpd":
        fresh = odb_snapshot()[2]
        if clients_for(fresh, name):
            raise Unsafe(f"{unit}: client remains registered")


def stop():
    host_guard()
    stop_guard()
    clients = stopped_snapshot()
    pids = {entry[0]: check_unit(entry, clients) for entry in UNITS}
    for entry in reversed(UNITS):
        stop_one(entry, pids[entry[0]], clients)
    print("Four production services stopped normally.")


def status():
    try:
        state, transition, clients = odb_snapshot()
        run_name = {1: "STOPPED", 2: "PAUSED", 3: "RUNNING"}.get(state, "UNKNOWN")
        print(f"Experiment=daq Run State={run_name}({state}) transition={transition}")
    except (Unsafe, OSError, ValueError, KeyError) as error:
        clients = None
        print(f"Experiment/Run State/transition=UNKNOWN ({error})")
    for entry in UNITS:
        unit, name, expected_exe, port = entry
        try:
            props = properties(unit)
            pid = int(props.get("MainPID") or "0")
            identity = proc_identity(pid)
            registered = ("UNKNOWN" if clients is None else
                          str([p for p, _ in clients_for(clients, name)]))
            print(f"{unit}: {props.get('ActiveState','UNKNOWN')}/"
                  f"{props.get('SubState','UNKNOWN')} MainPID={pid} "
                  f"client={registered} exe={identity[0] if identity else 'UNKNOWN'} "
                  f"cmdline={identity[1] if identity else 'UNKNOWN'}")
        except (Unsafe, ValueError) as error:
            print(f"{unit}: UNKNOWN ({error})")
        if port:
            print(f"port {port}: {'LISTEN' if port_listening(port) else 'UNKNOWN'}")


def recover():
    host_guard()
    stop_guard()
    try:
        state, transition, clients = odb_snapshot()
    except (Unsafe, OSError, ValueError, KeyError):
        state, transition, clients = None, None, None
    if state != 1 or transition != 0:
        print("DANGER: RUNNING/PAUSED/UNKNOWN or transition in progress. "
              "Data, EOR and Runlog may become inconsistent.", file=sys.stderr)
        if input("Type RECOVER to proceed: ") != "RECOVER":
            raise Unsafe("operator did not confirm recovery")
    old = {}
    for entry in UNITS:
        props = properties(entry[0])
        pid = int(props.get("MainPID") or "0")
        if pid:
            identity = proc_identity(pid)
            if (not identity or identity[0] != entry[2] or
                    props.get("ControlGroup") != f"/system.slice/{entry[0]}" or
                    props["ControlGroup"] not in identity[2]):
                raise Unsafe(f"{entry[0]}: MainPID identity cannot be verified")
        old[entry[0]] = pid
    # Complete all four stop jobs before considering forced signals.
    for entry in reversed(UNITS):
        unit = entry[0]
        if properties(unit).get("ActiveState") == "inactive":
            continue
        result = systemctl("stop", unit)
        if result.returncode:
            print(f"{unit}: graceful stop failed; checking residual PID", file=sys.stderr)
    for entry in reversed(UNITS):
        unit = entry[0]
        if int(properties(unit).get("MainPID") or "0") not in (0, old[unit]):
            raise Unsafe(f"{unit}: MainPID changed during recovery")
        for signal in (15, 9):
            pids = cgroup_pids(unit)
            if not pids:
                break
            for pid in pids:
                identity = proc_identity(pid)
                if not identity or unit not in identity[2]:
                    raise Unsafe(f"{unit}: residual PID {pid} cannot be verified")
                # PID reuse is checked immediately before each signal.
                os.kill(pid, signal)
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline and cgroup_pids(unit):
                time.sleep(0.5)
        if cgroup_pids(unit) or properties(unit).get("MainPID") not in ("0", ""):
            raise Unsafe(f"{unit}: process remains after recovery stop")
    if any(proc_identity_for_exe(entry[2]) for entry in UNITS):
        raise Unsafe("managed executable remains; refusing restart")
    # Starting mhttpd permits a fresh ODB read. Any pre-existing client
    # registration for the other services is detected before they start.
    for entry in UNITS:
        if entry != UNITS[0]:
            _, _, current_clients = odb_snapshot()
            if clients_for(current_clients, entry[1]):
                raise Unsafe(f"{entry[0]}: stale client remains")
        result = systemctl("start", entry[0])
        if result.returncode:
            raise Unsafe(f"{entry[0]}: restart failed: {result.stderr.strip()}")
        wait_ready(entry)
    state, transition, clients = odb_snapshot()
    for entry in UNITS:
        check_unit(entry, clients)
    print(f"Recovery complete: four services healthy; Run State={state}, "
          f"transition={transition}. No RUN or hardware action performed.")


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ("status", "start", "stop", "recover"):
        raise Unsafe("usage: prod_{status,start,stop,recover}.sh")
    {"status": status, "start": start, "stop": stop, "recover": recover}[sys.argv[1]]()


if __name__ == "__main__":
    try:
        main()
    except (Unsafe, OSError, ValueError, KeyError, KeyboardInterrupt) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
