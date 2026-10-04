#!/usr/bin/env bash
set -euo pipefail

source "$(dirname -- "${BASH_SOURCE[0]}")/../dev_pid.sh"

dir=$(mktemp -d /tmp/midas-dev-pid-test.XXXXXXXX)
exe=$(readlink -f -- "$(command -v sleep)")
first= second= third=
cleanup() {
  for pid in "$first" "$second" "$third"; do
    if [[ -n $pid ]]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi
  done
  rm -rf -- "$dir"
}
trap cleanup EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

# A recorded process matches and can be stopped without touching another one.
sleep 30 & first=$!
dev_pid_record "$dir/normal.pid" "$first" "$exe" || fail "record normal PID"
dev_pid_matches "$dir/normal.pid" "$exe" || fail "normal PID did not match"
IFS=$'\t' read -r pid start path hash < "$dir/normal.pid"
printf '%s\t%s\t%s\t%s\n' "$pid" "$((start + 1))" "$path" "$hash" > "$dir/normal.pid"
dev_pid_matches "$dir/normal.pid" "$exe" && fail "changed start time matched"
dev_pid_record "$dir/normal.pid" "$first" "$exe" || fail "restore normal PID record"
IFS=$'\t' read -r pid start path hash < "$dir/normal.pid"
printf '%s\t%s\t%s\t%064d\n' "$pid" "$start" "$path" 0 > "$dir/normal.pid"
dev_pid_matches "$dir/normal.pid" "$exe" && fail "changed command line hash matched"
dev_pid_record "$dir/normal.pid" "$first" "$exe" || fail "restore normal PID record"
dev_pid_stop normal "$dir/normal.pid" "$exe" >/dev/null
[[ ! -e $dir/normal.pid ]] || fail "normal PID file remains"
wait "$first" 2>/dev/null || true
first=

# A missing PID is stale, and the file is removed without sending a signal.
sleep 30 & second=$!
dev_pid_record "$dir/missing.pid" "$second" "$exe" || fail "record missing PID case"
IFS=$'\t' read -r pid start path hash < "$dir/missing.pid"
printf '99999999\t%s\t%s\t%s\n' "$start" "$path" "$hash" > "$dir/missing.pid"
dev_pid_matches "$dir/missing.pid" "$exe" && fail "missing PID matched"
dev_pid_stop missing "$dir/missing.pid" "$exe" >/dev/null
kill -0 "$second" || fail "unrelated process was stopped for missing PID"

# Replacing the saved PID with another live process must not stop that process.
sleep 30 & third=$!
dev_pid_record "$dir/other.pid" "$second" "$exe" || fail "record other PID case"
IFS=$'\t' read -r pid start path hash < "$dir/other.pid"
printf '%s\t%s\t%s\t%s\n' "$third" "$start" "$path" "$hash" > "$dir/other.pid"
dev_pid_matches "$dir/other.pid" "$exe" && fail "different process matched"
dev_pid_stop other "$dir/other.pid" "$exe" >/dev/null && fail "different live process was accepted"
kill -0 "$second" && kill -0 "$third" || fail "unrelated process was stopped"
[[ -e $dir/other.pid ]] || fail "unauthenticated live PID file was removed"

# A dead process and a legacy one-field PID file are both unauthenticated.
dev_pid_record "$dir/stale.pid" "$second" "$exe" || fail "record stale PID case"
kill "$second"
wait "$second" 2>/dev/null || true
second=
dev_pid_stop stale "$dir/stale.pid" "$exe" >/dev/null
[[ ! -e $dir/stale.pid ]] || fail "stale PID file remains"
printf '%s\n' "$third" > "$dir/legacy.pid"
dev_pid_stop legacy "$dir/legacy.pid" "$exe" >/dev/null && fail "legacy live PID was accepted"
kill -0 "$third" || fail "legacy PID file stopped another process"
[[ -e $dir/legacy.pid ]] || fail "legacy live PID file was removed"

echo "Development PID tests passed"
