# Production support-service controls

These scripts manage only mhttpd, mlogger, daq_monitor, and the online
analyzer for the `daq` experiment. Run them as root from the **actual DAQ
host shell** with the host PID and mount namespaces. They never change a
MIDAS run, frontend, or hardware.

## Install before use

The checked-in drop-in must be copied under **each** installed unit. Perform
this during an approved maintenance window on the DAQ host:

```sh
for unit in midas-mhttpd midas-mlogger midas-daq-monitor midas-analyzer; do
  sudo install -d -m 0755 "/etc/systemd/system/$unit.service.d"
  sudo install -m 0644 /home/daq/midas/midas/online/systemd/dropins/midas-daq-no-sigkill.conf \
    "/etc/systemd/system/$unit.service.d/no-sigkill.conf"
done
sudo systemctl daemon-reload
for unit in midas-mhttpd midas-mlogger midas-daq-monitor midas-analyzer; do
  systemctl show "$unit.service" -p SendSIGKILL -p KillMode -p TimeoutStopUSec
done
```

All four must report `SendSIGKILL=no` before start, stop, or recover is
allowed. The scripts do not install the drop-in. Installing and reloading
does not itself stop a service. The current units have `TimeoutStopSec=30s`;
with this drop-in, an unresponsive process may remain in its cgroup after
the stop job fails. Investigate it before another start.

`prod_status.sh` is read-only and can run in a sandbox, but host PIDs will
appear UNKNOWN there. The other scripts refuse to run outside the host PID
namespace. `prod_stop.sh` requires STOPPED with no transition, and refuses
to use SIGKILL. `prod_recover.sh` requires an exact interactive `RECOVER`
confirmation when the run is not definitely STOPPED. It may interrupt data
logging and leave EOR or Runlog data incomplete. It never cleans ODB clients
or shared memory. A stale client after restart is an explicit failure for
manual investigation.

The scripts use the MIDAS client names `mhttpd`, `Logger`,
`daq_monitor`, and `ana`. They read ODB through the existing mhttpd
JSON-RPC endpoint on 8081. A stopped mhttpd makes ODB status unavailable
until it is started; startup checks ODB immediately after starting it.
