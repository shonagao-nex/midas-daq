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

### EASIROC HV panel (development sources, 2026-11-04)

The bottom of `easiroc.html` now contains an independent HV Control panel.
It reads device-specific `Variables/HV`, `Commands/HV/TargetVoltage_V` and
`Settings/HV/CurrentLimit_uA` through existing ODB reads. It never accesses a
module directly or submits ASIC Apply. Missing/invalid/stale data is Unknown;
current over a configured limit is red. Protection state is displayed
separately from the threshold comparison. All HV controls remain disabled:
Enable/re-enable/readback semantics, per-device calibration and near-zero
voltage tolerance have not been established. Programmatic handler invocation
also cannot submit an operation; OK/Cancel wording and guards are mock-tested.
See `easiroc_frontend/docs/easiroc_slow_control.md` for register evidence,
software policy tests and the intentionally blocked hardware integration.

The local Node DOM/RPC mock passes 94 checks using the repository's existing
Node binary (command in the frontend Makefile `check-web` target). No real ODB,
service, hardware, HTTP request or browser session was used in this task.
Historical development 8181 `/Custom/Path`/served-asset observations above are
not revalidated here. Source changes do not establish live deployment on 8081
or served development deployment on 8181.

### Revised HV integration (2026-11-04)

This supersedes the preceding all-controls-disabled HV stage. The bottom panel
supports a staged 0--62 V target, independent HV request/result tracking and
separate confirmed Enable, normal stop/Disable and emergency shutdown actions.
It preserves edited targets across refresh, displays residual voltage/validity/
last update/errors and colors current above the independent ODB CurrentLimit
red. New missing-key default CurrentLimit is 10 µA; existing values are retained.
Capabilities are supplied per registered device by the frontend. Normal stop
ramps at 2 V/s and verifies ≤1 V before shutdown; emergency shutdown skips that
voltage wait. Both are STOPPED-only, including for emergency requests.

The requested rtl/HVControl.vhd was not available in the searched development
sources. Production Enable is separately gated false pending matching RTL
review; the panel reports that reason. This is not an ODB/browser unlock.
The frontend now contains actual MADC/shutdown callbacks and scheduling, so
launching it requires separate authorization for real hardware communication.
No such launch, ODB/service access or deployment occurred in this task.
Port 8181 served paths/assets were not queried; port 8081 and /home/daq were
not accessed. Local DOM/RPC mocks pass 110 checks; no real-browser layout or
served-file validation is claimed.

Mock-only previews: `easiroc.html?mock=hv_disabled`, `hv_settling`,
`hv_overcurrent`, `hv_stale`, `hv_error`. Their simulated capabilities and
acknowledgements do not enable hardware. Details and remaining hardware checks
are in the latest HV integration section of
`easiroc_frontend/docs/easiroc_slow_control.md`.

### Supplied RTL follow-up (2026-11-04)

`reference/firmware_20150923/rtl/HVControl.vhd` is now available and reviewed.
It clears shutdown on the first DAC byte write, before the zero-DAC sequence
has finished. Production Enable capability therefore additionally requires a
known previous DAC setpoint of exactly zero and an acknowledged shutdown;
low measured residual voltage alone is insufficient. The old missing-RTL
reason is replaced by this explicit recovery condition. Normal-stop/re-Enable
from a known zero state is supported; fresh startup and recovery from a
nonzero/unknown DAC remain blocked until a safe initialization procedure is
established. This is explained in the latest frontend design-document section.
The Web mock's OFF/Enable-capable fixture represents an already known zero DAC,
not a supported bootstrap from Unknown. Web mock still passes 110 checks;
no real ODB, served GUI, service or hardware was accessed.

### Legacy-compatible initial Enable (2026-11-04)

This supersedes the preceding known-zero-DAC/prior-shutdown requirement. The
backend now admits explicit zero-target Enable from Unknown (including after
frontend restart) and after emergency shutdown without requiring a known
retained DAC or prior shutdown. Other guards remain, including ASIC/InputDAC,
STOPPED/no transition, no ASIC Apply, fresh voltage ≤1 V/current within limit,
valid CurrentLimit, no active ramp or latched fault, and no known-ON duplicate
Enable. Backend capabilities drive the existing page; product JS/layout did
not need changes for this correction.

Unknown remains Unknown on screen and no measurement validity is synthesized
by submitting Enable. It enters command-derived ON/zero and final settling
only after frontend callbacks succeed. The Web DOM/RPC suite passes 114 checks,
including submission from an Unknown snapshot without writing ASIC request IDs.
Hardware can briefly resume its previous DAC output on the first DAC byte
before zero is transferred. This compatibility change does not guarantee
physical safety; see the latest frontend design section for retained RTL and
analog risks. No ODB/service/hardware access or served-file deployment occurred.

### 通常HV画面への統合（2026-11-04、現行）

開発8181の通常 `easiroc.html` を使用する。専用試験画面・backend・起動経路は撤去した。
通信なしの `?mock=` プレビューと通常mockテストは引き続き使用できる。

通常 `feeasiroc` は装置別HV schema/capabilityを生成する。
Settings/HV/HardwareAccessEnabled（新規false）は実機アクセスの明示的な許可であり、
falseではHV要求はRejected、周期測定なし、全HVボタン無効、理由を枠内に表示する。
現在の192.168.10.40はfalse、CurrentLimit=10 µA、HV状態と測定値はUnknownである。
許可trueは実機監視が開始し得るため、実機試験の許可後に設定する。
初回Enableの既知DAC=0 V、shutdown済み、Unknown禁止条件は追加していない。
通常停止・緊急停止・確認ダイアログ・独立HV要求とASIC排他は維持する。

InputDACの新規初期値・Web説明・mock値は両ASIC全64chとも480。
開発既存Settingsも各32個の350→480へ変更した。LastAppliedは350のままで、
実機Applyは実行していない。設定不一致表示はこの未適用変更を正しく示す。

通常frontendをSTOPPED/遷移なし/取得なしで再起動し、Firefoxで通常ページのREAD、
64ch=480、Unknown、禁止理由を確認した。配信4ファイルは開発ソースと一致する。
HV操作は実行していない。Web mock115 checks、frontend make all check成功。
変更・バックアップ・残存DACリスクの詳細は既存frontend設計文書の最新節を参照。
本番8081と `/home/daq` は変更していない。


### increaseHV ramp revision (development sources, 2026-11-04)

This supersedes the earlier 2 V upward steps and voltage-arrival settling gate.
Enable starts at zero; upward DAC steps are at most 4 V, each followed by at
least one second from write completion and a successful MADC current check.
The final step is also checked before Steady. Default CurrentLimit remains
10 µA (strictly greater is an exceedance); overcurrent ramps down at 10 V/step
without automatic restart. Normal descent remains 2 V/step; Normal Stop retains
the measured ≤1 V check before Disable. Measurement/communication failure
blocks further steps, and STOPPED/ASIC exclusion remains unchanged.

The panel now shows the DAC setpoint separately from measured voltage.
`Variables/HV/MonitoringStatus` displays current monitoring warnings separately
from the last operation result. Voltage mismatch above 1 V is advisory;
HardwareAccessEnabled=false preserves completed result history.

Offline validation: `make all check` passed (HV 216, ODB adapter 2630 checks;
other existing tests passed); Web syntax and DOM/RPC mock passed 119 checks.
No hardware communication, ODB changes, frontend restart or 8181 served-path
verification was performed. Production 8081 and /home/daq were not accessed.
See `easiroc_frontend/docs/easiroc_slow_control.md` for the legacy comparison
and unverified hardware behavior.


### Normal HV descent revision (2026-11-04, development sources and offline mocks)

This supersedes the 2 V downward steps and 60-second final settling timeout.
Normal descent now writes at most 10 V per step, measures through the existing
MADC procedure about once a second, and holds that exact DAC setting until
measured voltage is within ±1 V. At the final zero setting it waits for a valid
measurement ≤1 V, then sends shutdown; Stop succeeds only after acknowledgement.
A partial nonzero Voltage reduction ends at its requested target; a reduction
to zero also disables after the measured-zero check. Healthy settling has no
deadline and produces no voltage-tracking warning. Communication, invalid
measurements, invalid CurrentLimit and access-condition failures still fault.

Emergency remains available during settling and discards the old descent.
Overcurrent protection bypasses all normal voltage-arrival gates and retains
10 V steps to zero. Upward 4 V steps and the 10 µA default are unchanged.
The panel continues displaying setting, measured voltage/current and RampStatus.
An acknowledged Pending operation with fresh samples remains Pending beyond the
local 120-second ACK timeout; unavailable/stale status still reports uncertainty.
The existing hv_settling preview shows DAC 52 V / measured 60 V / target zero.

Validation: make all check passed (HV284, ODB adapter9354 checks plus all existing
tests); Web syntax and DOM/RPC mock125 checks passed. Real HV/communication,
ODB changes, frontend restart and served-page verification were not performed.
This revision changes development sources only; production8081, /home/daq,
Firmware, MIDAS core and START exclusion were not changed.

### Compact EASI HV controls (development GUI, 2026-11-04)

The IP field offers 192.168.10.40–43 through a native datalist and accepts
other canonical IPv4 addresses. Suggestions do not register devices. READ still
loads the existing registered device's ODB settings; invalid input is rejected
before RPC and unregistered addresses remain rejected. Editing/selecting an IP
only updates local presentation. Until the input matches a successfully READ
selection, all HV facts display Unknown and HV actions are disabled. READ in
progress also hides the previous device's HV facts. ASIC Save/Apply is unchanged.

The HV panel keeps Enabled, set/measured voltage, measured current, Monitoring,
RampStatus, Current Limit and Last update in a compact grid. Valid measurement
status and idle operation text are hidden; invalid/stale measurements, backend
blocking reasons, warnings, pending results and communication errors remain
visible. Target Voltage and Set Voltage are grouped, with Emergency in red.
The confirmation dialogs, capability checks and frontend algorithms are unchanged.

The explanatory text removed from the panel still applies: Enabled reflects
acknowledged commands, not hardware readback, and shutdown acknowledgement does
not establish zero residual voltage or independent physical shutdown. Current
is DC-DC converter current. HV uses a separate mailbox from ASIC Save/Apply.
The existing frontend ramps upward at 4 V/step; normal descent uses 10 V steps,
waits for measured ±1 V at each step and checks ≤1 V at zero before shutdown.
Emergency uses the immediate shutdown path. See the existing
`easiroc_frontend/docs/easiroc_slow_control.md` for the detailed safety model.

Validation: `make -C easiroc_frontend check-web
NODE=build/tools/node-v22.16.0-linux-x64/bin/node` (144 checks plus JS syntax),
existing GUI contract check (16), and offline Firefox mock layout/selection
checks. Browser captures and results are under `build/easiroc-gui-compact/`.
Only development port 8181 static assets and ODB identity/path were read to
verify the served source; no hardware action or ODB write was used.

### HV parameter revision pending cadence decision (2026-11-04)

Development sources now use a 20 µA default for newly created CurrentLimit keys;
existing ODB values are retained. Enable still starts at 0 V and keeps the ≤1 V
residual admission. Disable now uses ±5 V at normal descending steps and ≤5 V
before shutdown at zero: up to about 5 V residual may remain. Labels are Enable
and Disable; Emergency, current protection and failure handling are unchanged.
Ramp measurement cadence is still 1 second pending clarification: a 3-second
MADC cadence would also slow the existing current protection detection. No
frontend restart or ODB change was performed. See the latest slow-control document
section for the outstanding cadence choice and validation results.
