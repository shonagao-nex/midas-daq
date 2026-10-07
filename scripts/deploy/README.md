# Live deployment procedure

This procedure is for the actual DAQ host shell as user `daq`. It changes the
live checkout, services, and selected ODB configuration. Do not run it from the
development sandbox. Finish development and tests under `/home/nagao`, commit
and push the intended changes, then schedule a maintenance window with the run
STOPPED and no Run Start during deployment.

Runlog `EventSlipCount` uses `INT64` so `-1` represents N/A. Step 3 converts
only an existing `UINT64` Runlog slip key to `INT64` while STOPPED, preserving
nonnegative values that fit in `INT64`. An out-of-range value or any other
unexpected type stops deployment before conversion.

From the live checkout, the normal update is:

```sh
cd /home/daq/midas/midas/online
./scripts/deploy/deploy1_init.sh
```

Check its completion message and the read-only step 4 smoke test before
resuming operation. The script requires user `daq`, host `nexdaq1`, branch
`main`, a clean tracked working tree, and experiment `daq` STOPPED with no
transition on mhttpd port 8081. It also requires the existing MIDAS, ROOT,
ROOTANA, Runlog directory, `exptab`, and four installed systemd units. The
installed monitor and analyzer units must match their tracked files. The `daq`
user needs passwordless permission to stop/start only those two services.
Deployment does not install or update units.

If this is the first deployment of these scripts and the live checkout lacks
them, fetch the committed bootstrap once from the actual host shell with
`git pull --ff-only origin main` after checking for tracked local changes.
Later updates use `deploy1_init.sh`.

## One-time analyzer unit migration

If an older installed analyzer unit still points to
`analyzer/build/midas_analyzer`, the unit comparison in step 2 stops before the
build. During a STOPPED maintenance window, prepare the new executable and
install the tracked unit before running step 1:

```sh
cd /home/daq/midas/midas/online
export MIDASSYS=/home/daq/midas/midas_src ROOTSYS=/home/daq/root/current
cmake -S analyzer -B analyzer/build \
  -DROOTANA_DIR=/home/daq/midas/rootana \
  -DROOT_DIR=/home/daq/root/current/cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build analyzer/build -j2
test -x analyzer/bin/midas_analyzer
sudo install -m 0644 systemd/midas-analyzer.service \
  /etc/systemd/system/midas-analyzer.service
sudo systemctl daemon-reload
./scripts/deploy/deploy1_init.sh
```

Installing the unit and reloading systemd do not restart a running analyzer.
Step 1 builds and tests again, then restores only processes that were running
when deployment began. Do not remove the recovery journal or revert the unit
as a way around a failed unit comparison.

## Steps and effects

- `deploy1_init.sh` checks identity, checkout, run state, and initial
  frontend/service state. It holds an exclusive lock, copies its bootstrap,
  pulls `origin/main` with `--ff-only`, then calls step 2. It does not clean
  untracked files; Git stops if one conflicts with a tracked incoming path.
- `deploy2_impl.sh` backs up ODB to a unique file and writes a recovery
  journal. It stops only the frontends and monitor/analyzer services that were
  running when needed, builds and runs unit tests, calls step 3, prepares
  missing derived Runlog files, restores the original process state, and calls
  step 4. A completed deployment of the same commit and binaries does not
  restart processes again.
- `deploy3_configure_live_odb.py` validates DAQ settings schema before changing
  its owned fixed schema, Runlog BOR/EOR links and order, Programs, and Custom
  entries. It does not load an entire ODB backup. Unexpected schema, links,
  types, or paths stop deployment.
- `deploy4_check_live.py` checks the Git commit, binaries, fixed ODB schema,
  Programs, Custom pages, Runlog/ELOG resources, service/frontend state, and
  port 8081 without writing ODB or starting a run. Its checks read live state,
  so run it only from the actual host shell.

`scripts/archive_midas_data.sh` is a **tracked** script in this repository.
It is separate from these four deployment steps; executing it copies data to
the mounted archive and is not a smoke test. Deployment does not run it or
delete untracked files. Neither `git reset --hard` nor `git clean` is used.

## Failure and recovery

Keep Run Start disabled operationally while resolving a failure. ODB backups
are written below `/home/daq/midas/midas/backups/` as
`odb-before-deploy-*.odb`; the recovery journal is
`/home/daq/midas/midas/deploy-state.json`. Correct the reported cause and
rerun `deploy1_init.sh`. The journal retains the original process state; do
not delete it manually. No automatic full-ODB restore is performed.

The scripts recheck STOPPED before change stages, but they cannot atomically
block every MIDAS Run Start path. A detected race stops deployment.

Existing Run Number, Run Parameters, ELOG history, hardware settings, Analyzer
Histogram/Page settings, DAQ data, Runlogs, ELOG files, `runlog_selection.json`,
and unrelated Programs/Custom entries are preserved. Missing history keys may
be initialized to zero or empty values. `/Custom/Path` and the ELOG web port
are verified against the intended live values rather than overwritten.

For targeted diagnosis on the actual host, check STOPPED first:

```sh
cd /home/daq/midas/midas/online
python3 scripts/deploy/deploy4_check_live.py        # read-only smoke check
python3 scripts/deploy/deploy3_configure_live_odb.py # changes owned ODB keys
```

Do not run step 2 alone; it needs the bootstrap lock and initial process state.
Static syntax checks, which do not connect to ODB, are:

```sh
bash -n scripts/deploy/deploy1_init.sh scripts/deploy/deploy2_impl.sh
python3 -m py_compile scripts/deploy/deploy3_configure_live_odb.py scripts/deploy/deploy4_check_live.py
```

## ODB settings scope and comparison

Steps 3 and 4 validate the existence, type, scalar/array length, and required
STRING capacity of VME/EASIROC Settings and existing Analyzer settings. A
schema error stops deployment with the affected path and expected/actual
schema. Deployment does not copy development setting values, create missing
Settings, repair schema, or change existing setting values. New ODB
initialization is a separate operation. Frontends and the analyzer validate
the range, meaning, and combinations of values when they use them.

Existing custom Analyzer Histogram groups and Pages are allowed; deployment
does not require the default layout. An absent Analyzer tree retains the
analyzer's in-memory fallback, and absent Pages retain default pages. Missing
Pad fields remain empty pads. The deploy-owned `Programs`, Runlog JSON, and
Custom values listed in `MANAGED_VALUES` retain their existing policy. Compare
development and production setting values with the read-only
[`odb_settings_diff.py`](../README-odb-diff.md) from the actual DAQ host shell.
