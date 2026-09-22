#!/bin/sh
set -eu

root=/home/daq/midas/midas/online
unit="$root/systemd/midas-daq-monitor.service"
installer="$root/scripts/install_daq_monitor_service.sh"

check_line() {
  expected=$1
  if ! grep -Fqx "$expected" "$unit"; then
    echo "missing expected unit setting: $expected" >&2
    exit 1
  fi
}

check_line 'User=daq'
check_line 'Group=daq'
check_line 'WorkingDirectory=/home/daq/midas/midas/online'
check_line 'Environment=MIDASSYS=/home/daq/midas/midas_src'
check_line 'Environment=MIDAS_EXPTAB=/home/daq/midas/midas/exptab'
check_line 'Environment=MIDAS_EXPT_NAME=daq'
check_line 'UnsetEnvironment=MIDAS_SERVER_HOST MIDAS_SERVER_PORT MIDAS_DIR'
check_line 'ExecStart=/home/daq/midas/midas/online/daq_monitor/bin/daq_monitor -e daq'
check_line 'Restart=on-failure'
check_line 'RestartSec=5'
check_line 'WantedBy=multi-user.target'

sh -n "$installer"
echo "daq_monitor systemd static checks passed"
