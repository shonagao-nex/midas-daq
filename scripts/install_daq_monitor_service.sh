#!/bin/sh
set -eu

source_unit=/home/daq/midas/midas/online/systemd/midas-daq-monitor.service
target_unit=/etc/systemd/system/midas-daq-monitor.service

if [ "$(id -u)" -ne 0 ]; then
  echo "Run this installer as root on the DAQ host" >&2
  exit 1
fi

if [ ! -x /home/daq/midas/midas/online/daq_monitor/bin/daq_monitor ]; then
  echo "Build daq_monitor before installing its service" >&2
  exit 1
fi

install -o root -g root -m 0644 "$source_unit" "$target_unit"
systemctl daemon-reload

echo "Installed $target_unit"
echo "Enable and start with: systemctl enable --now midas-daq-monitor.service"
