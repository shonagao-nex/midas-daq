# NIM-EASIROC Slow Control investigation and architecture

This document is based only on the legacy implementation in
`reference/controller_20170425/`. Generic EASIROC and SiTCP behavior is not
used to fill gaps. No hardware access or register write was performed while
preparing it.

## Current extension status (development sources, 2026-11-04)

The backend extension was built and tested offline under
`/home/nagao/midas/midas/online`. A subsequent authorized read-only Web check
on 2026-11-04 confirmed development port 8181 as experiment `daq-dev`,
`/Custom/Path` as the development `daq_monitor/web` directory and
`/Custom/EASI Slow Control` as `easiroc.html`. Registered HTML, JS, CSS and
mock data returned HTTP 200 and matched the source bytes. Firefox exercised
the served mock page; a DOM harness exercised the real READ logic using only
`db_get_values` against the development ODB. The existing legacy IP is
`192.168.10.40`; at that earlier check no Devices directory or complete
extended schema existed. Those checks did not perform real Save/Apply, ODB
writes, service operations or hardware communication. A later, separately
authorized task added the eleven requested extended Settings leaves without
changing existing values or LastApplied, and verified the served real page
using a read-only browser proxy. The final pre-hardware inspection below
describes the subsequent current state. Production `/home/daq` and port 8081
were not accessed.
The GUI-only follow-up moves DAQ state after IP selection, results after the
operation buttons, and fixes the hidden mock toolbar CSS. Backend locking,
Deferred Transition, START exclusion, Apply and rollback are unchanged.
See `daq_monitor/web/README.md` for the browser checks and their limitations.

### Final pre-hardware inspection (2026-11-04, read-only)

This inspection used only HTTP `db_get_values`/`db_key` at development 8181,
source/filesystem inspection, read-only process/systemd queries and a Make
dry run. No binary, service, hardware, DAQ transition or Apply was executed;
only this existing document and the requested final `codex.log` were updated.

**Observed state.** Experiment is `daq-dev`; run 186 is STOPPED, transition=0,
requested transition=0. No `feeasiroc` entry exists in `/System/Clients`.
The status equipment's green Common/Status and old firmware Valid=true are
persistent historical values, not current process/communication evidence.
`/Programs/feeasiroc` has Required=true, the development start wrapper, and
Auto start/stop/restart=false. The PID file in the development runtime contains
612854, but this sandbox PID namespace cannot establish its host liveness or
executable. Named system units `feeasiroc.service` and
`midas-feeasiroc.service` are not found/inactive; no EASIROC unit was found in
the inspected system unit files. User-systemd bus access failed, so a custom
user manager or other supervisor is not excluded.

The on-disk development `bin/feeasiroc` SHA256 is
`482ff6645ecfe838434eb10b6317f74c8cbfe5badf47b2d4f4d6a712e9b9e062`,
ELF Build ID `1d72845ec579cc9cc9702a584b8d87e9030be9ab`.
`make -n -C easiroc_frontend feeasiroc` reports nothing to rebuild, which is
dependency/timestamp evidence only; it does not establish reproducibility or
identify a host running executable. The actual host `/proc/<pid>/exe`, command
line and build identity must be checked before a separately authorized launch.

`scripts/start_feeasiroc.sh:4-20` derives the binary/exptab from the invoking
user's HOME and defaults the experiment to `daq`; an authorized development
launch must explicitly select `MIDAS_EXPT_NAME=daq-dev` as user nagao. The
development exptab contains only `daq-dev /home/nagao/midas/midas/daq-dev`.
`scripts/dev_start.sh:77-105` starts infrastructure but explicitly not frontends.
Do not use the all-services script merely to launch the frontend.

**Startup side effects.** With the observed Enabled=true,
`feeasiroc.cxx:900-931` starts the existing diagnostic worker: RBCP firmware
read and TCP port-24 connect/close (`feeasiroc.cxx:622-657`,
`easiroc_status.cxx:40-51`). These are actual hardware communications, even
though the diagnostic sends no register writes and receives no data stream.
STOPPED startup does not request a DAQ START or issue DAQ ON; the latter is a
BOR acquisition action (`feeasiroc.cxx:1159-1162`). Periodic status repeats
diagnostics while idle (`feeasiroc.cxx:1424-1454`, period 10000 ms at line 885).
Do not start the frontend under a no-hardware-communication authorization.

Startup preserves existing Settings via `ensure_odb_value()`
(`easiroc_odb.cxx:64-74`) but creates missing legacy/default bookkeeping keys.
It rewrites Info/capabilities, clears firmware readback before diagnostics,
resets RunSnapshot and ConfigurationOK/run number, publishes runtime counters,
errors and apply/rollback state, and sets global-busy readiness false
(`feeasiroc.cxx:241-257,914-917`, `easiroc_odb.cxx:53-61,97-119,755-813`).
MIDAS also publishes client/equipment registration/Common information. Thus
starting is neither ODB-read-only nor a way to preserve all status history.
The current complete ASIC LastApplied is read and retained, not replaced by
the newly provisioned desired extras.

**Outstanding request.** Current request=3, active=0, handled=1,
successful=1, ApplyInProgress=false, ApplyState=Idle, LastApplyError empty,
HardwareStateIndeterminate=false. On a clean startup request>handled is
acknowledged without execution (`feeasiroc.cxx:169-203`,
`easiroc_manual_apply.cxx:233-247`). Expected publication is request still 3,
handled 3, active 0, successful still 1, state Indeterminate, inProgress=false,
lastAttemptSucceeded=false, startup error/time and
HardwareStateIndeterminate=true. Rollback bookkeeping is reset/published.
LastApplied and Settings are retained. Do not manually rewind request IDs,
clear the uncertainty flag or synthesize LastApplied to bypass this state.

The Web can then permit an explicit new request (normally 4) after READ,
registration, a confirmed idle/acknowledged mailbox, valid saved settings,
Enabled=true, STOPPED and transition=0. HardwareStateIndeterminate blocks
participating EASIROC BOR, not a corrective Apply; HV state is not a gate
(`daq_monitor/web/easiroc.js:229-242,397-416`, `easiroc_manual_apply.cxx:13-33`,
`feeasiroc.cxx:1028-1041`). Full successful Apply clears uncertainty. Current
monitor CanStart=true is for EASIROC disconnected/nonparticipating, not proof
that EASIROC acquisition may start (`daq_monitor.cxx:606-607`). No START was
tested, and the standard transition/locking design is unchanged.

**Current settings and history.** Legacy IP is 192.168.10.40, the same address
read by the legacy manual RBCP writer (`feeasiroc.cxx:343-365,447-454`). There
is no Devices registry/duplicate route. Both ASICs have code=600, slope=1,
HG/LG feedback=100 fF, shaping HG=100/LG=50 ns, InputDAC[32]=350 and every
ChannelEnabled=true. Probe is disabled on both ASICs, local channel=0,
signal=4; logic Pattern=3/Or64, OneChannel=0, HitNumThreshold=1,
AndChannels[64]=false; TriggerWidth=118 ns. All 43 inspected Settings ASIC,
extended Settings and LastApplied ASIC leaves have the required MIDAS types
and array lengths, and current desired values meet source validation bounds.

LastApplied.Valid=true, RequestId=1, ApplyUnixTime=0x6abc6a30; ASIC values match
the current ASIC desired values. There is no Extended flag, Probe,
SelectableLogic or TriggerWidth in LastApplied: this is the legacy ASIC-only
schema. Current ConfigurationMatch=true is stale: the current comparison
would find extras different until uncertainty takes precedence or a complete
Apply succeeds (`easiroc_last_applied.cxx:14-60`). Neither record proves ASIC
readback; configuration readback is unsupported.

**Complete plan and first-Apply rollback limitation.**
`easiroc_manual_apply.cxx:59-87,159-177` validates/encodes the whole immutable
snapshot before its first write. The legacy encoder overlays threshold,
gain, shaping, mask and InputDAC on full 57-byte ASIC images. The plan is
ASIC serial load, Probe serial load, 11-byte logic at 0x78, then width at 0x88
(`easiroc_slow_control.cxx:112-125,297-328,420-434`). Each serial group writes
0x00, 0x03, 0x3d, 0x00, waits 100 ms, then writes 0x00 twice: 14 writes and two
delays overall. Disabled probes still transmit two all-zero 20-byte images;
current logic is [3,0,1,0,0,0,0,0,0,0,0] and width code is 10. Unrelated
FPGA configuration registers are absent from this plan. The existing write
transport can retransmit a command up to three times with a 1000-ms receive
timeout (`rbcp.cxx:21-22`); failed acknowledgement can follow a real write.

On full success the new LastApplied stores ASIC+extras, Extended=true, the
new request/time and Valid=true, and clears uncertainty
(`easiroc_manual_apply.cxx:221-229`). ODB publication first clears Valid,
publishes all content and sets Valid last (`easiroc_odb.cxx:964-1020`). A
publication failure is reported as Failed and marks hardware indeterminate
even after hardware transmission succeeded (`feeasiroc.cxx:371-389`).

On a failed first extended sequence the old ASIC-only LastApplied is
**incompatible for rollback**: the backend does not even attempt ASIC-only
partial restoration. It retains Settings/LastApplied, reports Failed and
`No compatible valid LastApplied snapshot`, RollbackAttempted=false,
RollbackSucceeded=false, HardwareStateIndeterminate=true
(`easiroc_manual_apply.cxx:190-218`). Once a valid complete extended record
exists, subsequent write failures get one full rollback attempt. Successful
rollback clears uncertainty; failed rollback retains it. Neither rollback
nor Apply changes HV settings or rewrites desired ODB Settings.

Incomplete/type-invalid LastApplied can fail ODB reading/startup; Extended=true
requires all extras (`easiroc_odb.cxx:891-961`). Invalid/unavailable rollback
images are rejected before rollback writes. A limitation is that startup
`initialize_last_applied_odb()` creates missing *legacy* fields with zeros
while preserving an existing Valid flag (`easiroc_odb.cxx:570-649`); therefore
Valid alone must not be taken as evidence of a complete historic record.
Current legacy fields are complete, so that repair path is not required here.

**Minimum next steps and proposed hardware test (not performed).**

1. In an authorized host session verify the actual frontend PID/executable,
   explicit daq-dev environment, on-disk build identity and exclusive ownership
   of 192.168.10.40. Review existing firmware/physical compatibility, wiring,
   trigger-output effects and InputDAC bias implications. Export current
   Settings, Commands, Variables and LastApplied again before any launch.
2. Obtain authorization including startup diagnostic traffic and the first
   full Apply without a compatible rollback baseline. Start only the existing
   development frontend, with DAQ still STOPPED; do not start DAQ to test Apply.
3. Verify stale ID 3 is acknowledged without execution, lastSuccess remains 1,
   uncertainty is true and START is inhibited for EASIROC participation. Check
   diagnostics and the new client registration. Do not clear status manually.
4. READ 192.168.10.40; confirm unchanged saved values, complete schema, no
   unsaved edits, STOPPED/transition=0. No Save is needed for this unchanged
   baseline. Submit exactly one explicit Apply, normally ID 4. Its full images
   also operate control/latch signals, disable Probe outputs, set Or64 and
   118-ns width; it is not an isolated single-field threshold write.
5. Wait for frontend terminal status, not just a browser timeout. Check IDs,
   errors, rollback flags, Extended/Valid, all LastApplied fields and matching
   Settings. On success require hardware-indeterminate=false; record this as
   transmission success only. On failure/unknown status keep EASIROC START
   inhibited, capture the error/partial-write state and stop the trial pending
   recovery authorization. Do not deliberately inject a hardware failure.

No MIDAS, ODB-lock, deferred-transition or START-exclusion modification is
proposed. GUI RPC timeouts do not cancel a hardware operation, and the current
synchronous ODB lock can delay other ODB clients during Apply/rollback. Actual
transport reliability, analog behavior, device ownership and restart behavior
remain untested on hardware. This inspection did not rebuild or run tests.

**Apply uses only the DAQ state/transition gate, not HV state.** The frontend
requires STOPPED and a known zero `/Runinfo/Transition in progress` value,
alongside the existing Enabled, idle-mailbox and complete-settings validation.
HV ON, OFF, unknown, or an unimplemented monitor do not restrict Apply. No HV
state is read by the backend; HV Enable, SetVoltage and CurrentLimit are not
part of either the desired Apply plan or rollback. InputDAC needs no extra
confirmation. BOR does not automatically Apply.

Both the legacy ASIC path and registered-device path now reach the existing
RBCP writer inside `feeasiroc`, using the same request-ID state machine,
LastApplied and single-rollback backend. There are no new RPCs, services or
runtime dependencies. The ODB adapter uses the same typed 28-field traversal
as the mock store. Existing legacy settings without any extended keys retain
their ASIC-only scope. A complete legacy Probe/SelectableLogic/TriggerWidth
schema is read and applied as one full snapshot; a partially provisioned
schema is rejected. Legacy LastApplied records retain their original scope,
and newly transmitted extras are stored with `Extended=TRUE`.

### START/Apply exclusion and recovery

The installed MIDAS `src/midas.cxx` sets `/Runinfo/Transition in progress`
using `db_set_value()` before it invokes frontend BOR callbacks. Therefore a
single early STOPPED read is insufficient. `ApplyOdbLock` uses MIDAS's existing
local ODB semaphore (`db_lock_database`/`db_unlock_database`, declared in
`msystem.h`). It covers fresh run/transition checks, the immutable Settings
and LastApplied snapshot, all hardware writes/delays, rollback, and terminal
status/LastApplied publication. A START that acquires ODB first sets the flag
and the Apply is rejected with zero writes. If Apply acquires it first, START
waits for terminal status before setting its flag. No transition callbacks or
`cm_yield` run inside the locked scope. Diagnostic hardware access is joined
before the first configuration write; its worker never accesses ODB.

This is currently a long synchronous exclusion scope for a **local** ODB;
remote-ODB Apply is rejected because this lock cannot provide its exclusion.
All other clients needing that ODB's lock may wait during Apply/rollback.
RBCP waits up to 1 s per attempt and retries each packet up to three times.
A full plan has 14 writes and two 100 ms delays: its reply-wait budget is
42.2 s, or 84.4 s with a late failure followed by a full rollback. These are
not strict elapsed-time bounds: resolver/socket calls, EINTR retries, joins
and scheduling have no shared deadline. The registered-device path holds one
lock across all device requests and joins the diagnostic worker under that
lock. The reference MIDAS defaults to a 10 s ODB lock wait (overridable by
`/Experiment/ODB timeout`) and calls abort on lock-wait failure, including
mutex waits from the frontend's own watchdog. No runtime timing/ODB values
have been measured. The lock is released on normal scope exit/unwinding;
it has not been shortened by the precondition audit below.

Rollback failure or unavailable/incompatible LastApplied sets
`HardwareStateIndeterminate`. BOR checks the legacy flag and every registered
device's flag/ApplyInProgress before acquisition hardware access and rejects
START on an indeterminate or active Apply state. Missing registered status is
also rejected. A fully successful re-Apply clears the flag; successful rollback
retains the old LastApplied and clears uncertainty about the transmitted
configuration. Desired Settings are never rewritten on failure. LastApplied
continues to mean successful transmission, not hardware readback. Persistence
failure after a successful hardware sequence also inhibits START. An in-process
inhibit is retained even if writing the ODB uncertainty flag fails; only a
successful, persisted recovery clears it. The ODB mock also injects that
publication failure and verifies START remains prohibited.

### Lock-shortening precondition audit (2026-11-04)

**Implementation stopped at the required START-path precondition.** The task
requires every START to pass daq_monitor's sequence-400 validator before
shortening the existing locks. Source inspection cannot establish that
invariant, and the running development registration was not read: this session
authorizes development sources/builds/mocks only, not connecting to the ODB or
operating services. No Apply/frontend/monitor executable source or test source
was changed by this audit. Long Apply/Rollback locks therefore remain in place.

The monitor calls `cm_register_transition(TR_START, validate_start_transition,
kStartTransitionSequence)` with sequence 400 only after it connects, checks
its exact client name and initializes alarm classes. Registration failure
exits the monitor (`daq_monitor.cxx:1225-1260`, `status_policy.h:103`). This is
a dynamic `/System/Clients/<id>/Transition START` registration, not a callback
hard-coded into the MIDAS transition dispatcher. Existence of a process/PID,
a Programs record, a service file, or a source registration call does not
prove that the correct callback is currently registered in `daq-dev`.

Inspected standard START entry points:

| Entry point | Source route | Sequence-400 guarantee |
| --- | --- | --- |
| Custom dashboard | `daq_monitor/web/daq.js:273-299` -> JSON RPC | Uses registered clients; cached CanStart is not a universal guard |
| Native MIDAS Web | `MIDASSYS/resources/mhttpd.js:3482` -> JSON RPC | Same registration dependency |
| JSON RPC | `MIDASSYS/src/mjsonrpc.cxx:3605-3622` -> `cm_transition` | No explicit mandatory monitor check |
| odbedit | `MIDASSYS/progs/odbedit.cxx:2534` -> `cm_transition(TR_START,...)` | Same dispatcher as a direct SDK caller |
| mtransition / detached START | `MIDASSYS/progs/mtransition.cxx:195` -> `cm_transition` | Same dispatcher |
| Sequencer | `MIDASSYS/progs/msequencer.cxx:932` -> `cm_transition` | Same dispatcher |
| Automatic run restart | `MIDASSYS/progs/mlogger.cxx:3593`, `src/mfe.cxx:2326` | Same dispatcher |
| TMFE / custom C/C++ clients | `MIDASSYS/src/tmfe.cxx:739`, `src/midas_c_compat.cxx:340` -> `cm_transition` | Same dispatcher |

`MIDASSYS/src/midas.cxx:5018-5099` enumerates the clients that are actually
registered, constructs the callback list, and sorts it by sequence number.
The MTHREAD path waits for lower-sequence predecessors (5101-5117). Therefore
normal direct SDK calls do not inherently bypass a registered monitor, but
if it is absent/unregistered it is not added to the callback list and there
is no hard-coded rejection for missing `daq_monitor`.

The SDK's required-program check is conditional on
`/Experiment/Prevent start on required progs` (`midas.cxx:4708-4746`). Repository
`README.md:94-99` documents the intended value as false. The checked-in
`scripts/frontend_programs.odb` contains only fevme/feeasiroc Programs records,
not a required daq_monitor definition. `scripts/dev_start.sh` starts the
monitor but does not verify its sequence-400 registration or make its absence
fatal to every future START. The checked-in systemd unit targets production;
it is not evidence of development registration. Production paths in these
repository files were inspected as text only; no production files/services
were accessed.

Consequently neither current registration nor mandatory participation across
monitor startup/failure/exit is established. Even a one-time registration
snapshot alone would not prove the lifecycle invariant. No missing-monitor
START or other transition was executed to demonstrate this counterexample.
The existing frontend BOR guard remains useful for its own hardware but does
not prove that every START reaches monitor sequence 400.

#### Alternative requiring a separate implementation decision

The initial alternative was the standard
`cm_register_deferred_transition(TR_START, callback)` in feeasiroc if mandatory
monitor participation cannot be established. The detailed source audit below
finds version-specific blockers; this is not an implementation-ready proposal. MIDAS
searches registered deferred handlers before executing normal BOR callbacks
(`src/midas.cxx:4833-4870`); MFE already calls
`cm_check_deferred_transition()` (`src/mfe.cxx:2139`). No independent service,
custom RPC, worker thread or custom lock service is needed.

The proposed later change would claim an immutable request and publish the
per-device ApplyInProgress under a short ODB lock, release it before all
hardware/sleep/join/rollback work, and reacquire it only for terminal status,
LastApplied and uncertainty publication. A shared frontend busy state covers
both legacy and registered devices. New Apply requests also check pending
Requested Transition. The deferred callback must wait for operation completion
rather than remain FALSE forever after rollback failure: after completion,
the existing BOR inhibition rejects an indeterminate START so recovery is
possible. This changes START-during-Apply from immediate rejection to standard
MIDAS deferral, so it has not been silently substituted for the requested
sequence-400 rejection design.

Unresolved before implementation: the actual development registration and
its enforced lifecycle, deferred START acceptance/display/cancellation/resume
and run-number behavior, immutable per-device claim/publication failure
handling, restart recovery and status-persistence failure. The Web currently
accepts only MIDAS_SUCCESS in `daq.js:298`; deferred result handling requires
verification with the installed JSON-RPC/transition modes. The registered
backend currently has an empty applying-status publisher and publishes
ApplyInProgress only at terminal status; merely narrowing its RAII lock scope
would leave a race.

Existing `make -C easiroc_frontend all check` and
`make -C daq_monitor all check` were executed successfully (15 C++ suites in
the frontend; 6 C++ suites and 2 Python contracts in the monitor). The all
targets were already up to date; no compiler warnings/errors were emitted.
These tests exercise the existing long-lock implementation. They do **not**
prove that hardware/rollback/join occurs outside the ODB lock, nor validate
a shortened-lock START gate or real MIDAS registration. Future implementation
must add both-path assertions for unlocked writers/delays/joins, claim failure
with zero writes, START-first/Apply-first races, persistence failure and
restart recovery before claiming this task's new behavior is verified.

### Detailed deferred START applicability audit (2026-11-04)

**Verdict: structurally compatible with synchronous Apply, but not safe to
adopt unchanged in the current reference MIDAS.** A one-line deferred START
registration does not preserve run numbering, error cleanup, cancellation or
frontend-absence protection. No executable source, ODB, service, hardware or
reference dependency was changed. This audit updated existing Markdown only;
no binaries, tests, clients or experiments were executed.

#### Version and evidence limits

`MIDASSYS` below denotes `/home/nagao/midas/midas_src`. Its `include/midas.h:37`
reports MIDAS 2.1. HEAD is
`624b94843df572c52eab5c93475e03ed79d65c7c`, described with tags as
`midas-2026-07-a-214-g624b94843`. `include/git-revision.h:1` has the matching
revision (2026-08-31). Reading bytes without execution found the same revision
string in `lib/libmidas.a`, the development `bin/feeasiroc`, and
`daq_monitor/bin/daq_monitor`. This identifies the inspected artifacts, not
the processes or libraries actually running on the DAQ host. Live version,
registration, required-program values and served GUI resources were not read.
Findings below are static control-flow conclusions and risks, not measured
runtime behavior.

#### 1. Exact registration and callback contract

`MIDASSYS/src/midas.cxx:3917-3955` installs the callback in a process-local
transition table/mask, creates `/System/Clients/<client>/Transition START
DEFERRED` with value 0, and opens a read hotlink on the shared integer
`/Runinfo/Requested Transition`. Value zero is still a registration marker:
the dispatcher checks key existence, not a per-request callback result.
The callback has signature `BOOL callback(INT transition, BOOL first)`.
FALSE means continue waiting, TRUE means resume; there is no error-string or
failure-return channel. `first` is a polling aid, not a request ID; its static
state is reset when the checker observes request zero (3969-3994). In
particular, registration with an already-pending request need not provide a
fresh TRUE first flag. The callback should inspect state idempotently.

Initial normal START does not ask that callback whether to defer. It sets the
transition flag (4653-4655), selects/updates a run number (4766-4793), searches
for any registered deferred marker, stores Requested Transition=TR_START and
`/System/Transition/deferred`, then returns CM_DEFERRED_TRANSITION=110
(4833-4870; `include/midas.h:591`). Thus even an idle frontend's START is
initially deferred while the registration exists. This is not an atomic
barrier over all registered deferred callbacks: the sender reports the first
marker it finds, the shared request can be observed by every registered
handler, and a resumed TR_DEFERRED call skips the deferred-marker search.
The intended deployment must have a single START deferral owner, or establish
compatible coordination among any other START deferrers. Runtime ownership
has not been established.

#### 2. Synchronous Apply and request receipt

The Status equipment is EQ_PERIODIC (`feeasiroc.cxx:883-887`). MFE calls its
readout in the scheduler (`src/mfe.cxx:1848-1861`, 921-923). Its Apply, delays,
join and rollback are synchronous; no `cm_yield`, `ss_suspend` or deferred
checker is called from those EASIROC paths. MFE calls the checker later in the
scheduler (2139), and services IPC via cm_yield (2332-2349).

After removing long ODB exclusion, the *initiating client* can accept START
by writing the request while feeasiroc is inside Apply. No new frontend
thread/RPC is required for this receipt. The frontend callback/hotlink update
and resumption cannot execute concurrently with synchronous Apply. They occur
only when Apply returns, results have been handled, IPC refreshes the hotlink,
and the scheduler next runs its checker. Hotlink update routes are
`src/midas.cxx:5459-5487` and `src/odb.cxx:14145-14166`; cm_yield dispatches
IPC at `src/midas.cxx:5774-5776`. The existing watchdog maintains activity,
not deferred callback dispatch (`src/midas.cxx:7403-7416`).

With the current long locks still installed, even the initiating client's ODB
receipt is blocked. Deferred registration alone does not shorten those locks.
Do not add cm_yield inside Apply as a workaround: it can dispatch ordinary
transition RPCs reentrantly into partially completed hardware work.

#### 3. Automatic resumption and version-specific blockers

`src/midas.cxx:3977-3991` resumes only if the process-local mask matches the
hotlinked request and callback returns TRUE. It calls
`cm_transition(request | TR_DEFERRED, 0, ..., TR_SYNC, FALSE)`, then clears the
local request cache. The resumed transition clears the ODB Requested
Transition (4800-4808); normal callbacks then execute and Runinfo State becomes
RUNNING only on success (5220-5233). Conditions include a live/connected
frontend, its initialized registration, scheduler/IPC progress, an uncanceled
request, idle transport/rollback work, and passing ordinary START validation.
Apply completion alone does not satisfy all of these.

Three blocking implementation findings in this revision:

1. **Run number increments twice.** Initial START with number 0 changes R to
   R+1 before deferral (4766-4793). The standard checker resumes with number 0
   (3984), which reads R+1 and changes it to R+2. An explicit target K is first
   stored as K and then resumed as K+1. These are static paths assuming no
   intervening administrative writes. Cancellation does not restore the old
   number. Preserve the reserved/requested number explicitly on resume; do
   not manually rewind the run number in an application callback.
2. **STARTABORT/error classification is inconsistent.** The wrapper
   `cm_transition1` at 5312-5323 tests `transition == TR_START`, rather than
   normalizing the TR_DEFERRED bit. An initial normal deferred result 110 is
   treated as failure and attempts STARTABORT (which can itself be refused by
   the still-active START flag). Conversely, failure of the resumed
   TR_START|TR_DEFERRED call does not enter the wrapper's STARTABORT cleanup.
   `cm_transition2` can set Start abort and clear the transition flag on a
   callback failure (5204-5211), but that is not equivalent to executing all
   STARTABORT callbacks. Other frontends whose BOR already succeeded can need
   cleanup (`src/mfe.cxx:128-143`). The earlier suggestion to rely solely on
   existing BOR rejection is therefore insufficient to guarantee cleanup in
   this version. Standard deferral status must be exempted from failure, and
   resumed START failures need normal STARTABORT handling.
3. **Cancel/resume claim is not atomic.** The checker uses cached request
   state; a TR_DEFERRED call skips the active-transition checks (4638-4655,
   5387-5399), and writes its own transition flag again. A concurrent cancel
   between callback readiness and resume entry is not guarded by a fresh
   atomic claim of the pending request. Cancellation suppression must be
   verified/fixed at the standard transition layer without holding an ODB
   lock throughout BOR or hardware operations.

These changes concern the reference dependency, outside the online repository;
this audit does not authorize or perform them. An equivalent corrected MIDAS
revision could be evaluated instead. Reimplementing automatic resume inside
the frontend callback would add duplicated transition logic and is not the
recommended minimal approach.

#### 4. Rollback failure and normal START rejection

Keep two existing responsibilities distinct: an in-flight/busy lifetime for
actual Apply/rollback/result-save attempts, and the persistent/in-memory START
inhibit for HardwareStateIndeterminate or result-persistence failure. After
work finishes, the deferral callback must be able to return TRUE even when
START must subsequently be rejected; otherwise FALSE forever strands a
pending START and also prevents a recovery Apply while the transition is set.

Return FE_ERR_HW with an error string from a normal START validation/BOR
callback when uncertainty/in-progress/status-read failure is present. The
existing EASIROC BOR guard is `feeasiroc.cxx:1028-1041`; the standard MFE
handler invokes BOR at sequence 500 (`src/mfe.cxx:2592`, 128). This prevents
EASIROC's own DAQ ON, but does not guarantee rejection before every other
frontend's same-sequence BOR. If that stronger ordering is required, add an
early ordinary EASIROC TR_START validation via the existing standard API,
before hardware-start sequences, while retaining the BOR check. It requires
no custom RPC or separate service. Correct SDK failure cleanup above is a
prerequisite for either form of normal refusal on resumed START.

#### 5. Frontend stop/disconnect and required programs

Deferred registration alone is not a frontend-presence interlock. Normal
disconnect deletes client information and closes databases/hotlinks
(`src/midas.cxx:2964-2978`); stale clients can be removed during cleanup
(`src/odb.cxx:3433-3445`, `src/midas.cxx:4987-5005`). Once the deferrer is
absent, a later START need not defer. If the sole deferrer disappears while
Requested Transition is pending, no other scheduler is guaranteed to resume
or reject it; there is no deferred-owner timeout in the checker. A pending
START and active flag can remain stranded. Canceling that request is not a
permanent absence interlock.

The existing standard absence check is the combination of
`/Programs/feeasiroc/Required=TRUE` and
`/Experiment/Prevent start on required progs=TRUE`
(`src/midas.cxx:4708-4746`). This checks presence, not deferred readiness or
per-device hardware safety. The repository currently intends the latter flag
to be false (`online/README.md:94-99`), and both fevme and feeasiroc have
Required=true in the Programs template. Simply enabling the global flag can
also prohibit previously supported single-frontend runs. Required-client
policy for each run mode and current actual values must be resolved before
claiming missing-frontend START prohibition. RPC failure/timeouts when an
already-selected client disappears can also fail a transition, but do not
replace a mandatory-presence policy.

#### 6. Lifecycle, unregister and cancel

Register once in frontend_init before accepting Apply, check success, and
reject frontend startup/Apply admission on registration failure. MFE registers
normal BOR at 500 before calling frontend_init (`src/mfe.cxx:2592-2629`). Keep
registration active while the frontend is alive, including idle periods, and
avoid per-Apply registration/de-registration windows. This inevitably defers
ordinary idle STARTs too in this implementation.

There is no public deferred-de-registration API in `include/midas.h:1708-1714`.
`cm_deregister_transition(TR_START)` removes normal START callbacks/keys
(`src/midas.cxx:3749-3793`), not the DEFERRED marker or mask/hotlink; using it
would remove the MFE BOR guard while leaving deferral registered. Normal
process exit/disconnect removes the client marker. The process-local deferred
mask/table is not reset by a dedicated deregistration routine; same-process
reconnect/repeated registration needs its own verification and should not be
treated as a supported per-operation switch.

Native Cancel Transition sets both Requested Transition and Transition in
progress to zero (`resources/mhttpd.js:3536-3555`). Clearing only Requested
Transition stops future matching checks after the hotlink is refreshed, but
can leave the START flag set and prevent later operations. Cancel is not
hardware rollback/cancellation of the already accepted synchronous Apply.
It must not replay ApplyRequestId, overwrite LastApplied or interrupt rollback.
After cancellation, first-state/cache refresh, deferred status display and the
run-number reservation require testing. Normal client removal does not cancel
the global pending request. On frontend restart, any residual deferred START
must be treated as stale, not silently auto-resumed on the new registration;
the startup recovery/cancel policy must avoid canceling another owner's work.

#### 7. GUI and return modes

During pending START, Runinfo State can remain STOPPED while Transition in
progress=TR_START, Requested Transition=TR_START and System/Transition/status
is 110. A FALSE callback keeps this state. TR_SYNC initiation returns 110 on
acceptance, not success of a completed run; TR_MTHREAD without TR_SYNC returns
CM_SUCCESS immediately (`src/midas.cxx:5402-5452`). JSON RPC defaults to
TR_MTHREAD (`src/mjsonrpc.cxx:3617-3626`). Thus HTTP success is not RUNNING,
and checking only the immediate RPC status cannot identify a pending or
failed deferred transition. Existing MIDAS transition threads are reused;
no new application worker thread is proposed.

Native `resources/transition.html:456-484` displays pending/deferred state and
a cancel action. Native `resources/start.html:85-89` and custom
`online/daq_monitor/web/daq.js:298` reject non-success immediate RPC statuses;
synchronous 110 needs handling, while default MTHREAD often returns 1 before
the eventual 110. The custom dashboard currently observes the active flag but
not Requested Transition and has no dedicated deferred/cancel model. Extend
its existing polling to Requested Transition, System/Transition/deferred,
status/error and confirmed RUNNING; keep Start/Apply disabled while pending,
and present pending, canceled, refused and running distinctly. Source files
were inspected only; no browser, port 8181 or served resources were verified.

#### 8. Smallest conditional implementation scope and mock plan

After selecting/fixing a suitable MIDAS revision and resolving absence and
single-deferrer policy:

- `feeasiroc.cxx`: one standard registration and level-triggered callback,
  common busy admission for both legacy/registered paths, no cm_yield inside
  synchronous hardware work, diagnostic joins outside ODB exclusion, retain
  early/normal BOR refusal and stale request recovery.
- `easiroc_odb.h/.cxx`: narrow RAII locks to claim/snapshot/publication and
  terminal persistence only. Publish ApplyInProgress before the first write;
  if that fails, perform zero hardware writes. Release before transport,
  sleeps, joins and rollback. Save result/LastApplied/uncertainty before
  releasing progress at terminal handling; preserve memory inhibition on
  publication failure. Registered-device current status publisher is empty
  (`easiroc_odb.cxx:1432-1436`), so narrowing scope alone is not correct.
- The short claim reads actual Runinfo State, Transition in progress and
  Requested Transition, registry/IP and local busy state. A START arriving
  first rejects Apply; a claimed Apply finishes before deferred START resumes.
  Iterate registered requests without one outer lock spanning all devices.
- Existing ApplyRequestId, immutable full-settings plan, one rollback and
  transmission-record LastApplied remain. HV never becomes a permission gate;
  BOR does not perform Apply. Web Save remains independent of Apply.
- `daq_monitor/web/daq.js` and existing design/test files: add deferred polling
  and cancellation/error presentation, not a new RPC/service.

Required offline tests, planned but **not executed** in this read-only task:

| Area | Required assertions |
| --- | --- |
| Current SDK behavior | Reproduce run-number R->R+1->R+2, initial 110 failure branch and missing resumed STARTABORT with a mock-backed harness of the actual SDK routines; test the selected fix against them |
| Registration | Once-per-process, failure before Apply, idle START also deferred, multiple START deferrers, duplicate registration/reconnect |
| Ordering | Apply-first/START-first, legacy and registered devices, pending Requested Transition blocks later Apply, callback runs only after synchronous work/result handling |
| Lock duration | Inject lock-state/clock hooks: writer, delays, join and rollback see no held ODB lock; claim/final publication see the short lock; evaluate recorded maximum critical-section duration |
| Failure | Claim publication failure gives zero writes; every write-failure position, one rollback, failed rollback and failed result publication inhibit START |
| Resume/refusal | Busy callback FALSE, ready TRUE, normal rejection with error and STARTABORT cleanup, no BOR auto-Apply, no permanent FALSE on uncertainty |
| Lifecycle/cancel | Required frontend absent before START, exit/crash/disconnect while pending, stale START/Apply on restart, cancel before/during/after readiness, clear both flags without replay or revival |
| Run/GUI | Default and explicit run numbers preserved once, canceled reservation policy, sync 110 vs async 1, deferred status/owner/error, RUNNING confirmation, cancel and recovery display |
| Regression | Existing DAQ, Settings/LastApplied isolation, Save-only zero hardware writes, HV-independent permission, full ASIC/InputDAC/Probe/Logic/Width plan |

A pure model does not replace testing the actual installed revision's SDK
control flow. Actual MIDAS shared-memory/registration, thread scheduling,
client removal and browser behavior remain unverified. Real MIDAS integration
would require a separately authorized isolated experiment/host session.

### Device identity, independent ODB mailboxes, and migration proposal

The existing `/Equipment/EASIROC/Settings`, `Commands` and `Variables` remain
at their old paths and retain the single-device DAQ ownership. The page
identifies this existing device as the reserved implicit ID `legacy` and reads
its address from `Settings/Network/IPAddress`. No Settings/mailbox provisioning keys are automatically
created for registered devices, and startup does not copy legacy settings.

Additional, explicitly provisioned devices use:

```text
/Equipment/EASIROC/Devices/<device-id>/
  IPAddress                            STRING, canonical IPv4
  Settings/Enabled                     BOOL
  Settings/ASICSlowControl/ApplyAtBOR   BOOL, must be false
  Settings/ASIC1/...                   existing eight ASIC fields
  Settings/ASIC2/...                   existing eight ASIC fields
  Settings/Probe/ASIC1/{Enable,Channel,Signal}
  Settings/Probe/ASIC2/{Enable,Channel,Signal}
  Settings/SelectableLogic/{Pattern,OneChannel,HitNumThreshold,AndChannels}
  Settings/TriggerWidth
  Commands/ASICSlowControl/ApplyRequestId
  Variables/ASICSlowControl/...        independent status and acknowledgement
  Variables/ASICSlowControl/LastApplied/{Valid,Extended,RequestId,ApplyUnixTime,...}
```

IDs are 1--31 ASCII letters/digits/underscore/hyphen; `legacy` is reserved.
IPAddress is separate metadata, never the ODB directory name. IPs must be
canonical dotted decimal, and duplicates (including the old DAQ IP) are
rejected. Do not retarget an ID to another physical device or reuse its
LastApplied history. Register a new ID/IP and retain the old history instead.
The frontend handles mailbox IDs independently and acknowledges requests
already present on first observation/restart without executing them. Request
IDs cannot wrap. Registry validation is repeated before Web Save/Apply.

Migration is a **proposal only**. `proposeLegacySlowControlMigration()` makes
an independent in-memory copy of legacy ASIC settings, adds the new desired
field defaults, and preserves the original LastApplied scope. It does not
invent a successful Probe/Logic/Width transmission. Ordered migration steps:

1. Export/backup the legacy Settings, status, LastApplied and outstanding IDs
   in an approved host session; validate all old InputDAC values against the
   new UI range before provisioning anything.
2. Inspect the pure proposal and choose stable IDs/unique IPs. Keep the old
   DAQ device at the implicit `legacy` path for now. Do not provision a second
   registered device at that same IP. Relocating the legacy device to an
   explicit ID needs a later acquisition-ownership/serialization change and
   is deliberately not performed by this implementation.
3. Provision extra-device Settings and independent mailbox/status keys only
   after separate ODB authorization. Defaults are desired values only:
   LastApplied.Valid=false for new devices. Preserve exported history for an
   existing device; reset/acknowledge pending requests, never replay them.
4. Validate READ, Save/readback, rejection, START exclusion and restart against
   an isolated ODB in a separately authorized session before hardware tests.
   This task changes sources and mocks only; it does not provision an ODB,
   run a frontend or enable a live service.

This leaves one DAQ route intact. `SlowControlDevice` holds each device's
settings, status and LastApplied independently so a later multi-device DAQ
implementation can add per-device acquisition ownership without conflating
IDs with addresses. Simultaneous multi-device DAQ is not implemented here.

### Desired Settings and complete offline Apply

#### Development ODB schema and editor investigation (2026-11-04)

Read-only HTTP `db_get_values` at 8181 identified `daq-dev`, with no
`feeasiroc` entry in `/System/Clients`. The legacy mailbox was request=3,
active=0, handled=1, successful=1, inProgress=false, state=Idle, error empty,
HardwareStateIndeterminate=false. This is an outstanding unacknowledged
request, not confirmation of a successful idle frontend. Sandbox process
visibility does not prove whether a host process exists. No service was
started or operated. At this observation, ASIC1 code was 601, ASIC2 code 600,
and both InputDAC[32] arrays were all 350. These are current settings, not a
new default or evidence of successful hardware writes.

`feeasiroc.cxx` accepts the legacy mailbox in
`accept_manual_apply_request()`, prepares a snapshot and executes the existing
synchronous backend in `process_manual_apply_request()`. Terminal status
publication resets ActiveRequestId/inProgress and publishes handled ID/error.
`read_status_event()` services registered devices then the legacy mailbox;
`frontend_loop()` itself is empty. Absent frontend registration means the
observed requests have no registered consumer. Startup acknowledges stale
requests without replay and marks hardware state indeterminate. Existing
backend terminal and rollback handling was not changed in this follow-up.

Web acknowledgement polling had a deadline outside an unbounded awaited ODB
promise. A hung request could prevent `applyPending` cleanup. The Web now
bounds each ODB RPC to 10 seconds and acknowledgement waiting to 45 seconds,
then unlocks local editing/READ while STOPPED. Re-Apply separately checks
fresh status, frontend registration, active/in-progress/pending IDs and any
ambiguous submission for the selected device. It displays the backend error
and indeterminate/START-inhibit state. These are browser wait limits, not
cancellation of hardware processing. A late terminal acknowledgement can
restore retry permission. Browser-only ambiguity records are lost on reload;
the persistent ODB mailbox remains authoritative for published requests.
Save/reread failure also bypassed UI cleanup when `loaded` became null; the
editor now retains Desired Settings and restores READ access in that case.
Apply remains disabled until the saved snapshot is confirmed. The periodic
`NIM-EASIROC Status` equipment services requests every 10000 ms in STOPPED,
RUNNING and PAUSED states; the backend still independently rejects unsafe
DAQ states before any Apply writes.

The ODB has ASIC1, ASIC2 and ASICSlowControl/ApplyAtBOR=false but no Probe,
SelectableLogic, TriggerWidth or Devices. For the existing IP
`192.168.10.40`, the minimal proposal keeps device ID `legacy` and the existing
single-device DAQ route; Devices migration is unnecessary. Add **all** of the
following leaves under `/Equipment/EASIROC/Settings` only in a separately
authorized ODB provisioning task. Intermediate directories are TID_KEY.

| Relative key | MIDAS type / length | Initial desired value |
| --- | --- | --- |
| Probe/ASIC1/Enable, Probe/ASIC2/Enable | BOOL scalar each | false |
| Probe/ASIC1/Channel, Probe/ASIC2/Channel | INT scalar each | 0 |
| Probe/ASIC1/Signal, Probe/ASIC2/Signal | INT scalar each | 4 (fast shaper) |
| SelectableLogic/Pattern | INT scalar | 3 (Or64) |
| SelectableLogic/OneChannel | INT scalar | 0 |
| SelectableLogic/HitNumThreshold | INT scalar | 1 |
| SelectableLogic/AndChannels | BOOL[64] | all false |
| TriggerWidth | INT scalar, ns | 118 |

There are 11 leaves: eight INT scalars, two BOOL scalars and one BOOL[64].
Preserve ASIC1/2, especially code 601/600 and both existing InputDAC arrays,
Network/IPAddress, Enabled, ApplyAtBOR=false, IDs, status and LastApplied.
No new command/status mailbox is needed for the legacy route. The Web does
not invent these keys or enable controls over an incomplete schema.

`readSettingsExtras()` detects any of the three new sections and then requires
the complete, correctly typed payload. Partial provisioning makes Settings
reads fail (including the normal DAQ preparation), so adding keys individually
while clients use the schema is not a safe provisioning procedure. A complete
payload makes the next explicit Apply include ASIC+Probe+Logic+Width; Save
and READ remain ODB-only, and BOR still does not auto-Apply. Adding desired
values must never fabricate successful LastApplied entries. On the first
extended Apply, an old ASIC-only LastApplied cannot roll back a failed full
plan; failure then remains HardwareStateIndeterminate and inhibits START.

For additional distinct IPs, `/Equipment/EASIROC/Devices/<id>` requires its
own IPAddress, complete Settings, Commands, status and LastApplied schema as
described above. IDs must be valid, unique and not `legacy`; IPs must be unique
including the legacy IP. Registering this same IP a second time is rejected.
Moving the DAQ device to an explicit ID is outside this minimal proposal.
No real ODB keys, outstanding requests, services or hardware were changed.

READ accepts a registered IP and loads ODB values only. An unknown/duplicate
IP leaves the current selection and Desired Settings intact. Polling updates
status and the ODB comparison without replacing Desired Settings. Save writes
only selected-device Settings, verifies all keys, and retains Desired Settings
on a partial failure. Apply rejects unsaved or remotely changed Settings.
Legacy READ/ASIC Save/Apply remain available. Extended controls are enabled
when all extended fields have been explicitly provisioned; no schema migration
is automatically performed by READ or startup.

| Field | Range/default | Meaning |
| --- | --- | --- |
| Probe Enable | BOOL per ASIC, false | Enables that ASIC's selected probe output |
| Probe Channel | local 0--31, default 0 | Converted to encoder global channel 0--63 |
| Probe Signal | 0--4, default 4 | PA HG, PA LG, slow shaper HG, slow shaper LG, fast shaper |
| Logic Pattern | 0--9, default 3 | Existing enum order from OneChannel through And32Or |
| OneChannel | 0--63, default 0 | Selected global trigger channel |
| HitNumThreshold | 0--64, default 1 | Hit-count threshold |
| AndChannels | BOOL[64], false | Channels participating in the AND mask |
| TriggerWidth | 0=Raw, otherwise 46--278 ns step 8; default 118 | Existing firmware width code |
| InputDAC | new UI/extended schema 256--511, default 350 | Per-channel input voltage/bias offset raw code; legacy ASIC-only backend keeps its original 0--511 format |
| DiscriminatorDACCode | 0--1023, default 600 | ASIC-wide discriminator threshold reference raw code |

InputDAC and threshold defaults come from existing `easiroc_run_settings.h`
and legacy site YAML. Missing voltage/charge calibration is not guessed.
All existing ASIC fields (including mask, slope, shaping, feedback) remain.

The complete plan contains 16 operations: seven existing ASIC shift/latch
operations, seven probe shift/latch operations using the same legacy sequence,
SelectableLogic at `0x78`, then TriggerWidth at `0x88` (14 writes, two 100 ms
delays). It never uses the aggregate legacy startup plan, which also writes
read registers, pedestals, time-window and other unrelated FPGA registers.
All values are validated and encoded before the first write.

The existing request-ID/state machine handles the immutable desired snapshot.
On a write failure it stops, then attempts exactly one full retransmission of
compatible valid LastApplied. A legacy ASIC-only record cannot restore a
failed extended plan. `RollbackAttempted`, `RollbackSucceeded` and
`RollbackError` are recorded alongside the apply failure. Rollback never
operates HV. Failure/unavailable rollback leaves hardware state indeterminate;
a successful retransmission establishes only a successful-write record, not
physical readback. LastApplied changes only after full desired Apply success;
failed Apply (including successful rollback) preserves LastApplied and desired
Settings. The historical stage descriptions below describe the original
ASIC-only schema; these current safety/rollback rules supersede their original
execution policy.

### Offline verification

`make` builds the frontend without running it; `make check` runs transport-free
C++ tests. `make check-web NODE=/path/to/node` runs JavaScript syntax and DOM/RPC
mocks without MIDAS. Node is a test tool only, not a frontend/Web dependency.
The new backend cases cover all 16 operation addresses/order, five probe
signals on both ASICs, ten logic patterns, all allowed widths, pre-write
validation, unsafe states, every one of 14 failing write positions with both
rollback outcomes, independent device history, migration and restart IDs.
The shared schema is roundtrip-tested, including missing fields and an invalid
BOOL array length. Web mocks cover unknown/duplicate IP, READ isolation, Save-only operations,
unsaved Apply refusal, extended values, ODB-change polling, partial Save errors,
HV-independent real-mode request posting, fresh transition checks and registry
changes. `easiroc_apply_odb_mock_test` compiles the actual ODB adapter against
an in-memory MIDAS API without linking libmidas or transport. It tests all four
HV cases, all unsafe DAQ states, full settings/LastApplied codecs (registered
and legacy), rollback results/START inhibition, and a competing thread writing
the START flag while the Apply lock is held. A source contract checks that BOR
contains no auto-Apply and that its inhibit check precedes acquisition access.
Real hardware safety/readback and real ODB integration of extended snapshots
remain unverified. The subsequent read-only Web check verified served-file
mapping and mock-page browser rendering, as described at the start of this
document; the full normal MIDAS header/menu rendering remains unverified.

## Primary sources

- `Controller.rb`: user commands, limits, ramp/check sequences, startup and
  shutdown behavior.
- `VME-EASIROC.rb`: register addresses, RBCP operations, delays, latch/apply
  sequences, monitor selection and conversion.
- `ConfigLoader.rb`: YAML merge, validation, aliases and 57-byte EASIROC
  bitstream encoding.
- `yaml/RegisterAttribute.yml`: field order, width, array scope, bit order and
  active-low attributes.
- `yaml/DefaultRegisterValue.yml`: default values.
- `yaml/RegisterValueAlias.yml`: symbolic-to-integer mappings.
- `yaml/RegisterValue.yml`, `InputDAC.yml`, `PedestalSuppression.yml`, and
  `Calibration.yml`: site settings and calibration coefficients.

## Register map used by Slow Control

| Address | Direction in controller | Purpose and format |
|---:|---|---|
| `0x00000000` | write | 3-byte direct-control value for two EASIROCs and start-cycle bits |
| `0x00000003` | write | EASIROC1 Slow Control/probe byte stream; normal SC is 57 bytes |
| `0x0000003c` | write | EASIROC1 high-gain read-register channel, one byte |
| `0x0000003d` | write | EASIROC2 Slow Control/probe byte stream; normal SC is 57 bytes |
| `0x00000076` | write | EASIROC2 high-gain read-register channel, one byte |
| `0x00000078` | write | selectable-logic block, 11 bytes |
| `0x00000088` | write | trigger width, one byte |
| `0x00000100` | write | time window, one 16-bit big-endian value |
| `0x00001000` | write | pedestal suppression, 128 16-bit big-endian values: 64 HG then 64 LG |
| `0x00010000` | write | HV DAC high byte |
| `0x00010001` | write | HV DAC low byte |
| `0x00010002` | write | HV DAC start command (`1`) |
| `0x00010003` | write | shutdown command (`0`) |
| `0x00010010` | write | monitor-ADC selection/control |
| `0x00010011` | write | monitor-ADC control/strobe |
| `0x00010012` | write | input-DAC monitor mux selection |
| `0x00010020` | read | monitor-ADC result high byte |
| `0x00010021` | read | monitor-ADC result low byte |
| `0x00010030` | write | user clock-output selection |
| `0x00010100` | write | trigger mode; `+1` receives three trigger-delay bytes |

The address definitions are at `VME-EASIROC.rb:423-485`; the individual write
methods are at `VME-EASIROC.rb:159-206,311-420`. The trigger-values call is
commented out in normal startup (`Controller.rb:661`), and `RegisterValue.yml`
states that these trigger values are unused in this version.

## High voltage

### Set point and DAC encoding

The controller command is `setHV <bias voltage>`, in volts
(`Controller.rb:72-75,569-603`). `sendHVControl` accepts inclusive
**0.00--90.00 V**; values below zero or above 90 return without writing
(`VME-EASIROC.rb:327-339`).

The conversion is:

```text
hvDAC = truncate(voltage_V * HVControl[0] + HVControl[1])
      = truncate(voltage_V * 413.9 + 747.8)
```

The coefficients come from `Calibration.yml:1-3` and are returned unchanged by
`ConfigLoader.rb:179-181`. With the supplied calibration this gives:

- 0 V -> 747 (`0x02eb`)
- 90 V -> 37998 (`0x946e`)

The code does not separately range-check `hvDAC`, but the configured voltage
range and current coefficients keep it within 16 bits. The write order is
(`VME-EASIROC.rb:341-350`):

1. `0x00010000` <- high 8 bits (`hvDAC / 256`);
2. `0x00010001` <- low 8 bits (`hvDAC % 256`);
3. `0x00010002` <- `1`, described as starting DAC control.

There is no delay between those three writes and no explicit clear of the
start value in this method. No reset is performed. The public `setHV` command
first calls `sendMadcControl`, which writes `248` to `0x00010010`, waits 10 ms,
and writes `0` to `0x00010011` to select a 50 Hz monitor-ADC rate; it then
programs the HV DAC (`Controller.rb:72-75`, `VME-EASIROC.rb:311-318`).

### Enable, disable and shutdown

The only separately named shutdown operation is `shutdownHV`, implemented as:

```text
0x00010003 <- 0
```

See `Controller.rb:68-70` and `VME-EASIROC.rb:320-325`. The supplied source has
no complementary `enableHV`, `turnOnHV`, or write of a nonzero value to this
address. It is therefore not possible to establish from this snapshot whether
setting the DAC implicitly enables the supply, what polarity/latching applies
to the shutdown register, or how a supply is re-enabled after shutdown.

Normal interactive exit and SIGINT/SIGTSTP handlers first call `setHV(0.0)`,
wait 0.2 seconds, then call `shutdownHV`, wait another 0.2 seconds, and exit
(`Controller.rb:752-770,793-804`). The general command exception handler only
calls `setHV(0.0)`, waits one second, and exits; it does not issue the separate
shutdown command (`Controller.rb:42-51`).

### Ramp and controller-side safety

`sendHVControl` itself performs an immediate set-point update; it contains no
ramp. Ramping exists only in the higher-level commands
`increaseHV`/`decreaseHV` (`Controller.rb:77-115`):

- `increaseHV(target)` accepts 0--90 V, steps through 0, 10, 20, ... V, sleeps
  one second after each write, calls `checkHV` with a voltage ceiling for the
  next step and a 20 uA current ceiling, sleeps 0.2 seconds, then writes the
  exact requested target and checks again.
- `decreaseHV(value)` reads the current HV, steps downward in 10 V increments
  with the same sleeps/checks, then writes 0 V. Its argument is used only for
  an initial `checkHV(value + 10, 20)`; the final target is always 0 V.
- `setHV` remains directly callable and bypasses ramp and readback checks.

`checkHV` defaults to an 80 V voltage limit, 20 uA current limit, and three
attempts (`Controller.rb:213-243`). A limit violation waits and retries; after
the attempt limit it calls `setHV(0.0)`, waits one second, and exits. It does
not issue `shutdownHV` in that branch. These are software checks, not evidence
of firmware/hardware interlocks.

### HV and status readback

HV readback is monitor channel 3; current is channel 4. The results are
displayed in V and uA respectively (`Controller.rb:140-150`). `readMadc` is not
a read-only register operation: it first performs several control writes:

1. `0x00010010` <- monitor selection (`3` for HV, `4` for current);
2. wait 10 ms;
3. `0x00010011` <- `1`;
4. `0x00010010` <- `240` to start conversion;
5. wait 50 ms;
6. `0x00010011` <- `0`;
7. wait 10 ms;
8. read high/low bytes at `0x00010020`/`0x00010021` and combine big-endian.

The conversions from `VME-EASIROC.rb:352-386` and `Calibration.yml:4-9` are:

```text
HV_V       = 0.00208 * raw16 + 0.0355
Current_uA = 0.0364  * raw16
```

### HV unknowns requiring hardware documentation/testing

- exact semantics, polarity and persistence of `0x00010003 <- 0`;
- how HV is enabled after shutdown;
- whether `0x00010002 <- 1` self-clears and its required timing;
- DAC saturation, resolution and behavior at 0 V;
- whether calibration coefficients are device-specific and current;
- hardware interlocks, trip behavior and safe power-up state;
- whether setpoint and shutdown writes are acknowledged only at RBCP level or
  after the physical action completes;
- safe ramp rate and settling time;
- monitor accuracy, error handling and stale/conversion-busy behavior.

## Monitor ADC and status functions

| Controller function | Selection | Reported unit | Conversion |
|---|---:|---|---|
| temperature 1 | 5 | deg C | `4500.0 * raw16 / 65535 / 2.4 - 273` |
| temperature 2 | 0 | deg C | same |
| EASIROC1 input DAC | 1 | V | `0.00006866 * raw16` |
| EASIROC2 input DAC | 2 | V | same |
| HV | 3 | V | `0.00208 * raw16 + 0.0355` |
| HV current | 4 | uA | `0.0364 * raw16` |

Sources are `Controller.rb:140-210`, `VME-EASIROC.rb:352-416`, and
`Calibration.yml:4-9`. Input-DAC monitoring first selects a local chip channel
0--31 through `0x00010012`; the mux byte is calculated by the even/odd formulas
in `setCh` (`VME-EASIROC.rb:389-416`). Global channel 0--31 uses monitor
selection 1 and 32--63 uses selection 2 (`Controller.rb:164-210`). Passing 32
to `setCh` writes mux value zero to deselect/reset the mux. This is a mux write,
not a hardware reset command.

The monitor operations require writes and conversion delays, so none were
executed during the present investigation. Firmware status beyond the already
verified firmware-version read is not exposed by this controller snapshot.

## Threshold and input-voltage settings

### EASIROC discriminator threshold

The underlying EASIROC field is controller name **`DAC code`**, a global
10-bit value per EASIROC, hence validated range **0--1023**. It is packed
LSB-to-MSB in the Slow Control stream (`RegisterAttribute.yml:100-103`). The
site configuration uses 600 for EASIROC1 and `same` for EASIROC2
(`RegisterValue.yml:1-13`); the default is 840
(`DefaultRegisterValue.yml:37`). `DAC slope` is a separate 1-bit global field,
with aliases fine=`1`, coarse=`0` (`RegisterValueAlias.yml:39-41`).

`Controller.rb` exposes `setThreshold(pe, chip="0", filename="temp")`, waits
0.5 seconds, then applies the complete Slow Control sequence
(`Controller.rb:258-262`). However, the called
`VmeEasiroc#setThreshold` method is absent from the supplied source. Therefore
the PE unit, allowed PE range, chip selector semantics, calibration-file use,
and PE-to-10-bit-DAC conversion cannot be confirmed. Only direct YAML `DAC
code` encoding is established.

### Per-channel input DAC / voltage offset

The controller field is **`Input 8-bit DAC`**, but the authoritative attribute
file assigns **9 bits per channel**, 32 entries per EASIROC, LSB-to-MSB, with
numeric validation range **0--511** (`RegisterAttribute.yml:8-12`,
`ConfigLoader.rb:452-480`). `InputDAC.yml` supplies 350 for each of 64 channels;
the default is 256. These values override the defaults while loading each
EASIROC (`ConfigLoader.rb:21-27`).

The help describes `setInputDAC` in volts with a nominal 0.0--4.5 V range, but
the invoked `VmeEasiroc#setInputDAC` method is absent
(`Controller.rb:246-250,600`). No voltage-to-9-bit-code conversion or input
range check is present in the supplied implementation. The only confirmed
voltage conversion is monitor readback `0.00006866 * raw16`; it is not evidence
for the write-side DAC conversion.

There is no field literally named `voltage offset`. The per-channel input DAC
is the closest controller feature, but its physical role must be confirmed
from NIM-EASIROC documentation or hardware testing.

## EASIROC Slow Control fields

Each EASIROC has a 57-byte (456-bit) encoded Slow Control image. Numeric fields
are validated as unsigned values according to their bit width. Symbolic values
are resolved through `RegisterValueAlias.yml`; whitespace is removed before
alias lookup (`ConfigLoader.rb:367-387`).

### Per-channel fields (32 entries per EASIROC)

| Controller name | Width/channel | Confirmed range | Notes |
|---|---:|---:|---|
| `Input 8-bit DAC` | 9 bits | 0--511 | LSB-to-MSB; despite the name, encoded width is 9 |
| `DisablePA & In_calib_EN` | 2 bits | 0--3 | MSB-to-LSB; combined meaning is not decoded further |
| `Discriminator Mask` | 1 bit | 0--1 | active-low before final stream transformation |

The attribute metadata says array size 32, and supplied YAML contains 32
entries. `validateRegisterValueSub` verifies that an array was provided and
that each element fits, but does not explicitly verify array length
(`ConfigLoader.rb:452-480`); architecture code should add this validation.

### Global analog/gain/shaper fields per EASIROC

| Controller name | Bits | Values/range confirmed in source |
|---|---:|---|
| `8-bit DAC reference` | 1 | internal=0, external=1 |
| `Low Gain PA bias` | 1 | highbias=0, weakbias=1 |
| `Capacitor HG PA Comp` | 4 | 0--15 |
| `Capacitor HG PA Fdbck` | 4 | NoC, 100 fF through 1.5 pF aliases |
| `Capacitor LG PA Fdbck` | 4 | same alias set |
| `Capacitor LG PA Comp` | 4 | 0--15 |
| `Time Constant LG Shaper` | 3 | 25, 50, 75, 100, 125, 150, 175 ns aliases |
| `Time Constant HG Shaper` | 3 | same aliases |
| `T&H bias(Widlar)` | 1 | highbias=0, weakbias=1 |
| `RS_or_discri` | 1 | trigger=0, RS=1 |
| `DAC code` | 10 | 0--1023 |
| `DAC slope` | 1 | coarse=0, fine=1 |
| `NC` | 4 | 0--15, nominally not connected |

The feedback-capacitor settings and PA bias/compensation are the only confirmed
gain-related controls. The source contains no command expressed as a numeric
gain and no formula mapping capacitor selection to gain. `changeHG.c` edits an
older `High Gain Channel` text key used for readout-channel selection, not an
analog gain value; its key does not match the current two-key YAML layout.

### Global power/enable fields per EASIROC

All are 1-bit and accept alias `Enable=1`, `Disable=0`:

- `High Gain PreAmplifier PP`, `EN_High_Gain_PA`;
- `Low Gain PreAmplifier PP`, `EN_Low_Gain_PA`;
- `Low Gain Slow Shaper PP`, `EN_Low_Gain_Slow Shaper`;
- `High Gain Slow Shaper PP`, `EN_High_Gain_Slow Shaper`;
- `Fast Shapers Follower PP`, `EN_Fast Shaper`, `Fast Shaper PP`;
- `T&H(Widlar SCA) PP`, `EN_T&H(Widlar SCA)`;
- `EN_discri`, `Discriminator PP`;
- `DAC PP`, `EN_DAC`;
- `BandGap PP`, `EN_BandGap`;
- `High Gain OTAq PP`, `EN_High Gain OTAq`;
- `Low Gain OTAq PP`, `EN_Low Gain OTAq`;
- `Probe OTAq PP`, `EN_Probe OTAq`;
- `LVDS receivers PP`, `EN_LVDS receivers`;
- `EN_out_dig`, `EN_OR32`, `EN_32_triggers`.

`EN_OR32` and `EN_32_triggers` are marked active-low in the attribute file;
`Discriminator Mask` and both shaper time constants are also active-low.
Power-pulsing (`PP`) physical semantics, safe combinations and dependency
rules are not validated by the controller.

No field is explicitly named as a TDC configuration field. TDC data sending is
controlled separately by the DAQ status byte, while discriminator, trigger and
time-window controls may affect timing behavior. Their exact relationship to
the TDC cannot be inferred from these names alone.

## FPGA-side trigger, selection and suppression settings

- Pedestal suppression: 64 HG plus 64 LG thresholds, each 0--4095. They are
  written as 128 consecutive big-endian 16-bit values from `0x00001000`.
- Selectable logic: 11 bytes at `0x00000078`; pattern codes 0--9, hit-count
  threshold 0--64, and a 64-channel AND mask are constructed in
  `ConfigLoader.rb:102-151`.
- Trigger width: `raw` -> 0; otherwise `(width_ns - 38) / 8`, with validation
  40--800 ns, written to `0x00000088`.
- Time window: string `<n>ns`, 0--4095, written as a big-endian 16-bit value to
  `0x00000100`.
- Trigger mode/delays: mode 0--7; delays `-1` or 1--253. Defaults `-1` become
  18, 8 and 13. Mode is written to `0x00010100` and the three delay bytes begin
  at `+1`, but normal startup does not call this method and YAML calls it unused.
- User clock output: OFF=0, ON=1, 1 Hz through 3 MHz map to 2--8 and are written
  to `0x00010030`.

These are FPGA-side controls included by the legacy `slowcontrol` command, but
they are distinct from the 57-byte EASIROC ASIC Slow Control images.

## Configuration loading, encoding and apply sequence

### Load and validation

`ConfigLoader` reads the protected attribute/default/alias files plus user
register values, per-channel input DACs, pedestal thresholds and calibration
(`ConfigLoader.rb:6-39`). EASIROC2 values equal to `same` inherit EASIROC1.
Defaults are overlaid by `RegisterValue.yml`, then by `InputDAC.yml`. It checks
known names, scalar/array types, numeric bit-width ranges, probe/read-register
channels, FPGA setting ranges and calibration types
(`ConfigLoader.rb:391-665`). Loading YAML changes memory only.

### 57-byte encoding

Fields are appended in `RegisterAttribute.yml` order into a 57-byte zeroed
array. For each field, `MSBtoLSB` causes bit reversal and `ActiveLow` causes
width-limited inversion. After packing, every byte's bit order is reversed and
the entire byte array is reversed (`ConfigLoader.rb:260-356`). This exact
transformation should be isolated in a pure encoder and covered by golden
offline vectors before future hardware use.

### Hardware apply sequence

`sendSlowControl` selects Slow Control for both chips and calls
`sendSlowControlSub` (`VME-EASIROC.rb:97-101,504-540`):

1. set both `loadSc=false`, `rstbSr=true`, and both start-cycle bits false;
2. write the 3-byte direct-control register at `0x00000000`;
3. write EASIROC1's 57 bytes at `0x00000003`;
4. write EASIROC2's 57 bytes at `0x0000003d`;
5. set both start-cycle bits true and write direct control;
6. wait 100 ms;
7. set both `loadSc=true`, clear start-cycle bits, write direct control;
8. set both `loadSc=false` and write direct control again.

This is an explicit start/latch/apply sequence. `rstbSr` remains true throughout
and is not pulsed, so the routine does not perform a Slow Control reset.
However, the top-level `slowcontrol` command then also sends probe selection,
calls `sendReadRegister` (which pulses `rstbRead` false then true), and writes
pedestal, selectable logic, trigger width, time window and user-clock settings
(`Controller.rb:268-278`, `VME-EASIROC.rb:103-216`). Thus the legacy aggregate
command is broader than ASIC Slow Control and does include a read-register
reset pulse.

## Missing or ambiguous behavior

- `VmeEasiroc#setThreshold`, `setInputDAC`, `setRegister`, and `getRegister` are
  called by `Controller.rb` but are not defined anywhere in this snapshot.
- PE-to-threshold-DAC and volt-to-input-DAC conversions are absent.
- HV enable/re-enable semantics are absent; only shutdown is named.
- There is no readback of the 57-byte ASIC configuration, FPGA Slow Control
  settings, requested HV DAC code, or shutdown state.
- No transactional rollback is defined for a multi-write failure.
- Array length 32 is metadata/default convention but not enforced by validation.
- Safe dependencies between power-pulsing and enable bits are not checked.
- Monitor ADC busy/status/error reporting is absent.
- Calibration provenance, validity range and per-board identity are absent.
- It is unknown whether configuration writes are safe while DAQ is active.

## Proposed MIDAS-independent architecture

The implementation should be layered so that pure preparation can be tested
without any path to hardware:

1. **RBCP transport**
   - finite-time `read(address,length)` and explicit `write(address,bytes)`;
   - no writes from constructor/destructor;
   - reply validation, retry policy and structured errors;
   - transport does not know HV or EASIROC semantics.
2. **NIM-EASIROC command/register encoding**
   - pure 57-byte ASIC encoder;
   - pure encoders producing ordered write plans for ASIC apply, FPGA settings,
     monitor conversion, HV DAC and shutdown;
   - address/value/delay steps are inspectable and offline-testable;
   - no transport reference in encoder objects.
3. **Slow Control policy/service**
   - validates a complete staged snapshot before execution;
   - tracks desired, last-applied and observed values separately;
   - requires an explicit Apply request;
   - serializes operations, blocks unsafe DAQ/config combinations, records
     partial failure, and never silently retries HV changes;
   - HV uses a dedicated state machine with hard 0--90 V limit, explicit enable
     authorization, configurable conservative ramp, readback checks, trip
     limits, abort-to-zero, and separate shutdown action. Details not confirmed
     by source must remain disabled/configuration-required.
4. **MIDAS ODB interface**
   - mirrors ODB to staged policy data;
   - changing Settings alone never calls transport;
   - consumes explicit Control requests, publishes operation state/errors, and
     updates Variables from confirmed readback/applied snapshots.

Before an Apply, software should freeze one settings snapshot, validate every
field and array length, calculate all bytes/write plans, show or log the plan,
ensure DAQ state permits it, then execute serially. Successful completion—not
the user's requested value—updates `LastApplied`. A failed multi-write sequence
must be marked indeterminate until explicitly recovered or re-applied.

## Recommended ODB layout

One possible layout is:

```text
/Equipment/NIM-EASIROC/
  Settings/
    EASIROC1/                 # staged global fields and 32-element arrays
    EASIROC2/
    PedestalSuppression/HG[64]
    PedestalSuppression/LG[64]
    SelectableLogic/...
    TriggerWidth_ns
    TimeWindow_ns
    UserClockOutput
    HV/RequestedVoltage_V
    HV/RampStep_V
    HV/RampDelay_ms
    HV/VoltageLimit_V
    HV/CurrentLimit_uA
  Variables/
    FirmwareVersion
    ConfigState               # NotApplied/Applying/Applied/Failed/Indeterminate
    LastAppliedRevision
    LastAppliedHash
    LastError
    HV/MeasuredVoltage_V
    HV/MeasuredCurrent_uA
    HV/RequestedVoltage_V
    HV/State                  # Unknown/Disabled/Ramping/Enabled/Trip/Fault
    Temperature1_C
    Temperature2_C
    InputDACMonitor_V[64]
    MonitorTimestamp
  Control/
    Apply                     # edge/command, not a continuously hot setting
    ApplyRevision             # binds Apply to an exact Settings snapshot
    Cancel
    HV/EnableRequest
    HV/DisableRequest
    HV/ApplyVoltage
    HV/EmergencyShutdown
    AcknowledgeFault
```

`Settings` are desired/staged values only. `Variables` are readback or confirmed
software state and should not be user-writable. `Control` contains explicit,
edge-triggered actions. In particular, editing `RequestedVoltage_V` must not
change hardware; HV requires a separate Apply action and, because enable
semantics are currently unknown, should remain disabled until that protocol is
confirmed. Control commands should carry a monotonically increasing request ID
or revision so reconnect/reload cannot replay stale `true` values.

## Same frontend versus separate Slow Control frontend

### Same frontend as physics readout

Advantages:

- one process owns RBCP and the device state, so register writes and DAQ
  transitions can be serialized directly;
- easy enforcement that configuration Apply occurs only outside active DAQ;
- readout and applied-configuration metadata can be associated consistently.

Costs/risks:

- monitor conversions contain writes and sleeps, so synchronous Slow Control
  can delay the physics event loop unless moved to a carefully synchronized
  worker;
- HV faults or Slow Control exceptions can affect the physics frontend;
- run transitions and explicit Apply/HV state machines become more coupled.

### Separate Slow Control frontend

Advantages:

- monitor polling, ramp delays and operator control do not block physics event
  processing;
- HV policy/fault handling can have an independent lifecycle and permissions;
- clearer ODB ownership for Settings/Variables/Control.

Costs/risks:

- two processes can issue RBCP operations to one device unless ownership is
  explicitly partitioned or coordinated;
- Slow Control must know whether physics DAQ is active before applying unsafe
  settings;
- startup/shutdown ordering, locks, stale-state recovery and cross-process
  failure handling are required.

A separate frontend should therefore be the sole owner of Slow Control/HV
registers, with physics owning only DAQ status/readout registers, or both should
use a single serialized device service. Two independent, uncoordinated RBCP
clients must not be allowed to write overlapping registers.

## Offline verification required before implementation is enabled

- golden vectors for all 57-byte fields, bit orders and active-low fields;
- exact 32-entry array validation and EASIROC1/EASIROC2 address plans;
- ASIC start/latch sequence and delay plan;
- threshold and input-DAC conversion tests once missing formulas are sourced;
- HV endpoint/rounding/range/write-order plans without transport execution;
- monitor selection/conversion plans and calibration boundary tests;
- Apply revision, no-auto-apply, partial-failure and restart/replay tests;
- proof that object construction and ODB Settings callbacks cannot write;
- fake-transport tests that record intended operations only.

No such future plan should be enabled against hardware until the missing HV
enable/shutdown semantics and device-specific calibration are confirmed.

## Implemented offline policy layer

The initial transport-independent implementation is in
`easiroc_slow_control.h/.cxx`. It contains no socket or RBCP object and cannot
execute a transaction.

- `EasirocSlowControlConfig` stores raw numeric ASIC fields. The confirmed raw
  threshold (`DAC code`, 10 bits) and per-channel Input DAC codes (32 x 9 bits)
  are supported without a physical-unit conversion.
- `EasirocSlowControlConfig::legacySiteDefaults()` is the baseline used by
  the current ODB-backed ASIC image encoder. It starts with the complete legacy
  `DefaultRegisterValue.yml` configuration and applies the confirmed site
  overrides from `RegisterValue.yml` and `InputDAC.yml`: DAC code 600, fine
  slope (1), all Input DAC codes 350, 100 fF HG/LG feedback, 100 ns HG shaping,
  and 50 ns LG shaping. The zero-initialized constructor remains useful for
  explicit tests but must not be used as a production ASIC configuration.
- `SlowControlEncoder` validates field widths/counts and produces exactly one
  57-byte image using the legacy field order, per-field bit order, active-low
  transformation, per-byte bit reversal, and final byte reversal.
- `SlowControlConfig` groups two ASIC configurations, probe/read-register
  selections, pedestal thresholds, selectable logic, trigger width, time
  window, and optional trigger mode/delays.
- `SlowControlPolicy::buildApplyPlan` produces an ordered vector of write and
  delay descriptions. A write contains a register address and bytes; a delay
  contains milliseconds. Neither type has an execute method.
- `SlowControlPolicy::encodeLegacySiteThresholdOverlay()` copies that baseline
  independently for ASIC1 and ASIC2, validates and replaces only the 10-bit
  DAC code and 1-bit DAC slope, and returns two complete encoded images. It
  does not mutate a shared baseline.
- `SlowControlPolicy::encodeLegacySiteAsicOverlay()` is a pure, per-ASIC
  helper that copies `legacySiteDefaults()`, overlays the discriminator DAC
  code/slope, all 32 9-bit Input DAC values, and HG/LG feedback capacitance
  and shaping time after conversion from physical units to the confirmed
  4-bit and 3-bit codes. It also maps each ChannelEnabled value to the logical
  discriminator mask; the existing encoder performs the ActiveLow wire
  conversion. It returns one complete 57-byte image through that encoder. The
  production frontend passes the immutable request Settings snapshot to it
  only from the STOPPED-only manual-apply backend; BOR never calls it.
- `SlowControlPolicy::buildAsicApplyPlan()` accepts two complete 57-byte images
  and emits only the seven ASIC shift/latch operations: initial direct control,
  ASIC1 write, ASIC2 write, start-cycle assertion, 100 ms delay, load/latch,
  and load release. It contains no probe, read-register, pedestal, selectable
  logic, trigger-width, time-window, or trigger-delay operation.

The aggregate plan follows the legacy `slowcontrol` order: ASIC image/latch,
probe image/latch, read-register reset/selection, pedestal suppression,
selectable logic, trigger width, and time window. Trigger mode/delays are added
only when explicitly present because the legacy normal startup leaves that
call disabled. This aggregate plan remains available for legacy/offline use;
manual apply uses only the ASIC-only plan, while BOR uses neither plan.

`easiroc_asic_apply` executes that seven-transaction plan through injected
write and delay functions. The production frontend binds them to
`RbcpClient::write()` and `sleep_for()`, while offline tests use mocks. A
failed transaction stops the sequence immediately. The manual backend now
attempts the single compatible-LastApplied retransmission described above;
it does not claim ASIC readback.

`easiroc_slow_control_test` compares both ASIC images for the supplied YAML
settings against a fixed 57-byte golden vector derived independently from the
legacy `ConfigLoader` algorithm. It also checks DAC code boundaries 0, 600,
and 1023, slope boundaries 0 and 1, exact threshold bit placement, and that no
non-threshold image bit changes. The ASIC-only seven-operation plan and the
larger aggregate plan are tested separately, along with raw Input DAC limits,
MSB-to-LSB and active-low effects, direct-control start/load transitions, probe
and read-register selection, big-endian pedestal/time-window data,
selectable-logic layout, trigger width, and default trigger delays. No ODB,
hardware readback, transport, or network operation is involved.

## ODB ASIC settings snapshot stage

The frontend defines discriminator settings in
`/Equipment/EASIROC/Settings`: `ASICSlowControl/ApplyAtBOR` (BOOL, false) and
`ASIC1`/`ASIC2` `DiscriminatorDACCode` (INT, 600) and
`DiscriminatorDACSlope` (INT, 1). Each ASIC also has
`HGFeedbackCapacitance` and `LGFeedbackCapacitance` (INT, default 100), stored
as physical fF rather than the non-linear ASIC code. The confirmed legacy
mapping is `0 fF (NoC)=0`, `100=8`, `200=4`, `300=12`, `400=2`, `500=10`,
`600=6`, `700=14`, `800=1`, `900=9`, `1000=5`, `1100=13`, `1200=3`,
`1300=11`, `1400=7`, and `1500 fF=15`. `HGShapingTime` and `LGShapingTime`
are physical INT times in ns, defaulting to legacy HG=100 ns and LG=50 ns;
their confirmed mapping is `25=1`, `50=2`, `75=3`, `100=4`, `125=5`,
`150=6`, and `175 ns=7`. Each ASIC also has `InputDAC`, a 32-element
INT array indexed by local channel 0--31, with every missing-key default set to
350. Existing keys are preserved and are never overwritten by initialization.
Slope 0 means coarse and slope 1 means fine.

`ChannelEnabled` is also a 32-element BOOL array per ASIC. `TRUE` means the
operator-facing unmasked/enabled state and is the legacy default for every
channel; `FALSE` means masked. It maps to the ASIC's one-bit-per-channel
`Discriminator Mask` logical field, where `0` means unmasked and `1` means
masked. This field is `ActiveLow`, so the encoder inverts the serialized bit
while retaining the logical meaning. The channel mask is independent of the
common discriminator threshold DAC: the DAC is shared per ASIC, while the
mask suppresses individual channel discriminators.

At BOR these values are read once into `FrontendSettings`. For an enabled
frontend, each discriminator DAC code must be 0--1023, each slope must be 0 or
1, each feedback and shaping-time value must be one of the confirmed physical choices above,
and every Input DAC value must fit its confirmed 9-bit field (0--511). A
disabled frontend skips validation of this unused hardware configuration. The
same local values, including both feedback and shaping-time values, both
32-element Input DAC arrays, and both ChannelEnabled arrays, are published
under `RunSnapshot/Requested`, and the RunSnapshot schema version is 8. Since
the ECFG bank serializes the complete RunSnapshot subtree, those requested
values are included in its JSON payload without separate ECFG logic.

`ApplyAtBOR` is retained as a deprecated compatibility field. It must be
false for an enabled frontend. If it is true, BOR rejects the setting before
creating acquisition TCP/RBCP connections, draining the data stream, issuing
DAQ ON, or writing ASIC slow-control registers. A disabled frontend continues
to ignore unused slow-control settings, including this deprecated field.

BOR never encodes an ASIC image, builds an ASIC apply plan, invokes the apply
executor, performs a slow-control RBCP write, or waits for the 100 ms apply
delay. Discriminator DAC and Input DAC values are run-independent
configuration: BOR only snapshots and validates them, publishes them under
`RunSnapshot/Requested`, and records that snapshot in ECFG. Hardware apply is
available only through the explicit manual command while the run is STOPPED.
The encoder, ASIC-only plan builder, and transaction executor are called only
from that manual path. Manual apply overlays both Feedback Capacitance, both
Shaping Time values, and all ChannelEnabled values from the same immutable
Settings snapshot used for threshold and Input DAC. Each ChannelEnabled value
is converted to the logical `Discriminator Mask` value (`TRUE` to 0, `FALSE`
to 1); the existing encoder alone performs the field's ActiveLow wire
inversion. The physical shaping times are converted to their 3-bit codes
before the complete 57-byte image is encoded. Successful LastApplied therefore
records exactly the snapshot used to create the transmitted images.

Older ODBs may retain the BOR-oriented `ApplyAttempted`,
`ApplySequenceSucceeded`, and `LastApplyRunNumber` keys. The frontend no longer
publishes or resets those obsolete Variables. `RunSnapshot/Apply` retains its
compatible per-BOR `Attempted`, `SequenceSucceeded`, and `Error` fields and
reports no BOR attempt. These fields never represent ASIC readback, and no
`Readback/ASIC` subtree is created.

## Manual apply request mailbox stage

The first manual-apply backend stage adds the monotonic `DWORD` request token
`/Equipment/EASIROC/Commands/ASICSlowControl/ApplyRequestId`. The frontend
polls it from the existing periodic Status equipment and handles a request only
when it is greater than `Variables/ASICSlowControl/LastHandledRequestId`.
Writing the same ID again cannot replay a request.

`Variables/ASICSlowControl` publishes `ActiveRequestId`,
`LastHandledRequestId`, `LastSuccessfulRequestId`, `ApplyState`,
`ApplyInProgress`, `LastAttemptSucceeded`, `LastApplyError`, and
`LastApplyUnixTime`. The supported state names are `Idle`, `Pending`,
`Applying`, `Succeeded`, `Failed`, `Rejected`, and `Indeterminate`. The older
BOR-oriented status keys are no longer published; the new fields in the same
subtree own manual command acknowledgement status.

On frontend startup, a request ID newer than the saved acknowledgement is
classified as stale, acknowledged as `Indeterminate`, and never executed.
This prevents a restart from replaying a persistent ODB command. New requests
are accepted into `Pending` only in the normal frontend context and are
rejected when the run is not STOPPED, the frontend is disabled, another apply
is active, or the Settings snapshot fails validation. Invalid shaping times
are therefore rejected before any hardware callback. A safe request snapshots
Settings once, enters `Applying`, overlays threshold, all Input DACs, HG/LG
feedback, HG/LG shaping, and all 32 ChannelEnabled values onto independent
legacy-site baselines, and executes only the seven ASIC shift/latch
transactions. Save to ODB and Apply are separate operations:
editing Settings alone never writes hardware.

All seven transactions must succeed before the request is acknowledged as
`Succeeded`. The transaction executor stops on failure; the manual backend
owns the single rollback attempt. A stale startup request remains
acknowledgement-only. Hardware Apply now requires STOPPED and no transition;
HV state/monitoring is deliberately excluded from its permission checks.

## LastApplied configuration record stage

`Settings` remains the desired, current ODB configuration. In contrast,
`Variables/ASICSlowControl/LastApplied` is a record of the **last successfully
transmitted configuration; not ASIC readback**. It contains `Valid`,
`RequestId`, `ApplyUnixTime`, the discriminator threshold, HG/LG feedback
capacitance, HG/LG shaping time, 32 Input DAC values, and ChannelEnabled for
both ASICs. Fresh keys are initialized with `Valid = FALSE`, zero
identifiers/timestamp, and zero-valued payload fields; existing ODB values are
preserved.

The frontend never copies Settings, legacy defaults, or nominal values into
LastApplied merely to populate it. Only successful completion of the manual
seven-transaction sequence updates it, using the immutable Settings snapshot
that produced the transmitted images. Rejected or failed requests leave the
previous LastApplied values and `LastSuccessfulRequestId` unchanged. Saving
new Settings values therefore never changes LastApplied.

`Variables/ASICSlowControl/ConfigurationMatch`, `ConfigurationStatus`, and
`ConfigurationDetail` compare desired Settings with the LastApplied record.
The status is `Unknown` when `Valid` is false, `Match` only when every ASIC
threshold/slope/feedback/shaping/InputDAC/ChannelEnabled value agrees, and
`Mismatch` with the first differing
field (for example `ASIC2 InputDAC[17] differs`) otherwise. `ApplyAtBOR` is
not compared because it is a deprecated policy field rather than an ASIC
hardware setting.

`HardwareStateIndeterminate` is kept distinct from LastApplied. A partial
manual-apply failure retains the previous successful LastApplied record and
sets this flag; then ConfigurationStatus is `Indeterminate`, never `Match`.
The backend now attempts one compatible LastApplied retransmission. A fully
successful Apply or rollback clears this flag as a transmission-state result,
without claiming ASIC readback.

## BOR consistency snapshot stage

BOR still performs no slow-control hardware write. After taking and validating
the requested Settings snapshot, it takes one independent snapshot of
LastApplied and `HardwareStateIndeterminate`, then derives a fixed consistency
record with `compareAsicSlowControlSettings()`. It does not reuse the live
Variables status and never rereads either ODB subtree for that run.

`RunSnapshot/Consistency` contains `LastAppliedValid`,
`ConfigurationMatch`, `HardwareStateIndeterminate`, `LastAppliedRequestId`,
`LastAppliedUnixTime`, `Status`, and `Detail`. `Requested` already preserves
the complete desired configuration, so LastApplied's 64 Input DAC values are
not duplicated. SchemaVersion 8 causes the existing full-RunSnapshot ECFG JSON
serialization to include this subtree without ECFG-specific logic.

Only a valid, non-indeterminate and exactly equal LastApplied record is
`Match`. An invalid record is `Unknown`; a difference is `Mismatch`; and a
hardware-state uncertainty is `Indeterminate`, even if the stored values
match. Unknown/mismatched Settings versus LastApplied alone do not reject BOR.
HardwareStateIndeterminate or an active Apply does reject START before
acquisition access. For enabled runs, other non-matches emit a BOR warning;
disabled runs emit no such warning. `ApplyAtBOR=true`
continues to be rejected before any hardware access and before normal DAQ
startup.
