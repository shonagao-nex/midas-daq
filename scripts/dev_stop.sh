#!/usr/bin/env bash
set -euo pipefail

RUNTIME="/home/nagao/midas/midas"
RUN_DIR="$RUNTIME/dev-run"

if [[ "$(id -un)" != "nagao" ]]; then
    echo "ERROR: dev_stop.sh must be run as user nagao." >&2
    exit 1
fi

stop_process()
{
    local name="$1"
    local pidfile="$RUN_DIR/$name.pid"

    if [[ ! -f "$pidfile" ]]; then
        echo "$name: not running"
        return
    fi

    local pid
    pid="$(cat "$pidfile")"

    if ! kill -0 "$pid" 2>/dev/null; then
        echo "$name: stale PID file ($pid)"
        rm -f "$pidfile"
        return
    fi

    echo "Stopping $name (PID $pid)..."
    kill "$pid"

    for _ in {1..20}; do
        if ! kill -0 "$pid" 2>/dev/null; then
            rm -f "$pidfile"
            echo "  stopped"
            return
        fi
        sleep 0.25
    done

    echo "  graceful stop timed out; sending SIGKILL"
    kill -KILL "$pid" 2>/dev/null || true
    rm -f "$pidfile"
}

# Stop in reverse dependency/order
stop_process analyzer
stop_process daq_monitor
stop_process mlogger
stop_process mhttpd

echo
echo "Development DAQ services stopped."
