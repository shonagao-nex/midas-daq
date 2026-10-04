#!/usr/bin/env bash
set -euo pipefail

RUNTIME="/home/nagao/midas/midas"
RUN_DIR="$RUNTIME/dev-run"
source "$(dirname -- "${BASH_SOURCE[0]}")/dev_pid.sh"

if [[ "$(id -un)" != "nagao" ]]; then
  echo "ERROR: dev_stop.sh must be run as user nagao." >&2
  exit 1
fi

# Stop in reverse dependency/order
incomplete=0
dev_pid_stop analyzer "$RUN_DIR/analyzer.pid" /home/nagao/midas/midas/online/analyzer/bin/midas_analyzer || incomplete=1
dev_pid_stop daq_monitor "$RUN_DIR/daq_monitor.pid" /home/nagao/midas/midas/online/daq_monitor/bin/daq_monitor || incomplete=1
dev_pid_stop mlogger "$RUN_DIR/mlogger.pid" /home/nagao/midas/midas_src/bin/mlogger || incomplete=1
dev_pid_stop mhttpd "$RUN_DIR/mhttpd.pid" /home/nagao/midas/midas_src/bin/mhttpd || incomplete=1

echo
if ((incomplete)); then
  echo "Development DAQ stop incomplete; inspect the PID files above." >&2
  exit 1
fi
echo "Development DAQ services stopped."
