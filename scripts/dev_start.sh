#!/usr/bin/env bash
set -euo pipefail

ONLINE="/home/nagao/midas/midas/online"
RUNTIME="/home/nagao/midas/midas"
RUN_DIR="$RUNTIME/dev-run"
LOG_DIR="$RUNTIME/dev-log"

export MIDASSYS="/home/nagao/midas/midas_src"
export MIDAS_EXPTAB="$RUNTIME/exptab"
export MIDAS_EXPT_NAME="daq-dev"

MHTTPD_PORT=8181
ROOTWEB_PORT=8182

mkdir -p "$RUN_DIR" "$LOG_DIR"

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

start_process()
{
    local name="$1"
    shift

    local pidfile="$RUN_DIR/$name.pid"
    local logfile="$LOG_DIR/$name.log"

    if [[ -f "$pidfile" ]]; then
        local oldpid
        oldpid="$(cat "$pidfile")"

        if kill -0 "$oldpid" 2>/dev/null; then
            echo "$name already running (PID $oldpid)"
            return
        fi

        rm -f "$pidfile"
    fi

    echo "Starting $name..."

    "$@" >>"$logfile" 2>&1 &
    local pid=$!

    echo "$pid" > "$pidfile"

    sleep 0.5

    if ! kill -0 "$pid" 2>/dev/null; then
        echo "ERROR: $name failed to start." >&2
        echo "See: $logfile" >&2
        rm -f "$pidfile"
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
    "$ONLINE/analyzer/build/midas_analyzer" \
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
