# MIDAS DAQ development

This repository contains the VME and NIM-EASIROC frontends, `daq_monitor`, a
shared analyzer, and MIDAS Custom pages. Start here, then use the component
documents for details:

| Component | Entry point |
|---|---|
| Analyzer, online histograms, Pages, offline ROOT output | [analyzer/README.md](analyzer/README.md) |
| VME frontend | [vme_frontend/fevme.cxx](vme_frontend/fevme.cxx), [vme_frontend/Makefile](vme_frontend/Makefile) |
| NIM-EASIROC frontend | [easiroc_frontend/docs/easiroc_protocol.md](easiroc_frontend/docs/easiroc_protocol.md), [slow control](easiroc_frontend/docs/easiroc_slow_control.md) |
| DAQ monitor and Custom pages | [daq_monitor/daq_monitor.cxx](daq_monitor/daq_monitor.cxx), [web/README.md](daq_monitor/web/README.md) |
| Live deployment procedure | [scripts/deploy/README.md](scripts/deploy/README.md) |
| Read-only ODB settings comparison | [scripts/README-odb-diff.md](scripts/README-odb-diff.md) |

## Development services

### V1720E TRG-OUT

At `fevme` initialization, when V1720E is enabled in ODB, the frontend routes
only External TRG-IN to TRG-OUT. Register `0x8110` enables bit 30 and clears
software bit 31 and channel bits 7:0; other bits are preserved. Register
`0x811C` clears bit 15 (normal operation), bit 1 (panel output enabled), and
bit 0 (NIM); all other bits are preserved. Each D32 write is read back and a
failure prevents frontend initialization. The raw values and verification flag
are recorded under `/Equipment/VME/Readback/V1720E` as `TriggerOutMaskRaw`,
`FrontPanelIoControlRaw`, and `TriggerOutValid`. This is fixed frontend behavior,
so no adjustable ODB setting is added. The separate `0x810C` acquisition
trigger source and V3718 OUT0 Global BUSY configuration are unaffected.
BOR/EOR and STOP do not write either TRG-OUT register.

The [CAEN V1720 register description (2013-04-22)](https://wwwusers.ts.infn.it/~rui/univ/Acquisizione_Dati/Manuals/CAEN%20V1720%20Registers%20Description.pdf)
§2.22 and §2.25 define these fields; §2.27 defines ROC FPGA revision at
`0x8124`, already read during board probing. CAEN warns that the document may
not fully apply above ROC FPGA 3.8. The existing code was verified with 4.5,
but TRG-OUT behavior with that firmware, especially while STOPPED, requires
hardware measurement. With a suitable NIM scope and known TRG-IN pulse,
measure TRG-OUT before RUN, during RUN, and after STOP; compare pulse presence,
edge delay, and width. Also confirm software and channel self triggers produce
no TRG-OUT pulse. This measurement has not been performed by the software tests.

Work in `/home/nagao/midas/midas/online` with experiment `daq-dev` from
`/home/nagao/midas/midas/exptab`. Build the clients before starting them:

```sh
cd /home/nagao/midas/midas/online
make -C vme_frontend fevme
make -C easiroc_frontend feeasiroc
make -C daq_monitor daq_monitor
cmake -S analyzer -B analyzer/build \
  -DMIDASSYS=/home/nagao/midas/midas_src \
  -DROOTANA_DIR=/home/nagao/midas/rootana
cmake --build analyzer/build -j2
./scripts/dev_start.sh
```

`dev_start.sh` starts development `mhttpd`, `mlogger`, `daq_monitor`, and the
online analyzer in that order. It does **not** start either frontend or a run.
Use `./scripts/dev_stop.sh` to stop those four services. The scripts authenticate
their PID files before treating a process as theirs; inspect an error instead
of killing an unrelated PID. Logs are in `/home/nagao/midas/midas/dev-log/`.

The development MIDAS GUI is on port **8181**; the development analyzer ROOT
Web server is on **8182**. Both use `daq-dev`. The analyzer's standalone default
ROOT Web port is 8082, so `dev_start.sh` explicitly passes `-R8182`. Frontends
can be started individually from the development Programs page when configured.
The development scripts do not manage them.

## What each operation changes

- Builds, `make -C vme_frontend check`, `make -C easiroc_frontend check`,
  `make -C daq_monitor check`, and `ctest --test-dir analyzer/build` are local
  software checks. The DAQ, EASIROC, and buffer-clear pages with `?mock=1`
  use browser data and do not contact MIDAS. Analyzer `-f INPUT` reads recorded
  data and writes a ROOT file; it does not contact ODB or hardware.
- `dev_start.sh` and `dev_stop.sh` connect or disconnect development MIDAS
  clients. Online monitoring reads ODB and publishes status; the logger and
  monitor may write development logs, Runlogs, index files, and ODB status.
- Frontend startup, run Start/Stop, the EASIROC Apply command, and manual
  buffer clear can communicate with hardware. Web Save writes ODB settings;
  EASIROC hardware Apply remains a separate action. Analyzer
  `--init-hist-odb` and the `configure_dev_*.py` scripts change development
  ODB configuration.
- `scripts/archive_midas_data.sh` copies DAQ data to a mounted archive. It is
  not part of development startup or the deployment pipeline; do not run it as
  a read-only check.

Use the component documents above for command options and test scope. None of
these development instructions applies to `/home/daq`; live deployment is a
separate operation.

## MIDAS Programs and monitor service

The checked-in Programs definitions are in `scripts/frontend_programs.odb`.
`fevme` and `feeasiroc` are individually selectable while STOPPED. Their
`Required` flags keep them visible, while Auto start, Auto stop, and Auto
restart are false. The intended configuration keeps
`/Experiment/Prevent start on required progs` false, allowing VME-only,
EASIROC-only, or combined runs. Run Stop leaves frontend processes connected.
`scripts/install_frontend_programs.sh` changes the live ODB and is for the
actual DAQ host only. Its wrappers set the local experiment explicitly;
Programs Stop uses MIDAS `cm_shutdown()`.

`systemd/midas-daq-monitor.service` is the live monitor unit. The live install
helper `scripts/install_daq_monitor_service.sh` and the service commands in
the deployment documentation change host state; they are not development
startup commands. The dashboard's buffer-clear page submits ODB request IDs
to frontends and can trigger hardware work while STOPPED.
