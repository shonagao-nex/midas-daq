# DAQ general-user Custom page

For the development experiment, startup commands, and ports, start with the
[repository README](../../README.md). This document covers Custom-page data
sources and behavior.

## Run Summary (development)

`run-summary.html` is registered as `/Custom/Run Summary` in the development
ODB on port 8181. It reads `/runlogs/runlog_index.json`, applies
`/runlogs/runlog_selection.json`, and requests 30 listed JSON Runlog
files per page through mhttpd, with four requests in flight. The `runlogs`
symlink in this directory points to `$HOME/midas/midas/runlogs` relative to
the common repository layout. It does
not probe missing run numbers or modify Runlogs.
The `experiment_label` field in each Runlog's BOR section records
`/Experiment/Run Parameters/ExperimentLabel` when Logger starts that run.
Run Summary and its CSV export read this saved value, so later GUI edits do
not change older runs. Older Runlogs without the field show an empty value.

`scripts/update_runlog_index.py` rescans the Runlog directory and atomically
replaces `runlog_index.json`. `daq_monitor` launches it after a completed STOP,
outside the transition. It can also be run manually with the Runlog directory
as its argument, for example after removing or recovering files. A failed
refresh leaves the previous index intact and cannot fail STOP. The monitor
marks a run indexed only after the child exits successfully; failed launches
or child exits are retried after a five-second delay while it remains running.
The index is derived data; never edit the standard MIDAS JSON Runlogs to change
the list.
The monitor finds the index script relative to its own `bin/daq_monitor`
executable, so deployments need both the binary and `scripts/` in the same
repository layout. Restart the monitor to activate a rebuilt binary;
rebuilding alone does not replace the running process. Each environment keeps
its own Runlog directory and index; the relative `web/runlogs` link works in
both layouts.

Keep `runlog_selection.json` in the same directory. Its default content is
`{"runs":[]}`: an empty array selects every indexed run. To filter, edit it
manually to e.g. `{"runs":[63,61,60]}`. The index generator never changes
this file. Unknown runs are ignored, duplicates are removed, and the result
is sorted newest first. Invalid or unreadable selection falls back to the
full index with a page warning. Deploy the default file before opening the
page so normal operation never requests a missing selection resource.

For the shared user layout, set `/Logger/Message dir` to
`$HOME/midas/midas/log` and `/Logger/Runlog/JSON/Subdir` to `../runlogs`.
MIDAS then writes JSON files in `$HOME/midas/midas/runlogs`; the Web symlink
serves that same directory. Keep `/Logger/Data dir` and `/Logger/Elog dir`
pointed at their separate `data` and `elog` directories.

MIDAS BOR-only files can end with a comma until Logger appends EOR. The page
displays these as `INCOMPLETE`. Scaler details show only channels present in
the EOR object. Unknown additional Runlog fields are ignored.

`Export CSV` reads the same selected run list as the table, in batches of 50
with four concurrent requests. It uses the same Runlog parser and normalized
values as the table, skips files removed after indexing, and
includes BOR-only runs. The download has a UTF-8 BOM for spreadsheet programs,
quotes CSV fields containing commas, line breaks, or double quotes, and writes
only recorded scaler channels into the 64 `Scaler00`–`Scaler63` columns.

`daq.html` is the general-user MIDAS dashboard. It reads status from
`/DAQ/Status` and the standard MIDAS equipment statistics records. Start and
Stop use the existing MIDAS transitions. Settings are stored in ODB; the page
never writes hardware directly.
The Run settings use `/Experiment/Run Parameters/ExperimentLabel` (a 1024-byte
ODB STRING), alongside the existing `Type` and `Comment` keys. The Experiment
field is editable while STOPPED and is separate from `/Experiment/Name`.

## Offline preview

Open the following file in a browser with the query parameter shown:

```text
daq.html?mock=1
```

This uses `mock-data.js`, does not contact MIDAS, and disables both transition
buttons. Its scenario buttons cover STOPPED + WARNING, RUNNING + OK, and
RUNNING + ERROR. Mock Enable switches change only the in-browser mock object.
Missing `midas.css`, `midas.js`, or `mhttpd.js` warnings are harmless when the
file is opened directly; those resources are supplied by mhttpd in live use.

## Live installation

Do not replace `/Custom/Status`. Keep the standard Status page and register
this page as `/Custom/DAQ`.

If `/Custom/Path` is not already used by the experiment, the intended ODB
values are:

```text
/Custom/Path = /home/daq/midas/midas/online/daq_monitor/web
/Custom/DAQ  = daq.html
```

They can be created in an interactive `odbedit` session:

```text
create STRING /Custom/Path
set /Custom/Path /home/daq/midas/midas/online/daq_monitor/web
create STRING /Custom/DAQ
set /Custom/DAQ daq.html
```

If `/Custom/Path` already points elsewhere, do not overwrite it. Copy or
symlink `daq.html`, `daq.css`, `daq.js`, `mock-data.js`, `easiroc.html`,
`easiroc.css`, `easiroc.js`, `easiroc-mock-data.js`, `buffer-clear.html`,
`buffer-clear.css`, `buffer-clear.js`, and `buffer-clear-mock-data.js` into
that existing directory, then set only `/Custom/DAQ` to `daq.html`.

After registration the page is available from the `DAQ` side-menu entry or:

```text
http://<mhttpd-host>:<port>/?cmd=custom&page=DAQ
```

No ODB installation command is run by the repository build. Registration is a
separate deployment step.

## General-user dashboard notes

The Equipment table uses the frontend-published module records, not fixed GUI
addresses: `/Equipment/VME/Info/<module>/BaseAddress`, the existing
`/Equipment/VME/Settings/<module>/Enabled` setting, and
`/Equipment/VME/Variables/<module>/CommunicationOK` for V792, V1190, V775,
V1720E, and RPV130. EASIROC uses `/Equipment/EASIROC/Settings/Enabled`, its
network address setting, and its communication Variables. A missing ODB value
is shown as `—`; the page does not fall back to a hard-coded value.

The VME and EASIROC parent-row Enable switches use
the same MIDAS frontend processes shown on the Programs page. The dashboard
uses `cm_exist` for process state, `start_program` to start a frontend, and
`cm_shutdown` to stop one. There is no separate frontend Enable ODB setting.
The monitor records connected frontends as participants before BOR, and the
frontends snapshot participation in their RunSnapshot `Metadata/EnabledForRun`.
In STOPPED, a connected frontend shows Enabled and an absent one shows Disabled.
During RUNNING or PAUSED, a frontend excluded from the recorded run shows
Not participating. START requires at least one connected frontend.

Frontend Start/Stop controls are locked for RUNNING and PAUSED. While STOPPED,
the dashboard rechecks MIDAS run state before calling the same RPCs as Programs
and confirms the resulting process state. The module-name links use mhttpd's installed
`?cmd=odb&odb_path=...` URL form and point to the corresponding Settings tree.

Run Status and the Equipment parent rows show MIDAS physics `Events sent`
while RUNNING. After STOP, they show the last EOR-complete run's
`/DAQ/Status/Runlog/{VMEEvents,EASIROCEvents}` values and label them
`Last run <number>`. A rejected START may advance `/Runinfo/Run number` without
changing this completed-run number. Missing EOR data shows `—`; a frontend
absent from the completed run shows `N/A` (`-1` in Runlog). The Rate table
continues to show current MIDAS Statistics.

Raw hardware `EventCounter` values are deliberately excluded from the
general-user HEALTH table. Their meanings and readout timing differ by module,
so displaying them side by side can imply a false synchronization. They remain
Expert-GUI diagnostics. General users see the VME and EASIROC physics counts in
Acquisition, plus `EventSlipCount` and the V1720E
`CounterDiscontinuityCount` in Errors & warnings. If comparable per-module
counts are needed later, add distinct monitor/frontend-owned software readout
keys that increment at one explicitly defined synchronized readout point; do
not repurpose the raw hardware counters.

Disk capacity uses monitor-owned `/DAQ/Status/Disk/{FreeGB,TotalGB}` values;
the browser shows `FreeGB / — GB` only when TotalGB is unavailable.

The disabled Stop condition inputs are visual only. They do not write ODB and
do not affect Start or Stop. A future automatic-stop implementation could own
`/DAQ/Settings/StopAfterSeconds` and `/DAQ/Settings/StopAfterEvents`, with
`0` meaning disabled.

The rate-history canvas keeps up to one day in browser memory. The visible
range can be switched between 10 minutes, 30 minutes, 1 hour, 6 hours,
12 hours, and 1 day. Sampling can independently be switched between 1, 5,
10, and 30 seconds without clearing existing history. History is intentionally
cleared by a reload. Recent messages come from MIDAS JSON-RPC
`cm_msg_retrieve`; the standard Messages and Status links remain available.

The Alarm ON/OFF control reads and writes `/Alarms/Alarm system active` and
confirms each write by reading it back. MIDAS supplies the standard alarm beep;
the DAQ page disables TALK speech in the browser and stops any sound playing in
that tab when Alarm is switched off. Status collection and the
Severity/Reason display continue while the alarm system is off. On re-enable,
`daq_monitor` evaluates the current status and triggers only alarms that are
still active.

## EASIROC slow-control page

`easiroc.html` reuses the existing page with this order: IP Address and READ,
selected device IP/status, Desired Settings, then `Save to ODB → Apply to
hardware`. Detailed status/LastApplied panels are removed; concise result and
error messages remain. READ performs ODB reads only. Unknown/duplicate IPs
preserve the current selection and Desired Settings.

Extra devices are read from `/Equipment/EASIROC/Devices/<id>/IPAddress` and
have independent `Settings`, `Commands/ASICSlowControl` and
`Variables/ASICSlowControl` subtrees. The existing single-device settings are
still accessible through the implicit `legacy` ID using its existing network
IP. Duplicate IPs, including collisions with that legacy address, are rejected.
READ never registers an IP or creates a schema. Legacy ASIC Save is retained;
extended controls become available if a complete extended schema has been
explicitly provisioned. Partial extended schemas are rejected; an old
ASIC-only ODB remains usable without migration.

Desired Settings include existing ASIC threshold/slope/feedback/shaping/mask
and InputDAC fields, plus per-ASIC Probe Enable/Channel/Signal, ten-pattern
SelectableLogic with OneChannel/HitNumThreshold/BOOL[64] AndChannels, and Raw
or 46--278 ns TriggerWidth in steps of 8 (default 118 ns). InputDAC UI values
are 256--511 (default 350); discriminator DAC is 0--1023 (default 600).
Physical meaning is explained beside the ASIC inputs without inventing a
calibrated voltage/charge conversion.

Save writes Settings only and verifies every field with ODB readback. It
checks STOPPED during sequential writes, warns about partial saves and retains
Desired Settings after failure. Polling does not replace staged settings.
Unsaved changes or remote ODB edits prevent Apply. The existing monotonic
ApplyRequestId mailbox is retained; request IDs cannot wrap.

Hardware Apply requires STOPPED and a known zero transition flag. The existing
Enabled, valid/saved settings and idle-mailbox checks remain. HV ON/OFF,
unknown HV and missing HV monitoring do not restrict Apply. HV Enable,
SetVoltage and CurrentLimit are outside this Apply, and no extra InputDAC
confirmation is shown. Registered-device and legacy requests use the existing
frontend RBCP backend. All configured ASIC, Probe, SelectableLogic and
TriggerWidth fields are included together; BOR never automatically applies.

The frontend's local ODB semaphore excludes START from the fresh permission
check through Apply/rollback/result persistence. Web rechecks run/transition
state before posting, while the backend enforces the authoritative gate. Other
ODB clients can wait during the synchronous write/latch sequence or finite
RBCP timeouts. Remote ODB Apply is rejected because this exclusion requires
the local ODB. Failed rollback sets HardwareStateIndeterminate; BOR rejects
START until successful re-Apply clears it. LastApplied remains a successful
transmission record rather than hardware readback.

The 2026-11-04 lock-shortening task stopped at its required precondition:
source inspection cannot guarantee that every START reaches the monitor's
sequence-400 callback, and live development registration was not queried.
The long-lock implementation above is unchanged. Standard MIDAS Web, odbedit,
mtransition, sequencer, automatic restart and direct SDK calls dispatch to
currently registered clients; monitor absence is not a hard-coded SDK error.
A standard deferred START handler in feeasiroc is the documented alternative,
not an implemented replacement. See the precondition audit in
`easiroc_frontend/docs/easiroc_slow_control.md` for evidence, unresolved
registration/lifecycle requirements and test limitations.

The subsequent read-only Deferred START audit identifies blockers in the
reference MIDAS 2.1 revision `624b94843`: run-number re-increment on automatic
resume, inconsistent STARTABORT handling for TR_DEFERRED/CM_DEFERRED_TRANSITION,
and a cancellation/resume race requiring verification at the SDK layer.
Synchronous Apply can remain synchronous (START is received by the initiating
client through ODB; the frontend resumes only after returning to its scheduler),
but adding a deferred registration alone is not ready for adoption. Required
frontend policy, registration lifetime and stale/canceled requests also need
resolution. The custom dashboard would need Requested Transition and deferred
status/error/cancel polling; immediate JSON-RPC success is not run completion.
The full findings, source line references and offline test plan are in the
existing slow-control design document. No Web or runtime code was changed.

Open `easiroc.html?mock=match` for a two-device offline preview; no MIDAS or
hardware access occurs. Mock scenarios retain the existing status cases.
Run `make -C easiroc_frontend check-web NODE=/path/to/node` from the repository
root for DOM/RPC mocks. On 2026-11-04, read-only HTTP checks confirmed that
port 8181 reports experiment `daq-dev`, `/Custom/Path` points to
`/home/nagao/midas/midas/online/daq_monitor/web`, and
`/Custom/EASI Slow Control` is `easiroc.html`. The page URL is
`http://127.0.0.1:8181/?cmd=custom&page=EASI%20Slow%20Control` (the ODB key
is not a `/Custom/...` page URL). The registered page, direct HTML, JS, CSS
and mock data returned HTTP 200 and matched the development files byte for byte,
including after the layout fix. A hard reload is needed for cached assets.

The layout now places IP/READ first, selection and DAQ state next, Desired
Settings next, then the horizontal Save → Apply buttons and result/error.
The hidden mock toolbar has an explicit CSS rule so its flex layout does not
make it visible on the normal page. Firefox headless verified this behavior,
layout at 1440 and 450 CSS-pixel viewports, unsaved edit disabling Apply,
unknown-IP preservation, second-IP READ and the generated option counts/defaults
using the served mock page. The standard MIDAS resize listener reports a
missing `mheader_message` on mock pages, which omit MIDAS header initialization;
the inspected controls still work. MIDAS itself was not changed.

The actual page READ logic was also exercised with a DOM harness permitting
only read-only `db_get_values` HTTP requests to 8181: legacy IP
`192.168.10.40` loaded code 600 and InputDAC 350, and an unknown IP preserved
that selection/settings. The current ODB has no Devices directory or complete
extended schema; extended inputs stay disabled without provisioning. Multiple
IPs, extended settings, RUNNING/PAUSED guards and Save/Apply behavior were
tested with offline mocks, without changing the real DAQ state or invoking
real Save/Apply. The complete normal MIDAS header/menu rendering and actual
Save/Apply/rollback remain untested. No ODB writes, service changes, hardware
communication or production `/home/daq`/8081 operations were performed.
Migration design and enablement limitations are in
`easiroc_frontend/docs/easiroc_slow_control.md`.

### Editing lock and pending-request follow-up (2026-11-04)

A later read-only check found no `feeasiroc` registration in development
`/System/Clients`. ApplyRequestId was 3, ActiveRequestId 0,
LastHandledRequestId 1, ApplyInProgress false, ApplyState Idle and
LastApplyError empty. Idle alone did not mean the mailbox was acknowledged.
The host process state cannot be established from sandbox-local `ps`.

Previously the 45-second acknowledgement deadline was checked only before
awaiting an ODB read. An unresolved read/write promise could keep
`applyPending` true indefinitely, preventing its `finally` block from
unlocking the editor. ODB reads and writes now have a 10-second client-side
limit; acknowledgement polling retains its 45-second deadline, including
time spent awaiting reads. These limits do not cancel a submitted ODB write
or a hardware operation. Preparation has several bounded ODB reads, so the
45-second deadline is specifically the acknowledgement phase.

Editing/READ unlock on success, failure or timeout while STOPPED, preserving
Desired Settings. Re-Apply separately requires known status, an acknowledged
mailbox, no active request and a registered frontend. Registration alone is
not a liveness guarantee. An ambiguous submission is remembered per device
for the page lifetime until its request ID is acknowledged; an idle snapshot
with the old IDs cannot authorize retry. ODB pending IDs also block retries
after page reload. A page reload loses the browser-only ambiguous-submission
marker, so it is not a way to establish completion of a timed-out write.
Save/reread failures also used to skip UI cleanup when the saved snapshot
became unavailable. UI cleanup now retains editable Desired Settings and
re-enables READ, while Apply stays blocked until saved settings are confirmed.
Errors, rollback details and HardwareStateIndeterminate/START inhibition
remain visible. No backend safety gate, lock or transition design changed.
Offline tests exercise terminal outcomes, absent/disconnected frontend,
unresolved preflight/submission/acknowledgement promises, deadline expiry,
late acknowledgement and per-IP retry isolation, alongside old/new schemas.

## Manual buffer-clear page

`buffer-clear.html` is linked from the DAQ dashboard. It checks the exact MIDAS
client names `fevme` and `feeasiroc` before enabling each button, requires
`/Runinfo/State` to be STOPPED, writes one monotonic request ID, and waits for
the owning frontend's terminal acknowledgement. The frontend independently
rechecks STOPPED. A request left in ODB while a frontend is absent is
acknowledged without execution when that frontend next starts.

VME results are reported separately for V792, V1190, V775, and V1720E. RPV130
is deliberately excluded. NIM-EASIROC performs only a bounded host TCP receive
drain; the authoritative controller does not document a configuration-safe
device FIFO-clear command. Open `buffer-clear.html?mock=1` for a no-I/O preview.

## Data sources

The page uses these monitor-owned records:

- `/DAQ/Status/Global/{Severity,Summary,CanStart,CanStartReason}`
- `/DAQ/Status/Run/{RunNumber,State,DurationSec,ParticipationValid,ParticipationRunNumber,VMEParticipating,EASIROCParticipating}`
- `/DAQ/Status/Frontends/VME/{Severity,Reason,Connected,Participating,EventSlipCount}`
- `/DAQ/Status/Frontends/EASIROC/{Severity,Reason,Connected,Participating}`
- `/DAQ/Status/Logger/CurrentFilename`
- `/DAQ/Status/Disk/{Severity,Path,FreeGB,TotalGB}`

Run timestamps come from standard `/Runinfo/{Start time,Stop time}`. Module
Enable values and communication readbacks come from the Settings and Variables
paths documented above.

It also reads the standard MIDAS statistics fields `Events per sec.`,
`kBytes per sec.`, and `Events sent` from:

- `/Equipment/VME/Statistics`
- `/Equipment/NIM-EASIROC Physics/Statistics`

The two physics streams are shown separately. Their counters are not summed,
because they can represent correlated but non-identical event streams.

Acquisition Time writes `/Logger/Run duration` in seconds. MIDAS Logger uses
its standard STOP transition when the run duration is reached. Event Limit
writes the same value to `/Equipment/VME/Common/Event limit` and
`/Equipment/NIM-EASIROC Physics/Common/Event limit`. Each Physics frontend
uses its own internal events-sent counter and requests the standard MIDAS
STOP transition when its limit is reached. Blank input is saved as zero;
zero disables both limits. The GUI reads back both values and reports a
mismatch instead of showing one as the shared limit. The old
`/Logger/Channels/0/Settings/Event limit` must remain zero because it counts
all events written by the Logger channel, including both streams.

Start and Stop call the mhttpd JSON-RPC method `cm_transition` with
`TR_START` or `TR_STOP`. The Start button directly follows the published
`CanStart` value; the browser does not recalculate policy or severity.
An accepted asynchronous START request remains pending until the final run
state is observed. If STARTABORT returns to STOPPED after the run number
advances, the page checks MIDAS Messages for that run's `start aborted`
notification, releases the controls, and shows the monitor's CanStartReason or
a MIDAS Messages fallback. The run-number change alone is not an abort signal:
it can precede a successful BOR. A timeout releases a request whose final state
cannot otherwise be confirmed.

At START sequence 400, the monitor records the connected frontend set with the
target run number and publishes the validity marker last. During that run only
those frontends contribute frontend status and alarms. A participant remains a
participant after disconnecting, so its loss is an ERROR; a disconnected
non-participant is shown as `Not participating` and is not alarmed. An absent,
invalid, or mismatched run-participation record falls back to monitoring both
frontends. Configuration and RunSnapshot records remain diagnostic BOR output
and are not used to infer participation from a previous run.
