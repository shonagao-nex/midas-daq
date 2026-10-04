#!/usr/bin/env bash
set -euo pipefail

ONLINE="/home/nagao/midas/midas/online"
RUNTIME="/home/nagao/midas/midas"
RUN_DIR="$RUNTIME/dev-run"
LOG_DIR="$RUNTIME/dev-log"
source "$ONLINE/scripts/dev_pid.sh"

export MIDASSYS="/home/nagao/midas/midas_src"
export MIDAS_EXPTAB="$RUNTIME/exptab"
export MIDAS_EXPT_NAME="daq-dev"

MHTTPD_PORT=8181
ROOTWEB_PORT=8182

# ----------------------------------------------------------------------
# Safety checks
# ----------------------------------------------------------------------

if [[ "$(id -un)" != "nagao" ]]; then
  echo "ERROR: dev_start.sh must be run as user nagao." >&2
  exit 1
fi

if [[ "$MIDAS_EXPT_NAME" != "daq-dev" ]]; then
  echo "ERROR: MIDAS_EXPT_NAME is not daq-dev." >&2
  exit 1
fi

if [[ "$MIDAS_EXPTAB" != "/home/nagao/midas/midas/exptab" ]]; then
  echo "ERROR: unexpected MIDAS_EXPTAB: $MIDAS_EXPTAB" >&2
  exit 1
fi

mkdir -p "$RUN_DIR" "$LOG_DIR"

# Start a development service and record the process that actually execed.
start_process()
{
  local name=$1
  shift
  local file="$RUN_DIR/$name.pid" log="$LOG_DIR/$name.log" pid old

  if [[ -f $file ]]; then
    IFS=$'\t' read -r old _ < "$file" || old=unknown
    if dev_pid_matches "$file" "$1"; then
      echo "$name already running (PID $old)"
      return
    fi
    echo "$name: stale PID file ($old)"
    if dev_pid_same_exe "$old" "$1"; then
      echo "ERROR: $name cannot be authenticated; same executable is running (PID $old)." >&2
      exit 1
    fi
    rm -f -- "$file"
  fi

  echo "Starting $name..."
  "$@" >>"$log" 2>&1 &
  pid=$!
  sleep 0.5
  if ! dev_pid_record "$file" "$pid" "$1"; then
    echo "ERROR: $name failed to start or did not exec $1." >&2
    echo "See: $log" >&2
    exit 1
  fi
  echo "  PID $pid"
}

# ----------------------------------------------------------------------
# Start MIDAS development services
# ----------------------------------------------------------------------

cd "$ONLINE"

start_process mhttpd \
    "$MIDASSYS/bin/mhttpd" \
    -e "$MIDAS_EXPT_NAME" \
    --no-passwords

start_process mlogger \
    "$MIDASSYS/bin/mlogger" \
    -e "$MIDAS_EXPT_NAME"

start_process daq_monitor \
    "$ONLINE/daq_monitor/bin/daq_monitor" \
    -e "$MIDAS_EXPT_NAME"

start_process analyzer \
    "$ONLINE/analyzer/bin/midas_analyzer" \
    --no-profiler \
    -R"$ROOTWEB_PORT"

echo
echo "Development DAQ services started."
echo
echo "  Experiment : $MIDAS_EXPT_NAME"
echo "  MIDAS GUI  : http://133.11.162.51:$MHTTPD_PORT/"
echo "  ROOTWeb    : http://133.11.162.51:$ROOTWEB_PORT/"
echo
echo "Logs:"
echo "  $LOG_DIR"
echo
echo "Frontends are NOT started."
