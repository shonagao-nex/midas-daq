#!/usr/bin/env bash
set -euo pipefail

# Keep orchestration in this versioned file; Python handles the recovery journal
# and MIDAS JSON-RPC without shell evaluation of ODB or Git output.
export PYTHONDONTWRITEBYTECODE=1
python3 - "$@" <<'PY'
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path("/home/daq/midas/midas/online")
RUNTIME = Path("/home/daq/midas/midas")
JOURNAL = RUNTIME / "deploy-state.json"
BACKUPS = RUNTIME / "backups"
DEPLOY = ROOT / "scripts/deploy"
BINARIES = (
    "vme_frontend/bin/fevme",
    "easiroc_frontend/bin/feeasiroc",
    "daq_monitor/bin/daq_monitor",
    "analyzer/bin/midas_analyzer",
)
FRONTENDS = ("fevme", "feeasiroc")
SERVICES = ("midas-daq-monitor.service", "midas-analyzer.service")

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    "deploy_schema", DEPLOY / "deploy3_configure_live_odb.py")
schema = importlib.util.module_from_spec(spec)
spec.loader.exec_module(schema)
rpc = schema.Rpc()


def run(*args, env=None):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(tuple(map(str, args)), cwd=ROOT, env=env, check=True)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def binary_hashes():
    return {name: sha256(ROOT / name) if (ROOT / name).is_file() else None
            for name in BINARIES}


def atomic_json(path, value):
    temporary = path.with_name(path.name + f".{os.getpid()}.tmp")
    descriptor = os.open(temporary, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def read_journal():
    if not JOURNAL.exists():
        return None
    with JOURNAL.open(encoding="utf-8") as stream:
        data = json.load(stream)
    if data.get("phase") not in ("in_progress", "complete"):
        raise RuntimeError("Unrecognized deployment journal")
    return data


def started_flags():
    return {
        "frontends": {name: os.environ[f"DEPLOY_START_{name.upper()}"] == "1"
                      for name in FRONTENDS},
        "services": {"midas-daq-monitor.service":
                     os.environ["DEPLOY_START_MONITOR"] == "1",
                     "midas-analyzer.service":
                     os.environ["DEPLOY_START_ANALYZER"] == "1"},
    }


def active_service(name):
    output = subprocess.check_output(
        ("systemctl", "show", "--property=ActiveState", "--value", name),
        text=True).strip()
    if output not in ("active", "inactive"):
        raise RuntimeError(f"Unexpected service state {name}: {output}")
    return output == "active"


def frontend_running(name):
    result = rpc.call("cm_exist", {"name": name, "unique": True})["status"]
    if result not in (1, 103):
        raise RuntimeError(f"cm_exist({name}) returned {result}")
    return result == 1


def wait_frontend(name, wanted):
    for _ in range(120):
        if frontend_running(name) == wanted:
            return
        time.sleep(0.5)
    raise RuntimeError(f"Frontend {name} did not reach running={wanted}")


def wait_client(name):
    for _ in range(120):
        status = rpc.call("cm_exist", {"name": name, "unique": True})["status"]
        if status == 1:
            return
        if status != 103:
            raise RuntimeError(f"cm_exist({name}) returned {status}")
        time.sleep(0.5)
    raise RuntimeError(f"MIDAS client did not connect: {name}")


def backup_odb(commit):
    BACKUPS.mkdir(mode=0o700, exist_ok=True)
    filename = BACKUPS / ("odb-before-deploy-" + time.strftime("%Y%m%d-%H%M%S")
                          + f"-{os.getpid()}-{commit[:12]}.odb")
    env = os.environ.copy()
    env.update(HOME="/home/daq", MIDASSYS="/home/daq/midas/midas_src",
               MIDAS_EXPTAB=str(RUNTIME / "exptab"), MIDAS_EXPT_NAME="daq")
    for key in ("MIDAS_SERVER_HOST", "MIDAS_SERVER_PORT", "MIDAS_DIR"):
        env.pop(key, None)
    old_umask = os.umask(0o077)
    try:
        run("/home/daq/midas/midas_src/bin/odbedit", "-e", "daq",
            "-c", f"save {filename}", env=env)
    finally:
        os.umask(old_umask)
    if not filename.is_file() or filename.stat().st_size < 1024:
        raise RuntimeError("ODB backup is missing or unexpectedly small")
    filename.chmod(0o600)
    print(f"ODB backup: {filename}", flush=True)


def ensure_selection():
    directory = RUNTIME / "runlogs"
    if not directory.is_dir():
        raise RuntimeError(f"Missing live Runlog directory: {directory}")
    selection = directory / "runlog_selection.json"
    if not selection.exists():
        descriptor = os.open(selection, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o640)
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write('{"runs":[]}\n')
    with selection.open(encoding="utf-8") as stream:
        data = json.load(stream)
    if not isinstance(data, dict) or not isinstance(data.get("runs"), list):
        raise RuntimeError("Existing Runlog selection is invalid")
    index = directory / "runlog_index.json"
    if not index.exists():
        run(sys.executable, ROOT / "scripts/update_runlog_index.py", directory)


def verify_installed_units():
    for name in SERVICES:
        installed = Path("/etc/systemd/system") / name
        tracked = ROOT / "systemd" / name
        if not installed.is_file() or installed.read_bytes() != tracked.read_bytes():
            raise RuntimeError(f"Installed unit differs from tracked source: {name}")


def stop_for_build(started):
    schema.require_stopped(rpc)
    for name in FRONTENDS:
        running = frontend_running(name)
        if running and not started["frontends"][name]:
            raise RuntimeError(f"Frontend {name} was started after deployment began")
    for name in SERVICES:
        active = active_service(name)
        if active and not started["services"][name]:
            raise RuntimeError(f"Service {name} was started after deployment began")
    for name in SERVICES:
        active = active_service(name)
        if active:
            schema.require_stopped(rpc)
            run("sudo", "-n", "systemctl", "stop", name)
            if active_service(name):
                raise RuntimeError(f"Service did not stop: {name}")
    for name in FRONTENDS:
        running = frontend_running(name)
        if running and not started["frontends"][name]:
            raise RuntimeError(f"Frontend {name} was started during deployment")
        if running:
            schema.require_stopped(rpc)
            result = rpc.call("cm_shutdown", {"name": name})["status"]
            if result != 1:
                raise RuntimeError(f"cm_shutdown({name}) returned {result}")
            wait_frontend(name, False)
    schema.require_stopped(rpc)


def build_and_test():
    env = os.environ.copy()
    env["MIDASSYS"] = "/home/daq/midas/midas_src"
    env["ROOTSYS"] = "/home/daq/root/current"
    run("make", "-C", "vme_frontend", "fevme", env=env)
    run("make", "-C", "easiroc_frontend", "feeasiroc", env=env)
    run("make", "-C", "daq_monitor", "daq_monitor", env=env)
    run("cmake", "-S", "analyzer", "-B", "analyzer/build",
        "-DROOTANA_DIR=/home/daq/midas/rootana",
        "-DROOT_DIR=/home/daq/root/current/cmake",
        "-DCMAKE_BUILD_TYPE=Release", env=env)
    run("cmake", "--build", "analyzer/build", "-j2", env=env)
    run("make", "-C", "vme_frontend", "check", env=env)
    run("make", "-C", "easiroc_frontend", "check", env=env)
    run("make", "-C", "daq_monitor", "check", env=env)
    run("ctest", "--test-dir", "analyzer/build", "--output-on-failure", env=env)
    run(sys.executable, "-m", "unittest", "discover", "-s", "scripts/tests",
        "-p", "test_*.py", env=env)


def restore(started):
    schema.require_stopped(rpc)
    for name in SERVICES:
        if started["services"][name]:
            if active_service(name):
                raise RuntimeError(f"Service {name} started unexpectedly during build")
            run("sudo", "-n", "systemctl", "start", name)
            if not active_service(name):
                raise RuntimeError(f"Service did not start: {name}")
            wait_client("daq_monitor" if name == SERVICES[0] else "ana")
    for name in FRONTENDS:
        if started["frontends"][name]:
            if frontend_running(name):
                raise RuntimeError(f"Frontend {name} started unexpectedly during build")
            result = rpc.call("start_program", {"name": name})["status"]
            if result != 1:
                raise RuntimeError(f"start_program({name}) returned {result}")
            wait_frontend(name, True)
    schema.require_stopped(rpc)


def smoke(commit, started):
    env = os.environ.copy()
    env["DEPLOY_TARGET_SHA"] = commit
    env["DEPLOY_START_FEVME"] = "1" if started["frontends"]["fevme"] else "0"
    env["DEPLOY_START_FEEASIROC"] = "1" if started["frontends"]["feeasiroc"] else "0"
    env["DEPLOY_START_MONITOR"] = "1" if started["services"][SERVICES[0]] else "0"
    env["DEPLOY_START_ANALYZER"] = "1" if started["services"][SERVICES[1]] else "0"
    run(sys.executable, DEPLOY / "deploy4_check_live.py", env=env)


def main():
    if os.geteuid() != __import__("pwd").getpwnam("daq").pw_uid or \
            Path.cwd().resolve() != ROOT:
        raise RuntimeError("deploy2 must run as daq from the live checkout")
    lock_fd = int(os.environ.get("DEPLOY_LOCK_FD", "-1"))
    os.fstat(lock_fd)
    commit = os.environ["DEPLOY_TARGET_SHA"]
    actual = subprocess.check_output(("git", "rev-parse", "HEAD"), cwd=ROOT,
                                     text=True).strip()
    if commit != actual:
        raise RuntimeError("Checkout changed after bootstrap")
    schema.require_live(rpc)
    verify_installed_units()
    old = read_journal()
    if old and old["phase"] == "in_progress":
        started = old["started"]
        print("Resuming prior incomplete deployment state", flush=True)
    else:
        started = started_flags()
    if old and old["phase"] == "complete" and old["commit"] == commit and \
            old.get("binaries") == binary_hashes():
        # A repeated invocation does not disturb running processes or hardware.
        backup_odb(commit)
        run(sys.executable, DEPLOY / "deploy3_configure_live_odb.py")
        ensure_selection()
        smoke(commit, started)
        return
    backup_odb(commit)
    state = {"phase": "in_progress", "commit": commit, "started": started}
    atomic_json(JOURNAL, state)
    stop_for_build(started)
    build_and_test()
    schema.require_stopped(rpc)
    run(sys.executable, DEPLOY / "deploy3_configure_live_odb.py")
    ensure_selection()
    restore(started)
    smoke(commit, started)
    state.update(phase="complete", binaries=binary_hashes())
    atomic_json(JOURNAL, state)
    print(f"Deployment complete: {commit}", flush=True)


try:
    main()
except Exception as error:
    print(f"deploy2: {error}", file=sys.stderr)
    print("Deployment incomplete; preserve the checkout and rerun deploy1 after resolving the error.",
          file=sys.stderr)
    sys.exit(1)
PY
