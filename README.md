# midaq-daq

## MIDAS Programs

The standard MIDAS Programs page manages the two detector frontends as
`fevme` and `feeasiroc`. Both entries have `Required` set to true so they remain
visible and can be started individually from the Programs page while stopped.
`Auto start`, `Auto stop`, and `Auto restart` remain false, so neither frontend
is started, stopped, or restarted automatically.

The live experiment keeps `/Experiment/Prevent start on required progs` set to
false. With that setting, `Required` controls Programs-page visibility without
making either frontend mandatory for Run Start. The supported configurations
are therefore:

- VME only: run `fevme` while `feeasiroc` remains stopped.
- EASIROC only: run `feeasiroc` while `fevme` remains stopped.
- Both: run `fevme` and `feeasiroc` together.

Run Stop does not automatically stop either frontend.

The checked-in definitions are in `scripts/frontend_programs.odb`. On the DAQ
host, load them once while the local `daq` experiment is available:

```sh
/home/daq/midas/midas/online/scripts/install_frontend_programs.sh
```

This installer changes the live ODB, so it must not be run from a sandbox.
After loading it, use the MIDAS Programs page to start or stop each frontend.
Programs Stop uses the standard MIDAS `cm_shutdown()` request; there is no
custom kill command.

The start wrappers set the local experiment environment explicitly and remove
remote-server variables before executing the absolute frontend paths. Neither
frontend currently reads runtime files through relative paths, so the wrappers
do not depend on or change the inherited working directory.

The DAQ custom page links to `buffer-clear.html`. It submits monotonic ODB
request IDs to the running frontend and shows per-module acknowledgement. Both
the browser and frontend enforce STOPPED; frontend startup acknowledges but
does not execute a request left behind while it was absent.

## daq_monitor service

`systemd/midas-daq-monitor.service` follows the installed mhttpd/mlogger unit
layout: it runs as `daq:daq` from this repository, uses the local `daq`
experiment environment, and restarts after failures with a five-second delay.
The monitor itself rejects a second MIDAS registration whose actual client
name is not exactly `daq_monitor`, while systemd owns a single service process.

After building `daq_monitor`, install the unit from the real DAQ host (not a
sandbox), then enable and start it:

```sh
sudo /home/daq/midas/midas/online/scripts/install_daq_monitor_service.sh
sudo systemctl enable --now midas-daq-monitor.service
```

Run Stop does not affect this service. To inspect it without changing state,
use `systemctl status midas-daq-monitor.service` and
`journalctl -u midas-daq-monitor.service`.
