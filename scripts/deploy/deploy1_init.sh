#!/usr/bin/env bash
set -euo pipefail

ROOT=/home/daq/midas/midas/online
RUNTIME=/home/daq/midas/midas
EXPECTED_HOST=nexdaq1

case ${1:-} in
    ""|--check|--stable) ;;
    *) echo "Usage: $0 [--check]" >&2; exit 2 ;;
esac

if [[ $(id -un) != daq || $HOME != /home/daq || $(hostname -s) != "$EXPECTED_HOST" ||
      $(pwd -P) != "$ROOT" ]]; then
    echo "deploy1: run as daq on $EXPECTED_HOST from $ROOT with HOME=/home/daq" >&2
    exit 1
fi

if [[ ${1:-} == --check ]]; then
    export PYTHONDONTWRITEBYTECODE=1
    exec python3 "$ROOT/scripts/deploy/deploy3_configure_live_odb.py" --check
fi

if [[ ${1:-} != --stable ]]; then
    # The copy remains immutable while git updates this checkout.
    temp_dir=$(mktemp -d "$RUNTIME/deploy-bootstrap.XXXXXXXX")
    chmod 700 "$temp_dir"
    cp -- "$ROOT/scripts/deploy/deploy1_init.sh" "$temp_dir/bootstrap.sh"
    chmod 700 "$temp_dir/bootstrap.sh"
    exec "$temp_dir/bootstrap.sh" --stable "$temp_dir"
fi

temp_dir=${2:?stable bootstrap directory required}
[[ $temp_dir == "$RUNTIME"/deploy-bootstrap.* && -f $temp_dir/bootstrap.sh ]] || {
    echo "deploy1: invalid bootstrap copy" >&2
    exit 1
}
trap 'rm -f -- "$temp_dir/bootstrap.sh"; rmdir -- "$temp_dir"' EXIT

exec 9>"$RUNTIME/deploy.lock"
if ! flock -n 9; then
    echo "deploy1: another deployment is in progress" >&2
    exit 1
fi
export DEPLOY_LOCK_FD=9

if [[ $(git -C "$ROOT" rev-parse --abbrev-ref HEAD) != main ]]; then
    echo "deploy1: live checkout must be on main" >&2
    exit 1
fi
if ! git -C "$ROOT" diff --quiet || ! git -C "$ROOT" diff --cached --quiet; then
    echo "deploy1: tracked local changes prevent a safe fast-forward update; inspect these files (no reset/stash is performed):" >&2
    git -C "$ROOT" status --short --untracked-files=no >&2
    git -C "$ROOT" diff --stat >&2
    git -C "$ROOT" diff --cached --stat >&2
    exit 1
fi

# All access is through the existing mhttpd. No local live ODB client is started.
frontend_flags=$(python3 - <<'PY'
import json
import urllib.request

def rpc(method, params):
    req = urllib.request.Request(
        "http://127.0.0.1:8081/?mjsonrpc",
        json.dumps({"jsonrpc": "2.0", "method": method,
                    "params": params, "id": 1}).encode(),
        {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=8) as response:
        answer = json.load(response)
    if "error" in answer:
        raise RuntimeError(answer["error"])
    return answer["result"]

paths = ["/Experiment/Name", "/Runinfo/State", "/Runinfo/Transition in progress"]
result = rpc("db_get_values", {"paths": paths})
if result["status"] != [1, 1, 1] or result["data"] != ["daq", 1, 0]:
    raise RuntimeError("port 8081 is not STOPPED experiment daq")
flags = []
for name in ("fevme", "feeasiroc", "fehul"):
    status = rpc("cm_exist", {"name": name, "unique": True})["status"]
    if status not in (1, 103):
        raise RuntimeError(f"cm_exist({name}) returned {status}")
    flags.append("1" if status == 1 else "0")
print(" ".join(flags))
PY
)
read -r DEPLOY_START_FEVME DEPLOY_START_FEEASIROC DEPLOY_START_FEHUL <<<"$frontend_flags"
export DEPLOY_START_FEVME DEPLOY_START_FEEASIROC DEPLOY_START_FEHUL

service_flag() {
    local name=$1
    local state
    state=$(systemctl show --property=ActiveState --value "$name")
    case $state in
        active) echo 1 ;;
        inactive) echo 0 ;;
        *) echo "deploy1: unexpected $name state: $state" >&2; return 1 ;;
    esac
}
DEPLOY_START_MONITOR=$(service_flag midas-daq-monitor.service)
DEPLOY_START_ANALYZER=$(service_flag midas-analyzer.service)
export DEPLOY_START_MONITOR DEPLOY_START_ANALYZER
[[ $(service_flag midas-mhttpd.service) == 1 &&
   $(service_flag midas-mlogger.service) == 1 ]] || {
    echo "deploy1: mhttpd and mlogger must be active" >&2
    exit 1
}

before=$(git -C "$ROOT" rev-parse HEAD)
# Resolve and display the exact incoming tree before changing served files.
# Fetch + merge --ff-only preserves the existing fast-forward-only update policy.
git -C "$ROOT" fetch origin main
incoming=$(git -C "$ROOT" rev-parse FETCH_HEAD)
echo "deploy1: incoming tracked files ($before -> $incoming):"
git -C "$ROOT" diff --name-status "$before" "$incoming"
echo "deploy1: services/frontend state above will be restored; new HUL will remain stopped"
git -C "$ROOT" merge --ff-only "$incoming"
DEPLOY_TARGET_SHA=$(git -C "$ROOT" rev-parse HEAD)
export DEPLOY_TARGET_SHA
echo "deploy1: $before -> $DEPLOY_TARGET_SHA"

[[ -x "$ROOT/scripts/deploy/deploy2_impl.sh" ]] || {
    echo "deploy1: updated deploy2_impl.sh is missing or not executable" >&2
    exit 1
}
"$ROOT/scripts/deploy/deploy2_impl.sh"
