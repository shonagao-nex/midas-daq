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

## HV Control development stage (2026-11-04)

This section supersedes older HV proposals above (including 0--90 V, separate
services and emergency-shutdown commands). Scope is development sources under
`/home/nagao/midas/midas/online` only. No frontend, service, ODB client, hardware
probe or served Web request was run for this task. Port 8181, experiment
`daq-dev`, `/Custom/Path` and served asset identity are historical observations
above, **not revalidated in this no-ODB-access task**. Port 8081 and `/home/daq`
were not accessed. GUI validation used only the local files with DOM/RPC mocks.

### Evidence and unresolved safety conditions

Primary external reference: [EASIROC MODULE User Guide, March 18 2014 v1.0](https://research.kek.jp/people/isamun/easiroc/manual.pdf),
sections 1.3.2, 2.2.2 and 2.3 (printed pp. 7, 14--15), retrieved 2026-11-04.
The guide identifies the monitor current as DC-DC converter current rather
than MPPC current, requires CH=32 (No Connect) for measurement because the MUX
influences measurements, and describes physical HV routing jumpers. It warns
about ASIC damage from inappropriate bias relative to the sensor voltage.
It does not resolve the shutdown polarity/re-enable protocol for this firmware.

The authoritative source evidence remains `reference/controller_20170425/`:

- `Controller.rb:72-75`, `VME-EASIROC.rb:311-350`: prepare MADC with
  `0x10010 <- 248`, delay 10 ms, `0x10011 <- 0`; set DAC high/low at
  `0x10000/0x10001` then `0x10002 <- 1`. Calibration is
  `truncate(413.9 * V + 747.8)` (`yaml/Calibration.yml:1-3`). Our stricter
  requested range is 0--60 V: endpoint codes 747 and 25581. These writes
  do not prove that the supply is enabled or that the voltage settled.
- `sendShutdownHV` only writes `0x10003 <- 0`. No verified counterpart,
  enable-state readback, power-up state or setpoint readback was found.
  Do not guess `0x10003 <- 1` or infer OFF from measured zero voltage.
- `readMadc`, lines 352--386: select channel 3 (voltage) or 4 (current) at
  `0x10010`; delay 10 ms; `0x10011 <- 1`; `0x10010 <- 240`; delay 50 ms;
  `0x10011 <- 0`; delay 10 ms; read separate high/low bytes from
  `0x10020/0x10021`. Convert big-endian unsigned raw16 with
  `V = 0.00208 * raw16 + 0.0355`, `I_uA = 0.0364 * raw16`.
  Units are confirmed by `Controller.rb:140-150`; coefficients by
  `Calibration.yml:4-7`. The mathematical current span is 0--2385.474 µA;
  it is NOT an established safe operating/protection range for a detector.
- `setCh(32)`, lines 389--416, writes zero to `0x10012`. The offline protocol
  helper disconnects the MUX before sampling and leaves it disconnected after
  the pair, with 10 ms settling after each MUX write. It does not restore an
  arbitrary previously selected input. Legacy `statusInputDAC` disconnects at
  normal completion but lacks exception-safe restoration. A failed transaction
  in the new helper stops immediately; no speculative cleanup writes follow.
- `Controller.rb:213-243` performs software threshold retries and eventually
  writes zero then exits. Its default 20 µA is not a device-specific safety
  certification and is NOT adopted as a protection default. Ruby RBCP uses
  up to three one-second attempts and raises on failed replies; a lost ACK
  cannot prove a write did not occur. HV exceptions cannot establish OFF.
- The Ruby DAQ read path allows status commands during TCP acquisition, but
  this does not establish noninterference. MADC monitoring performs control
  and MUX writes; sharing UDP command state, MUX and physical hardware with
  acquisition is a potential conflict. The new policy prohibits all HV access
  in RUNNING, PAUSED, transitions and unknown run states. It adds no START
  lock or guarantee to the established transition mechanism.

Device-specific calibration validity, enable/re-enable and state discovery,
near-zero voltage tolerance, firmware timing/physical tracking behavior and
suitable detector CurrentLimit remain unresolved. All actual HV access,
including MADC monitoring (which requires writes), remains disabled by code.
There is no ODB flag to unlock it and no production transport binding to the
HV protocol/policy helpers. An independent specification/hardware validation
and a reviewed integration step are required before enabling them. Thus this
stage does **not** deliver operational hardware HV control or periodic samples.

### Implemented source boundaries

`easiroc_hv.{h,cxx}` supplies socket-free protocol helpers and an injected
policy backend, exercised only by offline tests. Constructors perform no I/O.
The policy requires a registered IP context, STOPPED/no transition, verified
hardware semantics and verified calibration. It starts unknown, with no
assumed enabled state or setpoint. Enable additionally checks successful ASIC
configuration, enabled InputDAC and known zero setpoint. Disable additionally
checks a fresh near-zero measurement against an explicitly supplied, verified
tolerance (default NaN rejects). Production never supplies verified context or
an enable callback, and never adopts persistent ODB values as initial hardware
truth.

The policy advances normal ramps by at most 2 V per one-second tick, up or
down. Overcurrent while ramping latches a zero target and uses 10 V steps;
steady overcurrent reports WARNING without changing voltage. It samples
before each step, samples every 30 seconds when steady, and never catches up
missed steps in a burst. Ramp timing gaps over two seconds, time reversal,
invalid samples or callback failures invalidate state, stop the ramp and
latch a fault and perform no further monitor or output callbacks until a
verified state recovery creates a new policy state. Final write completion invalidates the
sample until another measurement: successful transmission is not voltage
readback. Freshness windows are two seconds during ramp and 65 seconds steady;
these are software freshness policies, not hardware tolerance claims. Current
limits must be finite and positive within the legacy ADC representation; zero
is unconfigured, and does not activate provisional protection.

`process_hv_requests()` is called only from the existing status callback. It
uses the existing legacy IP and Devices registry and existing canonical-IP /
duplicate validation. It adds no hardware callbacks, new service or RPC. Each
registered base (legacy `/Equipment/EASIROC` or
`/Equipment/EASIROC/Devices/<id>`) independently owns:

- `Settings/HV/CurrentLimit_uA` (DOUBLE, missing default 0 = unconfigured).
  Existing values are preserved, including across requests; HV never rewrites
  ASIC Settings, LastApplied or ASIC ApplyRequestId.
- `Commands/HV/{RequestId,Operation,TargetVoltage_V,Enable}`. RequestId is an
  ordered DWORD; Operation is a string, target DOUBLE and Enable BOOL. These
  are a reserved independent request schema; this release rejects **every**
  request and the browser does not submit them. A future executable mailbox
  will need immutable snapshot/commit ownership and restart semantics reviewed
  together with the missing physical state discovery.
- `Variables/HV/{LastHandledRequestId,Result,Error,ResultUnixTime}`. Higher
  request IDs receive Rejected, including preexisting requests on restart;
  acknowledgement is published last only if result publication succeeds.
  Repeated IDs do not replay. Invalid/duplicate registry is explicitly reported.
- `Variables/HV/{EnabledState,SetVoltageState,RampStatus}` remain Unknown;
  `OperationsAvailable`, `MeasurementValid`, `ProtectionActive` remain false.
  Historical `MeasuredVoltage_V`, `MeasuredCurrent_uA`, `LastUpdateUnixTime`
  are preserved but marked invalid. `BlockedReason` explains missing evidence.

The old status callback remains at ten seconds because this adapter never
runs a ramp or monitor. The one-second/30-second scheduling policy is tested
in isolation; enabling real scheduling without resolving START exclusion is
not part of this release. MIDAS core, ODB locking, Deferred Transition, START
exclusion and the ASIC Apply implementation are unchanged.

The existing page gains an independent final HV panel after a divider, keeping
all existing controls/layout. It reads only cached device-specific ODB fields;
refresh during a run never requests MADC access. Missing, stale, future-dated
or unavailable measurements display Unknown, with last measurement time shown
separately. Valid current above a configured limit is red, even while protection
is inactive. Enable and target controls remain disabled in every real/mock
page because the hardware evidence is unresolved. Guarded handlers have
OK/Cancel confirmation wording with IP and requested value, check fresh DAQ
state/registration, and reject every operation even after OK or client-side
manipulation. They never send ASIC Apply or direct network traffic. The dialog
code is tested programmatically; normal disabled buttons cannot open a dialog.
CurrentLimit is edited through ODB Settings independently; this page does not
provide a provisional limit editor or a new Monitor page.

### Offline verification and limits

`make -C easiroc_frontend -j4 all check` passes with no compiler warnings or
errors. New `easiroc_hv_test` has 62 checks: endpoint conversion, protocol/MUX
ordering, no cleanup after failed writes, Enable preconditions, both ramp
directions, emergency latching, steady warning, monitor cadence, stale data,
DAQ/transition guards, scheduling gaps and communication failure. Existing
ODB adapter mock now has 1590 checks including independent HV IDs, preserved
CurrentLimit/ASIC request, duplicate IP rejection and failed-publication
acknowledgement. Existing ASIC, readout and START exclusion tests still pass.

The initial `make check-web` could not find `node` in PATH. Using the already
available development-local binary succeeds:
`make -C easiroc_frontend check-web NODE=./build/tools/node-v22.16.0-linux-x64/bin/node`.
Syntax and 94 DOM/RPC checks pass, including Unknown/stale/current color,
disabled controls, Cancel/OK, RUNNING/PAUSED/transition/registry rejection and
ASIC request isolation. These are simulated DOM checks, not a real-browser
layout or served-file/deployment validation. No hardware communications,
production changes, ODB modifications or service actions were performed.

## HV pre-enablement specification audit (2026-11-04, read-only investigation)

This follow-up inspected source and public documentation only. The sole output
edits are this existing document and the requested final-report `codex.log`.
No source, reference file, ODB, service, hardware or production file was changed;
no build, tests, frontend launch, MIDAS connection, reset or HV operation was
performed. Earlier test results above are historical, not rerun results. The
working tree already contained the preceding HV implementation changes.

### 1. Confirmed control behavior and evidence

Line references below refer to the development files as inspected today.
`R/` abbreviates `reference/controller_20170425/`.

| Item | Confirmed software behavior | Evidence |
|---|---|---|
| Public set command | `setHV` prepares MADC then writes the HV DAC; it does not call an Enable routine | `R/Controller.rb:68-75` |
| MADC preparation | `0x10010 <- 248`, wait 10 ms, `0x10011 <- 0`; comment calls this 50 Hz | `R/VME-EASIROC.rb:311-318` |
| DAC encoding/order | `floor(413.9*V+747.8)` for nonnegative V; high byte `0x10000`, low byte `0x10001`, then `0x10002 <- 1`, no intervening explicit delay | `R/VME-EASIROC.rb:327-350,464-474`; `R/yaml/Calibration.yml:1-3` |
| Requested voltage limits | Ruby accepts 0--90 V; new policy validates 0--60 V; 0 V is code 747, 60 V code 25581 | same Ruby method; `easiroc_hv.cxx:6-8` |
| Shutdown | `shutdownHV` calls `sendShutdownHV`, which writes `0x10003 <- 0`; this establishes the command bytes, not electrical polarity/latch behavior | `R/Controller.rb:68-70`; `R/VME-EASIROC.rb:320-325` |
| Explicit ON/state readback | No dedicated ON/re-enable command or Enable/shutdown/setpoint register readback is implemented; `statusHV` reads analog voltage/current only | `R/Controller.rb:17-22,140-150`; `R/VME-EASIROC.rb:311-416` |
| Controlled exit | Signal/interactive-exit paths send zero, wait 200 ms, shutdown, wait 200 ms; they do not confirm measured zero | `R/Controller.rb:752-770,793-804` |
| Nonuniform exit protection | Exception dispatcher tries zero then exits; `-e ... -q` exits before interactive cleanup. These paths cannot justify a universal safe shutdown guarantee | `R/Controller.rb:42-51,679-686` |
| Controller startup | Constructor reloads ASIC settings and exits DAQ mode. Main sends ASIC/probe/etc.; optional `.rc` commands are executed. No unconditional HV initialization establishes OFF | `R/VME-EASIROC.rb:14-24`; `R/Controller.rb:652-677`. No `.rc` is supplied in this reference snapshot |
| Frontend lifecycle | `frontend_init/exit` do not call HV write/Enable helpers; acquisition cleanup is not HV shutdown. Software restart provides no proof of the physical HV state | `feeasiroc.cxx:900-947`; production call-site search for HV policy helpers |

The nominal DAC command increment is about `1/413.9 = 2.416 mV/code`, derived
from the supplied calibration, not a hardware accuracy specification. The
MADC conversion is a separate calibration path; no identity between DAC code
and measured voltage, zero residual, settling time or load-independent output
is established. Common bias is also distinct from per-channel bias after
InputDAC. A command ACK proves neither physical settling nor Enabled/OFF.

The primary external source is [EASIROC MODULE User Guide, March 18 2014
v1.0](https://research.kek.jp/people/isamun/easiroc/manual.pdf), reviewed in full.
Printed p. 7 describes internal/external HV routing jumpers and a voltage LED
threshold of 5 V. Neither constitutes a digital Enable-state readback; LED
absence cannot establish near-zero output. Printed p. 14 requires MUX CH=32
(No Connect) during measurement and identifies current as DC-DC converter
current, not individual MPPC current. Printed p. 15 describes a sensor-relative
bias restriction and ASIC damage risk. The guide does not specify the numerical
normal DC-DC current interval, zero-voltage noise/tolerance, re-enable protocol,
or reset/power-up HV state needed here. Its physics ADC description does not
establish the separate MADC's effective resolution. The reference README line
6 identifies controller firmware 2016-05-23, whereas the guide is from 2014;
installed-board/firmware compatibility is not verified by this investigation.

### 2. MADC, zero detection and current protection limits

`R/VME-EASIROC.rb:352-386` selects voltage channel 3 or current channel 4 via
`0x10010`, waits 10 ms, writes `0x10011=1`, writes `0x10010=240`, waits 50 ms,
writes `0x10011=0`, waits 10 ms, reads separate high/low bytes at
`0x10020/0x10021` and combines them unsigned big-endian (`high*256+low`).
Thus the confirmed **software representation** is 16 bit, 0--65535. Actual
ADC model, usable bits, effective resolution, accuracy, saturation behavior
and conversion-ready indication are not established by the supplied material.
Do not conflate the event-data 12-bit ADC with this MADC format.

`R/yaml/Calibration.yml:5-7` and `R/Controller.rb:140-150` establish:

- `V = 0.00208*raw + 0.0355 V`: nominal 2.08 mV/count; raw zero maps to
  +35.5 mV. This intercept is a conversion coefficient, not measured zero
  noise or an OFF tolerance. No fit uncertainty, zero sample distribution,
  temperature drift or confidence interval is supplied. Even the source
  comments contain alternative coefficients without provenance.
- `I = 0.0364*raw µA`: nominal 0.0364 µA/count (36.4 nA/count). The arithmetic
  span is 0--2385.474 µA. This is not a certified analog linear range or safe
  load range. Current offset is omitted in the legacy formula; absence of an
  offset term is not proof of zero offset in hardware.
- For CurrentLimit storage/validation, retain zero as an explicit unconfigured
  sentinel and reject nonfinite/negative values. A candidate representational
  protection threshold must satisfy `0 < limit < 2385.474 µA`; actual deployment
  must further restrict it below the verified linear/safe-current maximum with
  calibration/noise/saturation headroom. The current code permits equality at
  full scale (`easiroc_hv.cxx:88`, `easiroc_hv.h:9`), but its strict `I>limit`
  comparison then cannot trip on an in-range sample. Full-scale samples are
  presently accepted by `validSample` (`easiroc_hv.cxx:35-37`); clipping must
  not be mistaken for evidence that current is below the limit.
- Operational selection must establish an interval above the validated maximum
  normal current plus measurement margin and below the verified safe current
  and measurable linear ceiling minus margin. No numeric operational interval
  can be selected from these files. Ruby's default 20 µA
  (`R/Controller.rb:213-243`) is a legacy software setting, not a specification
  of normal current or guaranteed safe current.
- `CurrentLimit=0` is suitable as an unconfigured sentinel: the new policy
  rejects it before calling its monitor/output backend (`easiroc_hv.cxx:88`).
  Before enabling hardware, a valid nonzero device-specific threshold must be
  an admission condition for Enable and voltage requests themselves, not just
  a later tick check. ProtectionActive must remain false until fully validated.
- Calibration loading uses one common YAML path independent of IP
  (`R/VME-EASIROC.rb:219-228`). `ConfigLoader.rb:637-663` checks structure and
  Float types, not validity/provenance/serial identity. Alternative commented
  gains/offsets show that values have alternatives, but do not prove whether
  differences are per-device, revision or historical. Per-device differences
  are possible and must be measured/identified rather than assumed absent.

No justified near-zero numeric tolerance can be selected. A future tolerance
must account for independently verified residual physical voltage, offset,
noise, calibration error and discharge settling, and require fresh repeated
measurements as specified by the approved test procedure. The mock's 0.1 V
context and 0.04 V sample (`tests/easiroc_hv_test.cxx:32-34`) are test data,
not recommended operating values. Physical zero, a zero command and digital
Disable must be recorded as separate facts.

`setCh(32)` writes `0x10012=0` and waits 10 ms
(`R/VME-EASIROC.rb:389-416`). After selecting an InputDAC channel for measurement,
`statusInputDAC` restores CH=32 on its normal completion at
`R/Controller.rb:164-210`; it has no finally/ensure restoration. `statusHV`
itself does not select/restore MUX. The guide does not establish that every
HV pair needs two redundant writes when the MUX is already disconnected, or
that the MADC selector must be restored to a particular previous channel.
The new helper's before/after CH=32 writes (`easiroc_hv.cxx:40-45`) are a
conservative software policy; after failure it issues no extra cleanup writes
and MUX state must remain unknown. Do not call this an unconditional successful
restoration guarantee.

The legacy DAQ monitor command thread admits statusHV/statusInputDAC while TCP
readout runs (`R/Controller.rb:388-415`). This is evidence of software usage,
not proof of electrical/timing noninterference. Monitoring performs control/MUX
writes, can change analog connections and occupies communication time. The
confirmed HV address set does not serialize ASIC images or touch ASIC Apply,
but that alone cannot exclude analog effects on acquisition. Keep RUNNING,
PAUSED and transitions blocked, and serialize hardware ownership inside the
existing single frontend. No change to MIDAS core/ODB locks/Deferred Transition
or START exclusion is proposed.

### 3. Current implementation and minimum changes before enablement

The setVoltage MADC preparation and DAC-byte order match legacy setHV exactly;
measure matches per-channel control/read order and adds the explicit MUX
policy above (`easiroc_hv.cxx:13-50`). No shutdown or Enable transport function
is implemented. `Backend::enable` is only an injected callback
(`easiroc_hv.h:39-43`).

Current blocks are independent and deliberate:

- `easiroc_hv.h:25-35`: verification flags, known-state flags default false,
  near-zero tolerance defaults NaN; `easiroc_hv.cxx:21-33,55-75` rejects unsafe
  contexts, unknown states and latched faults.
- `easiroc_odb.cxx:1510-1578`: per-device adapter exposes no transport,
  publishes OperationsAvailable/MeasurementValid/ProtectionActive=false,
  state Unknown, and rejects every new request. It neither reads an executable
  request payload into the policy nor owns per-device ramp state.
- `feeasiroc.cxx:883-887,1424-1427`: only this rejection adapter is called from
  the ten-second status equipment; `frontend_loop` is empty at line 1318.
  There are no production callers of measure/setVoltage/requestEnable/tick.
- `daq_monitor/web/easiroc.html:128-132` and `easiroc.js:619-656`: controls are
  always disabled and the handler unconditionally throws after confirmation;
  it has no HV mailbox submission/result-wait implementation. Polling ODB
  fields does not initiate MADC monitoring.

A Boolean unlock or binding RbcpClient alone is therefore insufficient. Minimum
future source work, after specifications are resolved, is confined to:

1. HV module: verified Enable/Disable commands and startup state reconciliation;
   per-device calibration/provenance, measured-zero criteria, valid-limit
   admission checks, saturation handling and post-write settling verification.
   Keep unknown state on uncertain ACK or restart; never copy saved ODB targets
   into actual hardware state. No invented enable register value.
2. Existing ODB adapter/frontend: immutable device+IP request snapshots, ordered
   request/ack status, reject stale restart requests, per-device policy state,
   serialized hardware callbacks and a bounded one-second scheduling path with
   30-second steady cadence. Separate monotonic scheduling from wall-clock
   publication; timestamp completed samples, not the pre-I/O tick time. Existing
   ten-second callback cannot drive one-second ramps. Budget conversion time
   across multiple devices. Reuse existing registration/DAQ-state exclusion
   contracts; if those cannot guarantee the required boundary without changing
   the prohibited mechanisms, keep the affected operation disabled.
3. Existing Web handler: independent HV payload/RequestId submission, request
   result tracking, registry and fresh STOPPED/transition checks, OK/Cancel,
   backend capability gating and staged target preservation (current refresh
   assigns the target input every time). Preserve Settings/HV/CurrentLimit,
   separate device identities and ASIC mailbox/Save/Apply exactly.

These are future changes, not implemented or authorized for execution here.
Firmware register/latch/default semantics, converter discharge capabilities and
ADC identity need board-version-matched firmware/schematics or designer
confirmation. This reference snapshot contains no such firmware/schematics.
Real-device startup/restart persistence, zero noise/drift, current baselines,
per-device gain/offset, settling and analog interference require controlled
measurements. Documentation may resolve semantics; measurements are still
needed to validate the installed devices.

The existing overcurrent mock (`easiroc_hv.cxx:77-104`) is **not ready for
hardware protection**. It measures before writing and subtracts at most 10 V
from the remembered setpoint per eligible tick. It is not a continuous 10 V/s
physical slew and cannot establish the supply's sinking/discharge behavior.
A 60 V commanded decrease can take six ideal one-second steps plus detection
and communication delays; this may be too slow for damage prevention. It
also leaves up to the next tick before observing a surge caused by a write.
On reaching the final target it immediately stops ramp monitoring, invalidates
the prior sample and waits for the 30-second steady cadence. An overshoot or
current rise caused by that final write could therefore be missed during the
ramp and later receive WARNING only. Completion must instead be gated by
post-write measurements/settling before leaving ramp protection.

Both legacy RBCP (`R/RBCP.rb:106-140`) and C++ RBCP
(`rbcp.cxx:21-22,169-175,247-251`) may attempt a transaction three times, with a
one-second receive timeout per attempt. A control strobe may execute despite
a missing ACK and be repeated. The mock fault latch prevents subsequent
callbacks after an exception, but cannot cancel retries already inside an
injected transport callback or ensure no physical write occurred. Validate
idempotence and bounded timing before adopting this transport for HV; an
HV-specific non-retrying/bounded mode would need separate review confined to
the HV/transport layer, leaving ASIC transport behavior untouched. Do not add
automatic zero/shutdown/reset recovery after communications become uncertain.

### 4. Proposed staged hardware validation (not executed)

Each stage requires separate explicit authorization, an approved procedure,
a device/firmware identity, appropriate instruments and defined stop criteria.
Hardware/DAQ-host operations must not be launched from this sandbox.

1. Obtain matching firmware/schematics or designer confirmation for shutdown,
   re-enable, power-up defaults, DAC strobe and MADC behavior. Resolve routing,
   safe limits and recovery procedure before any unknown register experiment.
   Complete the above source work and offline failure/cadence/IP isolation
   tests before connecting its backend.
2. With sensors disconnected and an approved rated test load/instrument setup,
   establish physical HV and routing independently. Under an approved power-up/
   restart procedure, distinguish board power state from software restart and
   verify the documented zero/shutdown/re-enable path. Do not infer OFF from
   LED, process exit, stored ODB state or raw zero.
3. At independently confirmed zero, collect repeated raw voltage/current data
   over the relevant settling/temperature conditions per device, with MUX
   disconnected. Determine offsets, uncertainty and near-zero criteria; then
   characterize gain with approved low-voltage/load points and reference meters.
4. Establish normal-current envelope, safe load ceiling and saturation margin;
   approve a nonzero per-device CurrentLimit. Validate small low-voltage up/down
   steps first, then the requested 2 V/s and 10 V/s policy only within the
   established safe test envelope. Verify final-step current/voltage settling
   and controlled threshold crossing with test loads. No deliberate destructive
   overcurrent, unknown-register write or uncontrolled communications-loss test.
5. After bench validation, test the existing frontend's independent mailbox,
   request persistence/restart Unknown behavior, device/IP isolation, no ASIC
   retransmission, STOPPED-only access and zero HV traffic during RUNNING,
   PAUSED or transitions. Use mock/injected failures for lost ACK/timing paths
   unless a separately approved bench failure procedure makes physical testing
   safe. Connect detectors only after the sensor-specific limits and the
   measured shutdown/ramp behavior have been approved.

## HV integration with revised requirements (2026-11-04)

This section supersedes the earlier 0--60 V, zero-default CurrentLimit and
all-operations-disabled development stages. The user explicitly specified
0--62 V, default CurrentLimit 10 µA and measured-zero threshold ≤1.0 V.
These are requested operating policies, not newly measured hardware accuracy.
Only development source edits, builds and communication-free mocks were
performed. No frontend, ODB client, service or hardware was started/accessed;
no `/home/daq` file or production port 8081 was accessed. Development port 8181,
experiment and `/Custom/Path`/served assets were not queried because real ODB
and service access are prohibited in this task. GUI checks use local files.

### Evidence and the remaining RTL gate

Legacy `VME-EASIROC.rb:311-416` and `yaml/Calibration.yml:1-9` remain the
source of DAC/MADC addresses, ordering, delays and calibration. The DAC range
is now constrained to 0--62 V; endpoint codes are 747 and 26409
(`truncate(413.9*V+747.8)`). Shutdown is only `0x10003 <- 0`; there is no
invented `0x10003 <- 1` operation. A successful setpoint write records Enabled
as ON because it may release shutdown; the code never writes a zero DAC as
an implicit prelude to emergency shutdown or as a way to discover state.
EnabledState reflects acknowledged commands, not physical state readback.

The [official User Guide](https://research.kek.jp/people/isamun/easiroc/manual.pdf),
printed p.14, was consulted again for the CH=32 requirement and DC-DC current
interpretation. `measure()` explicitly writes `0x10012=0` before the pair and
again after successful completion, with 10 ms waits, following legacy
`setCh(32)`. It does not restore an arbitrary previously connected channel.
On an error it stops without extra cleanup writes and marks state unknown.
It retains the legacy MADC ch3/ch4 control sequence and conversions. Full-scale
raw samples are rejected as potentially saturated, not treated as safe.

**The requested `rtl/HVControl.vhd` is absent from this checkout and from the
searched `/home/nagao` tree. Its location was requested from the user; a public
search did not locate a usable copy. No RTL contents were inferred.**
Consequently `easiroc_hv.h::kEnableProtocolVerified` remains false in production.
Only the injected offline adapter tests pass a verified flag. Enable admission
therefore remains unavailable against hardware, even when other conditions
hold. A matching RTL review is needed before changing this source gate;
there is no browser/ODB unlock. Monitoring, explicit shutdown and safe
near-zero normal-stop handling do not require guessing a re-enable register.
Known-ON voltage ramping is implemented, but cannot become available through
production Enable until that gate is resolved.

### Policy and command lifecycle

`easiroc_hv` now has independent Enable, Voltage, Stop and Emergency operations.
It performs no I/O on construction. All requests require a registered/enabled
route, STOPPED, no transition and no ASIC Apply/pending ASIC request. The
existing local ApplyOdbLock implementation is reused for each HV service
invocation; neither that implementation nor MIDAS core, Deferred Transition,
START exclusion or the ASIC Apply executor was changed.

- Startup and IP changes discard old requests without execution, initialize
  Enabled/setpoint state Unknown and never populate actual state from stored
  command values. New monitoring can establish measured voltage/current,
  never digital Enable. Explicit acknowledged shutdown establishes command
  OFF. Enable requires this known shutdown, target exactly zero, valid
  CurrentLimit, non-indeterminate valid ASIC LastApplied and all 64 applied
  InputDAC codes in 256--511 (enable bit set). It takes a fresh sample and
  requires voltage ≤1 V and current not above CurrentLimit before sending
  the zero-DAC sequence. Even then it enters Settling before success.
- Ordinary ramping changes the remembered successfully written setpoint by
  at most 2 V per eligible one-second tick. During ramp/settling, a measured
  current above CurrentLimit latches target zero and a 10 V step until zero.
  Current falling below the limit does not cancel that emergency descent.
  A normal Stop can interrupt a ramp and requests shutdown after reaching
  zero; it preserves any existing overcurrent descent latch.
- On the final write the ramp remains active. Two subsequent one-second
  samples within 1 V of target are required before leaving Settling; this is
  an explicit software settling criterion. For target zero this is the
  user-specified ≤1 V condition. A 60-second settling timeout faults without
  additional recovery writes. A surge following the final DAC write still
  triggers emergency descent. There is no extra write when setpoint already
  equals target.
- Normal Stop of a known ON supply uses the normal ramp to zero, verifies the
  final samples and only then sends shutdown. For an unknown setpoint, it
  can only sample and shutdown when already ≤1 V; otherwise it rejects and
  directs the operator to explicit emergency shutdown. It never guesses a
  ramp start or accidentally releases an unknown shutdown via a DAC write.
- Emergency sends shutdown first, including from Unknown or a latched fault;
  it has no measurement/CurrentLimit prerequisite. It is still prohibited in
  RUNNING/PAUSED/transitions/ASIC Apply. Shutdown never asserts measured zero:
  a new residual-voltage measurement is taken promptly and then every 30 s.
  Lost shutdown ACK is Indeterminate/Unknown, not OFF/success.
- Steady monitoring is every 30 s while STOPPED. Overcurrent then is WARNING
  only; no autonomous ramp/shutdown is issued. Invalid CurrentLimit does not
  disable residual monitoring, but cannot authorize an ordinary operation or
  protection. CurrentLimit defaults to 10 only for missing keys; existing
  values, including a previously stored unconfigured zero, are preserved.
  Finite limits must satisfy `0 < limit < 2385.474 µA` to be operationally
  accepted. That ADC-representation bound is not a detector safe-current rating.
- A monotonic clock schedules ticks and detects >2 s ramp gaps, reversal and
  >1 s callback measurement/write budgets. No burst catch-up occurs. A fault
  clears measurement validity and known output state, stops the ramp and
  prevents further automatic hardware callbacks. Only a new explicit emergency
  shutdown can reconcile it; no automatic zero write, shutdown or reset follows
  communication failure. Completed samples have a separate wall-clock update
  timestamp. Freshness remains 2 s while ramping/settling and 65 s steady.

### Frontend, transport and device-specific ODB

`frontend_call_loop` is enabled and frontend_loop invokes the HV adapter at
most once per monotonic second, replacing the old ten-second rejection-only
status call. The adapter owns no services/threads. All hardware callbacks run
serially on the frontend thread under the existing exclusion contract. It
joins the preexisting diagnostic worker before any HV I/O, so the worker cannot
share TCP/RBCP access with an HV transaction. ASIC Apply remains unchanged and
can execute between HV ticks; if it is pending/active at the next tick, the
HV ramp is interrupted with Unknown rather than issuing overlapping I/O.

The existing RbcpClient gains an optional maximum-attempts constructor argument,
validated as 1--3, with default 3 preserved for every existing caller. Only HV
binds it with 1 attempt to avoid hidden retransmission of strobes after a lost
ACK. The existing one-second packet timeout and ACK validation are retained.
There is no new transport library or custom RPC. This does not guarantee an
analog 2/10 V/s slew or protection response; actual converter dynamics remain
subject to hardware tests.

The adapter reuses the legacy Network/IPAddress plus Devices/<id>/IPAddress,
canonical IP/duplicate validation, and each registered base's own Settings,
Commands and Variables. Invalid registry means zero HV hardware access; deleted
or reassigned identities lose runtime state. Unknown/malformed ASIC state also
blocks HV access. ASIC LastApplied is used as a successful-transmission record,
not promoted to ASIC readback.

Each base preserves `Settings/HV/CurrentLimit_uA` independently of requests.
Existing `Commands/HV/{Operation,TargetVoltage_V,Enable}` are staged display
values. Execution uses an atomic STRING `RequestPayload` of the form
`<DWORD id> <Enable|Voltage|Stop|Emergency> <voltage>` plus the independent DWORD
`RequestId` commit token. The adapter snapshots them under the existing lock,
checks matching IDs and rejects mismatches, extra tokens or unknown operations.
This prevents another browser's replacement payload from being executed under
an older request ID. Startup consumes/discards all preexisting tokens, and
in-memory seen IDs prevent replay even if result publication fails. IDs never
wrap. A later stop/emergency can supersede an active request; result tracking
uses ResultRequestId as well as LastHandledRequestId and ActiveRequestId.

Variables/HV publishes command-derived EnabledState/StateSource/SetVoltageState,
SetVoltage_V/TargetVoltage_V, measured values, MeasurementValid,
LastUpdateUnixTime, RampStatus, InProgress, Result/Error/ResultUnixTime,
ActiveRequestId/ResultRequestId/LastHandledRequestId, ProtectionActive,
OperationsAvailable and individual Enable/Voltage/Stop/EmergencyAvailable
capabilities. BlockedReason specifically identifies the unresolved RTL gate.
Validity is cleared first and set last after a complete status snapshot;
acknowledgement is written last. Publication failure faults in memory, attempts
best-effort Unknown/invalid publication and never replays hardware as a retry.

### GUI and validation

The original bottom HV panel now has 0--62 V target staging, Enable-at-zero,
normal stop/Disable and a visually distinct emergency shutdown. Confirmations
name the IP and distinguish ramp-and-verify from immediate shutdown. It uses
existing ODB JSON RPC calls only. It rechecks registration and fresh DAQ/ASIC
state while staging and before the RequestId commit. ASIC Save/Apply functions
and their request IDs are untouched. Refresh preserves an edited HV target;
selection changes load that device's staged target. Settings CurrentLimit is
read/displayed independently and remains editable through ODB Settings.

Each selected base has its own outstanding request record. Pending/timed-out
requests block voltage/Enable re-submission until a matching terminal result;
explicit Stop/Emergency remains available when backend capabilities permit.
An emergency advances even an uncertain request ID, rather than replaying it.
RPC timeout never becomes a synthetic success. Missing/stale/future-dated/error
samples display Unknown, current > CurrentLimit is red, and last sample time
remains separately visible. No physical OFF is inferred from a sample or from
EnabledState. No new monitor page/graph/temperature feature was added.

Explicit local mock previews are `?mock=hv_disabled`, `hv_settling`,
`hv_overcurrent`, `hv_stale` and `hv_error`; the mock badge states no hardware
access. Simulated Enable capability in those previews is not the production
RTL gate. The Node DOM/RPC suite also exercises real mailbox submission logic
with in-memory RPCs and virtual timers, including uncertain acknowledgements.

Validation on these sources: `make -C easiroc_frontend -j4 all check` succeeds
without compiler warnings/errors. HV policy/protocol: 97 checks; real ODB adapter
linked to in-memory MIDAS APIs: 2154 checks; other existing ASIC/START/readout
suites pass. `make -C easiroc_frontend check-web
NODE=./build/tools/node-v22.16.0-linux-x64/bin/node` succeeds: 110 checks plus
JS syntax. Tests cover 0/62 V, DAC/MADC ordering and conversion, 2/10 V steps,
10 µA threshold/equality, 1 V boundary, post-final samples/surge, shutdown
failure, residual voltage/re-Enable, stale data, restart/no replay, torn
requests, IP separation, RUN/PAUSED/transitions/Apply prohibition and failed
result publication. These are offline results, not real-browser layout,
served-file, real socket, FPGA, analog slew, calibration or hardware validation.

Before any authorized frontend launch, obtain/review the missing matching RTL,
check actual firmware/board identity and calibration, and bench-test the zero
DAC/shutdown/re-enable sequence, residual voltage, current reading, settling
and safe load response with external instruments. The 1 V settling band,
10 µA default and 2/10 V steps implement the requested policies; their physical
suitability has not been measured here. Existing calibration/LastApplied is
not proof of hardware state after power loss, firmware changes or an edited IP.
Do not clear the Enable gate merely to make a button usable. Starting this
frontend can perform actual MADC writes every 30 s while STOPPED, even when
Enable remains gated; it must not be launched under this task's no-hardware
communication authorization.

## Supplied HVControl RTL review and safe re-Enable (2026-11-04 follow-up)

The user supplied `reference/firmware_20150923/rtl/HVControl.vhd`. This section
supersedes the preceding statement that the RTL is unavailable and the
resulting blanket protocol gate. The reference file was inspected without
modification. No real ODB, service, firmware programming, hardware communication
or HV operation was performed. This is static RTL review, not HDL simulation
or validation of the installed FPGA image.

Confirmed relative register behavior in the supplied RTL:

| Evidence | Behavior |
|---|---|
| lines 94--111 | Write-only RBCP receiver covers four bytes at generic G_HV_CONTROL_ADDR. The absolute 0x10000 mapping is from the existing Controller; the containing top-level instantiation is not supplied here. |
| lines 114--123 | Offset 0 stores the high DAC byte, offset 1 stores the low byte; **each independently clears shutdown immediately**, before the newly requested DAC value has been transferred. |
| lines 124--135 | Offset 2 asserts dac_start and clears shutdown. With no receiver write, dac_start is cleared on a later clock. Offset 3 sets shutdown to 1 without testing the written data; the legacy write of zero is valid. |
| lines 139--148 | DAC data contains high/low bytes at bits 21:14 and 13:6; RST clears dac_par, but up_dac/low_dac and shutdown have no reset assignment in their receiving process. |
| lines 150--161 | RST or shutdown drives HV_EN low; otherwise HV_EN is high. No separate Enable write is needed to release shutdown, and no Enable/DAC readback port is provided. |
| lines 163--190 | The start logic drives a separate shift_reg component. RBCP acknowledgement is not proof that the DAC serial transfer or analog output has settled. The component implementation is not present in this supplied file. |

**The first DAC byte write can re-enable the supply at its previous DAC output.**
A three-write zero-DAC sequence is not an atomic, safe zero-before-enable
operation. A low measured voltage while shutdown is active does not identify
that retained DAC value. In particular, Emergency at a 62 V setpoint followed
by a low residual voltage must not authorize writing a zero DAC as re-Enable:
HV_EN may become high before the old nonzero output is replaced.

Changes made after this finding:

- `kEnableProtocolVerified` is now true for the reviewed reference logic; this
  is not a claim that the installed firmware/board has been identified.
- `request(Enable)` additionally requires a known previously acknowledged DAC
  setpoint exactly 0 V, along with acknowledged OFF, target 0 V, applied ASIC,
  enabled InputDAC, valid CurrentLimit and a fresh voltage/current check. The
  condition is checked **before any Enable measurement or DAC callback**.
- EnableAvailable uses the same known-zero condition; BlockedReason explains
  unsafe initial/emergency recovery instead of incorrectly reporting a missing
  RTL file. Changing ODB state strings cannot create known internal DAC state.
- A normal Stop from a known ON supply writes/ramp-confirms zero before
  shutdown, retaining known-zero state for a later re-Enable in the same
  frontend process. Emergency never substitutes a DAC write; it preserves the
  prior setpoint knowledge, including a known nonzero value. Communication or
  publication faults clear that knowledge. An emergency from known zero can
  remain eligible; one from nonzero or unknown cannot.
- Restart still begins Unknown. A near-zero normal Stop of an unknown supply
  can shut it down, but it cannot establish that the retained DAC is zero.
  There is intentionally no ODB-based override or auto-bootstrap.

The safe **initial** Enable/recovery path is therefore still unresolved for a
fresh frontend process or an unknown/nonzero retained DAC. The supplied RTL
alone provides no way to preload the DAC while keeping shutdown asserted.
A board-matched documented initialization procedure or independently verified
hardware support is needed; do not invent a register write, assume a reset
makes persistent OFF/zero, restore a saved setpoint as fact, or power-cycle
as an unapproved workaround. Actual firmware identity, receiver/top-level
mapping, shift_reg timing, calibration and analog transients remain bench
verification items. Existing 0--62 V, 10 µA default, 1 V zero criterion and
normal/emergency stop behavior otherwise remain unchanged.

Regression results after these changes:
`make -C easiroc_frontend -j4 all check` passes without compiler warnings/errors;
HV policy/protocol 101 checks, in-memory real ODB adapter 2150 checks, all other
existing suites pass. The adapter's earlier artificial initial-Enable success
expectation was replaced with rejection of unknown retained DAC; it is not an
acceptable production bootstrap. Pure policy tests supply a trusted known-zero
fixture to test the complete normal-stop/re-Enable/ramp lifecycle and explicitly
prove that Unknown and a retained 62 V DAC cannot send an Enable write even
when measured residual voltage is low.
`make -C easiroc_frontend check-web NODE=./build/tools/node-v22.16.0-linux-x64/bin/node`
passes all 110 checks. No frontend or served GUI was started/queried and no
live/development ODB or service was changed. Reference RTL remains untouched.

## Legacy-compatible initial Enable admission (2026-11-04 follow-up)

This section supersedes the preceding known-zero-DAC/prior-shutdown admission
policy. At the user's explicit instruction, initial Enable no longer requires
known DAC=0, an acknowledged prior shutdown, or known Enable/DAC state after a
frontend restart. The change prioritizes compatibility with the legacy
operation sequence; it does not establish that initial Enable is electrically
safe. No firmware, MIDAS core, START exclusion or existing lock code changed.

### Reference comparison

`reference/controller_20170425/Controller.rb:68-98` implements `shutdownHV` as a
separate command, `setHV` as MADC setup followed by DAC control, and `increaseHV`
as a loop whose first step is `setHV(0)`. None checks for known-zero DAC,
acknowledged shutdown or an initialized software state before that first write.
`VME-EASIROC.rb:311-350` sends MADC setup (`0x10010=248`, 10 ms, `0x10011=0`),
then high byte `0x10000`, low byte `0x10001`, start `0x10002=1`. Shutdown is
`0x10003=0`. These transport sequences are unchanged in the current helpers.

The [Kyoto University public EASIROC manual mirror, 6 June 2017](https://www-he.scphys.kyoto-u.ac.jp/member/kikawa/tmp/chikuma/easiroc_manual.html)
was reviewed. It lists firmware_20150923 and controller_20170425, describes
setHV/increaseHV/statusHV, and identifies the InputDAC enable bit and code
range 256--511. It does not prescribe the previously added known-DAC/prior-
shutdown software gates. Its reported deployment testing is not proof of the
identity or behavior of the hardware connected to this development frontend.

Compatibility here concerns **initial/re-Enable admission**, not all legacy
behavior. The old setHV is an immediate DAC change accepting up to 90 V; its
increaseHV uses 10 V steps, one-second sleeps, extra waits and 20 µA checks.
Current policy deliberately retains 0--62 V, default 10 µA, normal 2 V steps
per second, overcurrent 10 V steps toward zero, STOPPED/transition/ASIC checks,
and final confirmation by two subsequent one-second samples.

### Minimal implementation change and unchanged guards

`easiroc_hv::request(Enable)` no longer reads set_known/set as admission gates,
and no longer requires enabled_known/OFF. The ODB EnableAvailable capability
and BlockedReason follow the same rule. The obsolete known-zero recovery
reason was removed. No product Web JS/layout change was needed: the existing
page uses the backend's per-device capability and continues to display Unknown
until actual measurement or command-result information is published.

The following remain required: registered/enabled device, STOPPED, transition
clear, no pending/active ASIC Apply, reviewed reference protocol, no latched
communication/publication fault, no active ramp, exact Enable target 0 V,
valid positive CurrentLimit, valid/non-indeterminate ASIC LastApplied and all
applied InputDAC enable bits set. A fresh MADC sample is still taken before the
zero-DAC write, with measured voltage ≤1 V and current ≤CurrentLimit required.
These measurement checks were not among the user-requested removals and remain
additional preconditions compared with legacy setHV. Known-ON duplicate Enable
is still rejected; normal voltage changes/stops retain their ramp rules. A
fault still requires explicit recovery; Unknown alone is no longer a fault or
an Enable veto.

Startup, IP-change and restart handling are unchanged: actual Enabled/setpoint
state is Unknown and MeasurementValid=false; stale requests are consumed
without execution. A **new explicit** zero-target Enable can now take a fresh
sample and issue the zero-DAC command without an implicit shutdown. Only
successful callbacks establish command-derived ON/setpoint=0, then Settling
runs until two later samples qualify. A submission, cached target, low sample
or empty state alone never becomes hardware readback or confirmed safety.
A request/measurement/write/publication failure retains the existing
Indeterminate/Unknown behavior and automatic hardware callbacks remain halted.

Normal stop still ramps to zero, measures twice, then shuts down; emergency
still sends shutdown without a zero-voltage prerequisite or zero-DAC write.
After acknowledged emergency shutdown, a retained nonzero/unknown DAC no longer
vetoes explicit Enable; fresh measurements and all other guards still apply.
Steady overcurrent remains WARNING only; ramp/settling overcurrent still latches
an emergency descent. CurrentLimit remains independent and persistent per IP.

### Explicitly retained risks

`reference/firmware_20150923/rtl/HVControl.vhd:114-159` clears shutdown on the
first high-byte write, before the newly requested zero code is transferred.
Thus the previous nonzero DAC setting can briefly reappear even when the
pre-Enable measured voltage was ≤1 V while shutdown was active. That reading
does not identify the retained DAC, and a target of zero does not make the
multi-write transaction atomic. Neither ACK, a later near-zero sample nor the
one-second current-monitor interval excludes an earlier transient.

The static counter finding in `HVControl.vhd:163-180` and serial behavior in
`shift_reg.vhd:25-50` are not repaired by this source-policy change: periodic
retransfer and mixed high/low-byte data remain possible in the reference RTL.
The real board/DAC/firmware mapping, physical HV_EN behavior, latch timing,
output peaks and calibration remain unverified here. Unknown, commanded zero,
measured ≤1 V and shutdown continue to be different facts. Removal of the
software gates is an intentional compatibility decision, not a hardware
safety guarantee or authorization to operate the module in this task.

### Offline verification

`make -C easiroc_frontend -j4 all check` passes without compiler warnings or
errors. HV policy/protocol has 115 checks, the real adapter linked to in-memory
MIDAS APIs has 2390 checks, and the existing ASIC/START/readout suites pass.
New/regressed cases cover first Unknown Enable without preceding shutdown,
zero-target settling then upward ramp, retained nonzero DAC after emergency,
new requests after restart, stale-request discard, fresh measurement and fault
handling, unchanged normal/emergency stop, overcurrent descent/steady warning,
IP separation and independent ASIC request IDs. Pure tests no longer fabricate
a known-zero DAC just to bypass the removed condition.

`make -C easiroc_frontend check-web NODE=./build/tools/node-v22.16.0-linux-x64/bin/node`
passes JS syntax and 114 DOM/RPC checks. Added checks submit Enable-at-zero from
an Unknown/invalid-measurement snapshot when backend capability permits, while
leaving EnabledState, MeasurementValid and LastUpdate unchanged until the
frontend responds. The browser writes only the independent HV mailbox.

No frontend, real ODB, service, hardware communication or HV operation was run.
No production file or port 8081 was accessed. Development port 8181 and actual
served paths/assets were not queried under this task's restrictions. Builds
and tests used development files; no deployment/analog behavior is claimed.

## 通常 frontend への統合・専用試験環境の撤去（2026-11-04）

この節が現行の開発8181の状態を示す。以前の調査・試験時点の配備状況、
InputDAC初期値、Enable禁止条件は履歴であり、この節と後述の互換Enable仕様を優先する。

### ソースと実行経路

- `feeasiroc.cxx` は通常の `feeasiroc` 一種類に統一した。試験専用の環境変数分岐、
  equipment名、起動・終了・DAQ callbackの迂回、模擬backendを削除した。
  通常の `frontend_loop()` が既存HV policy、RBCP、MADC callbackを接続する。
- `easiroc_odb.h/.cxx` は通常 `/Equipment/EASIROC` と既存 `Devices/<id>` 登録を使用する。
  装置別 Settings/HV、Commands/HV、Variables/HV とcapabilityを生成する。
  試験fixture生成・別namespace・SIMULATION結果加工は削除した。
- 実機通信を発生させず通常版を配備するため、装置別
  `Settings/HV/HardwareAccessEnabled`（BOOL、欠損時false）を追加した。
  falseでは全HV操作capabilityがfalseになり、要求はRejected、周期MADCも実行しない。
  BackendModeは `Hardware (inhibited)`。模擬値や成功は生成しない。
  許可取り消し時は既存状態・測定をUnknown/無効にし、降圧やshutdownを自動送信しない。
  許可変更は拒否済み要求を再実行しない。ASIC Applyの設定・要求管理から独立している。
- 許可trueで使用する既存policyは変更しない：0–62 V、CurrentLimit初期値10 µA、
  通常2 V/s、Ramp中過電流10 V/s降圧、定常過電流WARNING、実測≤1 V、
  最終ステップ後連続2回確認、監視1秒/30秒、通常停止と緊急shutdown。
  RUNNING/PAUSED/遷移中はHV操作・測定を禁止し、既存ASIC Applyとの排他を維持する。
- 初回Enableは目標0 VとASIC適用済み/InputDAC有効等を要求するが、
  DAC既知0 V・shutdown済み・Unknown禁止は復活させていない。
  起動/IP変更時は過去RequestIdを破棄し、状態をUnknownから開始する。
- 通常 `easiroc.html/.js/.css` のHV枠を使用する。試験URL分岐・リンク・バナー、
  専用frontend識別と疑似ASICロックを削除した。確認ダイアログと独立HV要求を維持する。
  通常の通信なしブラウザmockとC++ mockユニットテストは維持する。
- 削除した専用ファイルは `easiroc_hv_simulation.h` と
  `tests/easiroc_hv_browser_test.py`。専用の生成済みブラウザprofile/画像も削除した。
  `scripts/start_feeasiroc.sh` は引数を転送し、既存のMIDAS daemon起動オプションを受け付ける。

### InputDAC 480

`easiroc_run_settings.h::kDefaultInputDac` を480に変更した。新規ODBは同じ設定型から
初期化するため、両ASIC各32chが480になる。Web説明・mock初期値・対応テストも480にした。
`legacySiteDefaults()` と旧Controllerの比較用350 fixtureは、参照データの忠実性を保つため
変更していない。通常frontendの設定配列は480の現行設定でoverlayされる。
480と旧350はどちらもbit8=1。InputDAC有効化bit、ChannelEnabled、他のASIC設定は変更しない。

### 開発8181への反映記録

対象は `daq-dev`、port8181、Custom/Pathは
`/home/nagao/midas/midas/online/daq_monitor/web`、登録IPは192.168.10.40。
RUN186はSTOPPED、遷移/要求遷移0。取得停止・TCP未接続・faultなし・ASIC要求処理済みを
確認して、ホスト側mhttpdの既存MIDAS RPCから通常frontendを再起動した。
最終通常client PIDは912305。起動コマンドは開発experimentを明示した
`/usr/bin/env MIDAS_EXPT_NAME=daq-dev /home/nagao/midas/midas/online/scripts/start_feeasiroc.sh -D`。
サービスunit、他のDAQ client、MIDAS本体、START排他、firmwareは変更していない。
最終実行バイナリのSHA256は
`c435375d03dbda8db94147a55dfdb0eeb6aed3b86abff4f8b8cc51b12f4a9b81`。

専用clientは非取得・HV操作中でないことを確認して停止し、専用Programsと4 equipment
（設定用1、無効なDAQ equipment3）のODBをバックアップ後に削除した。
復旧用JSONと保護データ比較はgit対象外の
`easiroc_frontend/build/backups/hv-normal-20261104/` に保存した。
`before.json` はMIDAS db_copyのsave形式、`final.json` は反映後、
`protected-before.json` は比較用値、`browser-read.json` は通常画面の確認結果である。

開発ODB Settings/ASIC1/InputDAC、ASIC2/InputDACを各32個の350から480に上書きし、
64chすべてを読戻し確認した。LastApplied（350のまま）、他のSettings、ASIC要求、
RUN情報を比較して保持を確認した。実機Applyは送信していないため、設定とLastAppliedの
不一致表示は意図した状態であり、適用済み480と表示してはいけない。

通常HV schema生成済み。CurrentLimit_uA=10、HardwareAccessEnabled=false、
EnabledState/SetVoltageState=Unknown、MeasurementValid=false、更新時刻0。
HV要求・処理済みIDは0で、今回HV要求は送信していない。
実機アクセス許可をtrueにすると周期MADCが開始し得るため、今回trueには変更していない。

### 検証と残課題

- `make -C easiroc_frontend -j4 all check` 成功。16 test、3960 checks。
  HV policy115、ODB adapter2632を含む。コンパイラ警告・エラーなし。
- Web mock115 checks成功。全64chの480、Enable確認と独立要求、禁止状態、
  エラー/Unknown、複数IP、ASIC Applyとの独立性を含む。
- ODB mockには、許可初期false・拒否要求の非再実行・許可取り消し時のUnknown化・
  緊急要求/周期監視も通信しないことを追加した。既存の再起動要求破棄も成功した。
- Firefoxで通常8181を開きREADのみ実行した。64入力が480、10 µA、Unknown、
  Hardware (inhibited)と禁止理由、HV全4ボタン無効、通常Save/Apply表示を確認した。
  配信HTML/JS/CSS/mock-dataの4ファイルが開発ソースとbyte一致した。
  HV操作、実機Apply、HV周期測定は行っていない。
- 他DAQ client登録の維持、RUN番号/状態/遷移、LastApplied、既存ASIC要求の不変を比較した。
  実際のRUN開始・DAQ読出しの回帰試験は今回実行していない。

実機試験へ進むには、担当者の許可と配線・負荷・firmware対応・校正の確認後に、
通常装置のHardwareAccessEnabledを明示的に有効にする必要がある。
Controller/RTLで確認したDAC上位/下位/開始書込み時のshutdown解除、旧DAC出力の一時再開や
中間コードのアナログ影響は解消していない。互換Enableを安全保証と解釈しない。
今回の配備・mock成功は実機HV、校正、DAQ読出しの成功を示さない。
本番 `/home/daq`、8081は操作・変更していない。

## 実機初回0–2 V試験の事前確認（2026-11-04、印加未実施）

ユーザーはMPPC取り外し済み、開発8181の対象192.168.10.40へのHV操作・監視を許可し、
無負荷試験、CurrentLimit試験なし、ASIC Applyはユーザー手動と指定した。
ただし「配線や出力先が確認できない場合、HV印加には進まず報告する」という条件がある。
現時点では実物のHV配線・JP2/JP3・外部HV電源/他負荷の接続状態を確認できていない。
現物確認を依頼し、HardwareAccessEnabledはfalseに保持した。今回HV操作要求は送信していない。

### 読み取りによる確認結果

- 実験daq-dev、開発port8181、Custom/Pathは開発webディレクトリ。
  RUN186=STOPPED、Transition in progress=0、Requested transition=0。
- 通常client feeasiroc PID912305、heartbeatは約1秒以内、FrontendBuildは
  Nov 4 2026 07:15:37。既存の開発起動コマンドと前回配備記録が一致する。
  ディスクバイナリSHA256は前回配備値
  c435375d03dbda8db94147a55dfdb0eeb6aed3b86abff4f8b8cc51b12f4a9b81 と一致。
  他clientの停止・frontend再起動・サービス操作はしていない。
- legacy登録IPは192.168.10.40、Devicesキーなしで他の登録ルートなし。
  frontend公開の Readback/Firmware はValid=true、v.4.0.1-p0、合成日2015-2-20、
  raw=[64,16,32,21,2,32]。このバージョン読戻しだけでは参照RTLとの完全同一性は証明しない。
- CurrentLimit_uA=10、Commands/HV/TargetVoltage_V=0、RequestId=LastHandledRequestId=0。
  ASIC ApplyRequestId=LastHandledRequestId=4、ApplyInProgress=false。
  InputDAC Settingsは両ASIC各32ch=480、LastApplied.Valid=true、各32ch=350。
  今回これらのODB値は変更せず、ASIC Applyも送信していない。
- HV EnabledState=Unknown、MeasurementValid=false、LastUpdateUnixTime=0。
  保存されている電圧・電流の数値0は有効な実測値ではない。設定0 Vと実測0 V、
  shutdown済みを同一視しない。実測≤1 VやDisable成功は今回確認していない。

### 制御・測定手順と有効化時の作用

`reference/controller_20170425/Controller.rb:68–98` と
`VME-EASIROC.rb:311–379` を現行 `easiroc_hv.cxx:14–56` と照合した。
MADC初期化0x10010=248、0x10011=0の後、HV DAC上位0x10000、下位0x10001、
開始0x10002=1を書き、shutdownは0x10003=0である。
電圧MADC ch3、電流ch4を別々の上位/下位byteから読み、
電圧=0.00208*raw+0.0355 V、電流=0.0364*raw µAで換算する。
現行測定は前後に0x10012=0を設定し、旧ControllerのsetCh(32)
（VME-EASIROC.rb:389–414、mux=0）と一致する。
[公式User Guide](https://research.kek.jp/people/isamun/easiroc/manual.pdf) の印刷p.14も
測定時CH=32 (No Connect)を要求する。印刷p.7はJP2の内蔵/外部HV経路と
JP3の外部出力経路を説明するが、現物のジャンパ位置を示す資料ではない。

`easiroc_odb.cxx:1630–1662` の実行許可falseはHV要求とtickを抑止する。
trueへ変更すると、過去要求の再実行やDAC Enable要求なしでも次回serviceで
30秒周期のMADC測定が始まり得る。測定にはMUX/MADC制御レジスタ書込みを伴う。
したがって現物確認前は許可を切り替えていない。RUN/PAUSED/遷移/ASIC busyの禁止は
`easiroc_hv.cxx` のaccess()/tick()と既存GUIガードが引き続き適用される。
ソース変更や実RUN遷移による試験は行っていない。

### 再開に必要な確認と残課題

JP2/JP3の実物位置、内蔵HVの出力先、外部HV電源と全負荷の非接続を担当者が確認する。
外部電圧計が使えるかも確認する。これらが確認されるまで有効化・印加へ進まない。
確認後もDAC書込みでshutdownが解除され以前の出力が一時再開し得る既知リスクは残る。
今回は新しいHV通信、Enable、0–2 V昇降圧、Disable、CurrentLimit動作試験を未実施。
測定値・残留電圧・停止成功を推測しない。本番8081、/home/daq、Firmware、MIDAS本体、
START排他機構は変更していない。

## 実機0–2 V初回試験の再開・結果（2026-11-04）

直前節で未確認だったJP2/JP3、HV出力先、外部HV/負荷の接続状態はユーザーが確認・承認した。
MPPC取り外し済みの無負荷条件で、開発8181の通常feeasiroc、対象192.168.10.40のみ操作した。
同じ配線確認を再要求していない。ソース変更、frontend再起動、ASIC Save/Applyは実施していない。

### 事前条件と実施経路

RUN186=STOPPED、Transition in progress=Requested transition=0、通常client PID912305稼働、
heartbeat新鮮、firmware公開読戻しValid=true/v.4.0.1-p0を確認した。
CurrentLimit_uA=10、目標0 V、HardwareAccessEnabled=false、HV RequestId/Handled=0。
Settings/InputDACはASIC1/ASIC2各32ch=480、LastAppliedは各32ch=350、Valid=true。

既存mhttpd JSON-RPCで対象装置のHardwareAccessEnabledだけをtrueにし、
通常frontendのMADC callbackで最初の有効測定を確認した。HV要求IDは0のままで、
許可変更による旧要求の実行・DAC書込み要求は認めなかった。
その後Firefoxで通常easiroc.htmlを開き192.168.10.40をREADし、各操作を実ボタンクリック、
ネイティブOK確認ダイアログを承認して送信した。mockページや別frontendは使用していない。

### 観測記録（時刻はJST、2026-11-04）

| 時刻 | 要求ID | 操作・状態 | MADC電圧 V | MADC電流 µA | 結果 |
|---|---:|---|---:|---:|---|
| 08:05:27測定 | 0 | 実機監視開始、HV状態Unknown | 0.06254 | 0.0364 | Valid=true、エラーなし |
| 08:06:15送信、08:06:18完了 | 1 | Enable at 0 V、Settling→Steady | 0.06254 | 0.0364 | Succeeded、ON、Set=Target=0 |
| 08:06:24–25送信、08:06:26 Set=2 | 2 | 0→2 V指令、Ramping→Settling | 0.06254 | 0.0364 | DAC指令2 V、実測追従なし |
| 08:06:43試験側打切り | 2 | Settling/Pending継続、最終測定08:06:42 | 0.06254 | 0.0364 | 2 V到達未確認、追加昇圧中止 |
| 08:06:50–51送信、08:06:54 shutdown ACK | 3 | Normal Stop、2→0 V指令→2回確認→Disable | 0.06254 | 0.0364 | Succeeded、OFF、Disabled、Set=Target=0 |
| 08:06:56停止後測定 | 3 | shutdown後の残留電圧測定 | 0.06254 | 0.0364 | Valid=true、≤1 V、エラーなし |
| 08:07:19–20 | 3のまま | 許可falseへ復帰、監視停止 | 最後の保存値0.06254 | 最後の保存値0.0364 | Unknown/Valid=false/Indeterminateへ無効化 |

ID2は送信後約18秒で試験側の待機を打ち切った。frontend自身の60秒整定timeoutの発生ではない。
この時点でもMADC更新時刻は進み、通信エラーは公開されていなかった。
実測は2 Vとの差が1 Vを超えていたため、連続2回整定条件を通らずPendingのままだった。
追加昇圧せずID3の通常停止でID2を置換した。通常停止ボタンは降圧・連続測定・shutdownを
一連で実行するため、別の電圧0 V要求や重複Disable要求は送信していない。
DAC設定は1秒tickで0→2、2→0の一段ずつ変化したが、実出力の2 V/s追従は確認できていない。

### GUI・最終状態・保護データ

ID1、ID2、ID3の確認ダイアログに対象IPと操作が表示され、実際の装置別HV mailboxに届いた。
ID1のGUIはON/Steady/Succeeded/Valid、0.063 V、0.036 µAを表示した。
ID3のGUIはOFF/Disabled/Succeeded/Valid、同じ残留測定値と08:06:56の更新時刻を表示した。

停止完了後は新規HV要求を送信していない。HardwareAccessEnabled=falseに復帰し、
43秒以上後もLastUpdateUnixTime=1793747216（2026-11-04 08:06:56 JST）が変化しないことを確認した。
Commands/HVのTargetVoltage_V=0、RequestId=3、Operation=Stop、Enable=false。
Variables/HVのLastHandledRequestId=ResultRequestId=3、ActiveRequestId=0。

現行adapterは許可取り消し時、停止確認済みのOFF/既知0 V状態でもfault()を呼び、
EnabledState/SetVoltageState/RampStatus=Unknown、MeasurementValid=false、Result=Indeterminate、
Error="HV hardware access revoked; state unknown, no automatic hardware action" とする。
これは今回の通信異常や追加shutdownを意味せず、許可解除に伴う既存無効化処理である。
直前の停止ACK・有効な残留測定と、許可解除後の最終Unknown表示を混同してはいけない。
停止成功の履歴をこの無効化処理が上書きする点はソフトウェア上の残課題として記録する。

Settings全体（許可を元へ戻した後）、LastApplied、ASIC要求、Runinfoを事前スナップショットと比較し一致した。
LastAppliedは以前のMIDAS db_copy save形式（メタデータ込み）とも完全一致。
CurrentLimit=10 µA、全64chのSettings=480/LastApplied=350を維持した。
他装置、DAQ読出し、RUN遷移、過電流保護の意図的試験、緊急shutdown、未知レジスタ操作は実施していない。
本番8081、/home/daq、Firmware、MIDAS本体、START排他機構は変更していない。

### 結論と残課題

0 V Enableと通常停止はfrontendの書込み応答・MADC上は成功したが、2 V到達試験は未達である。
すべての有効なMADC読出しは0.06254 V/0.0364 µAで一定だった。
値から換算を逆算すると電圧raw=13、電流raw=1相当だが、raw自体を今回別途取得していない。
設定に追従しない原因は未特定。MADC更新時刻/Validは通信・変換経路の成立を示すだけで、
アナログ出力や正しい測定対象への接続の独立した証明にはならない。
外部電圧計との比較は未実施のため、物理的な2 V出力やOFFを独立に保証しない。
以後の昇圧試験へ進む前に、許可された手順で外部測定との比較、MADC測定経路・
設置firmwareと参照RTLの対応、HV DAC/Enableのアナログ動作を切り分ける必要がある。
この調査目的の追加HV要求やレジスタ操作は今回行っていない。

要求・測定時刻・全状態遷移・GUI結果・保護データの記録はgit対象外の
`easiroc_frontend/build/hv-first-trial-20261104.json` に保存した。
`codex.log` は最終報告本文のみで上書きし、途中記録やコマンド履歴は保存しない。


## increaseHV方式への昇圧変更（2026-11-04、開発ソース・offline mockのみ）

この節は、先行節の通常昇圧2 V/stepおよび電圧到達を待つ整定条件を更新する。
対象は開発 `/home/nagao/midas/midas/online` の通常frontendとWebソース。
文書の実体は `easiroc_frontend/docs/easiroc_slow_control.md`（ルート直下のdocsはない）。
今回は実機通信、HV操作、ODB変更、frontend再起動を実施していない。
8181の実験・ODB `/Custom/Path`・配信ファイルも再照会しておらず、
この変更が稼働frontendまたは配信GUIに反映されたとは確認していない。
本番8081、`/home/daq`、FirmwareおよびMIDAS本体は変更していない。
既存の未コミット変更は保持した。

### 旧Controllerの確認と意図した差異

一次資料は `reference/controller_20170425/Controller.rb` の `increaseHV`
（77–98行）、`checkHV`、`VME-EASIROC.rb` の
`sendMadcControl` / `sendHVControl` / `readMadc`。
[公開マニュアル](https://www-he.scphys.kyoto-u.ac.jp/member/kikawa/tmp/chikuma/easiroc_manual.html)
にも各段の電流確認が記載されている。

旧increaseHVは0から10 V刻みでDACを書き、sleep 1、checkHV、sleep 0.2を繰り返し、
最後に指定値を書いて同じ検査を行う。旧checkHVはMADC ch3電圧とch4電流の
**上限**を確認し、超過時は再測定、試行上限で直接0 Vを書いて終了する。
電圧の到達下限チェックはない。旧制限90 V・20 µAは採用しない。

現行開発policyは次のとおり。

- Enableは既存の条件確認後、必ず0 VのDACを書き、1秒後の電流確認を経てSteadyへ進む。
- 通常昇圧は最大4 V/step。0→18 Vなら0→4→8→12→16→18 V。
  各DAC書込みcallbackの**完了時刻**から最低1000 ms待ち、既存MADC手順で測定してから
  次のDACを書き込む。callbackに時間がかかっても待機を短縮しない。
  最終段も同様に測定する。旧sleep 0.2は追加せず、既存のfrontend tickで進行する。
- DC-DC部のMADC ch4電流について `MeasuredCurrent > CurrentLimit_uA` のときだけ超過。
  ODB初期値10 µA、変更可能。10 µAちょうどは次段へ進める。
- Ramp中超過では追加昇圧せず、目標を0 Vへ切り替え、既存10 V/step・約1秒間隔で降圧する。
  電流が低下しても降圧を継続し、自動再昇圧しない。最終0 V書込み後も測定する。
- 通信、MADC、測定値、CurrentLimit、タイミング異常はFault/Unknownとし、次段を実行しない。
- 通常Voltage要求は最終段の電流検査が正常ならSteadyへ進み、電圧到達の下限で待機しない。
  書込み直後の短いSettlingは最終段の電流確認待ちを表す。
  実測と測定時設定値の差が絶対値で1 Vを超える場合はMonitoringStatusのWARNINGとする。
  実測値と更新時刻は引き続き保存・表示する。
- 通常降圧は2 V/step・約1秒間隔を維持。Normal Stopは0 V設定後も
  実測≤1 Vを連続2回確認してshutdownする既存条件と60秒timeoutを維持する。
  Emergency Shutdownは既存0x10003へのshutdown書込みで、測定を前提としない。
- STOPPED・遷移なし・登録装置・HardwareAccessEnabled・ASIC Apply排他を維持する。
  ASIC Save/Applyの設定と要求mailboxは独立したまま。

### 操作結果と監視状態

`Variables/HV/Result, Error, ResultRequestId, ResultUnixTime` は操作結果用。
`Variables/HV/MonitoringStatus` を現在の監視状態・電圧不一致・定常過電流・
通信異常などの表示用に追加した。定常時の過電流はWARNINGのみでDAC操作を起こさず、
成功済みのResultを書き換えない。HardwareAccessEnabled=falseによる無効化でも
完了済みResult/Error/要求ID/結果時刻を保持し、EnabledState/SetVoltageStateをUnknown、
MeasurementValid=false、操作capability=falseとし、監視停止理由を別に公開する。
進行中の要求が中断された場合はIndeterminateで終了する。

GUIは既存の0–62 V範囲、設定目標の編集保持、確認ダイアログを維持。
実行中DAC設定値SetVoltage_Vと実測電圧・電流・RampStatus・最終更新時刻を表示し、
MonitoringStatusと最終操作結果を別欄にした。表記は「昇圧4 V/step、約1秒間隔」。
HTMLの静的ファイル参照versionも更新した。GUIから装置への直接通信は行わない。

### offline検証と未確認事項

- `make -C easiroc_frontend all check` 成功。コンパイラ警告・エラーなし。
  HV protocol/policy 216 checks、実ODB adapterをin-memory MIDAS APIに接続したmock 2630 checks、
  その他の既存standalone/contract testsも全件成功。
- `make -C easiroc_frontend check-web NODE=./build/tools/node-v22.16.0-linux-x64/bin/node`
  成功。JavaScript構文検査およびDOM/RPC mock 119 checks。
- 0→2/18/62 V全ステップ、DAC完了後999 msで未進行・1000 msで測定、
  測定前の次DAC禁止、最終段電流確認、実測未到達でもWARNINGで完了を検証。
  10 µA境界、超過時追加昇圧禁止・10 V降圧・自動復帰なし、MADC/通信異常、
  通常2 V降圧、Normal Stop、Emergency、DAQ/ASIC排他、IP分離、
  ODB結果ID/時刻保持、Webの設定値/実測値/警告/結果表示を確認。
- 実機の昇降圧、MADC校正、DC-DC電流の実測、DAC/アナログ出力の追従、
  実際の1秒間隔、設置Firmwareとの対応、DAQ読出しは今回未検証。
  稼働8181でのロード・配信・動作確認と反映作業は今回行っていない。


## 修正版の通常frontend反映と実機0→10→0 V試験（2026-11-04）

### 対象・切替・保護対象

ユーザーの明示許可により開発8181、実験daq-dev、通常feeasiroc、IP192.168.10.40のみ実施。
MPPC取り外し・ジャンパ・外部接続確認済みとのユーザー申告を前提とした。
本番8081と/home/daqにはアクセスせず、Firmware・MIDAS本体・START排他機構は変更していない。
ASIC Save/Apply、RUN操作、専用HV試験環境作成は行っていない。

HTTPの読取りでRUN186=STOPPED、Transition in progress=Requested transition=0を確認。
/Custom/Pathは `/home/nagao/midas/midas/online/daq_monitor/web`、
`/Custom/EASI Slow Control=easiroc.html`。cache-bypass付き8181応答の
html/js/css/mock-data全4ファイルが開発ファイルとバイト一致した。

ホストのPID/mount namespaceを実プロセスと照合し、同一namespaceの承認済み実行環境から
通常frontendのみ停止・起動した。sandboxから共有メモリODBに接続するclientは起動していない。

- 旧PID912305、実行中バイナリSHA256
  `c435375d03dbda8db94147a55dfdb0eeb6aed3b86abff4f8b8cc51b12f4a9b81`。
- 新PID940529、実行中バイナリSHA256
  `6c3bd8a449bd4ac1eb9caeea6fcf34fa8edb49da9413846087537f40763b7513`。
  FrontendBuild=`Nov  4 2026 09:57:35`。
- `make -C easiroc_frontend all check` 再実施成功。最新buildはup-to-date、
  HV216・ODB adapter2630 checksを含む既存試験全件成功。警告・エラーなし。
- Web構文・DOM/RPC mock119 checks成功。
- Settings全64ch InputDAC=480、LastApplied全64ch=350、ASIC要求/処理済みID=4を保持。
  Settings/HV以外のSettings、LastApplied全体、非HV Commands、Runinfo、他clientのPID/内容を照合し不変。
- 切替直後はHardwareAccessEnabled=false、HV操作capability=false、MeasurementValid=false。
  旧HV要求ID3は処理済みのまま再実行なし。起動によるMADC周期通信なし。
  通常frontendの既存の読取り専用startup診断は実行された。

切替前ODBコピー・稼働旧バイナリ・PID/hash・全観測JSON・GUI表示は、gitignore対象の
`easiroc_frontend/build/backups/hv-increase-trial-20261104/` に保存した。
これは記録用ディレクトリであり、別実験やHV専用frontendは作成していない。

### 操作手順と昇圧結果

CurrentLimit=10 µAとTargetVoltage=0 Vは既存値を確認し、HardwareAccessEnabledのみtrueへ変更。
初回MADCは0.06254 V、0.0364 µA、Valid=true、Errorなし。
すべてのHV要求は通常URL
`http://127.0.0.1:8181/?cmd=custom&page=EASI%20Slow%20Control` の
READ→HVボタン→対象IP入り確認ダイアログから送った。
ブラウザ内部変数を直接確認する最初の事前検査はReferenceErrorとなったが、
その時点ではアクセス許可false・HV要求未送信。画面要素とODBの照合に修正した。

電圧固定なら次段に進まないため、4/8/10 Vの各段を別のVoltage要求として送信し、
毎回書込み後のMADC測定とSucceededを確認してから次の要求を送った。
この実機試験は単一の10 V要求による自動連続Rampとは区別する。
単一要求内の4/8/10 V順序とエラー遮断は先行offline mockで検証済み。
固定判定は前段最終実測からの上昇が0.1 V未満なら中断としたが、今回は全段で上昇した。

時刻は2026-11-04 JST。DAC時刻は書込み応答後のODB設定値を初めて観測した時刻であり、
wireの書込み時刻を直接測定したものではない。

| 要求ID / 操作 | DAC設定観測 | DAC設定 V | 書込み後の最終MADC時刻 | 実測 V | 電流 µA | 結果 |
|---|---|---:|---|---:|---:|---|
| 4 Enable | 10:13:06.512 | 0 | 10:13:08.501 | 0.06254 | 0.0364 | Steady / Succeeded |
| 5 Voltage | 10:13:11.513 | 4 | 10:13:13.501 | 3.63598 | 0.4732 | Steady / Succeeded |
| 6 Voltage | 10:13:16.513 | 8 | 10:13:18.502 | 7.58382 | 0.7644 | Steady / Succeeded |
| 7 Voltage | 10:13:21.513 | 10 | 10:13:23.503 | 9.57646 | 0.9100 | Steady / Succeeded |

実際の設定列は0→4→8→10 V。書込み応答後の設定観測から最終MADC観測まで各約1.99秒、
通常GUIのVoltage要求確認から完了まで各約3.86秒。各段の要求間隔は約5秒。
frontendの1秒service周期と書込み完了後の最低1秒待機により、今回の書込み後検査は約2秒となった。
全電流は10 µA以下、最大0.91 µA。意図的な過電流試験は実施していない。
前回の0.06254 V固定は再現せず、測定値は設定に応じて上昇した。
昇圧完了時のWARNING・通信エラーはなし。外部電圧計による独立測定は未実施。

### 通常降圧と停止異常

要求ID8 Normal Stopで、10→8→6→4→2→0 Vを確認した。
以下のMADCは**次のDACを書き込む前**の測定値であり、同じODBスナップショットには
次の設定値が公開される。次段の設定後の測定と取り違えないこと。

| 測定・次DAC観測時刻 JST | 測定時DAC V → 次DAC V | 実測 V | 電流 µA | 状態・監視 |
|---|---|---:|---:|---|
| 10:13:26.513 | 10 → 8 | 9.58894 | 0.7644 | Ramping / OK |
| 10:13:28.513 | 8 → 6 | 9.33934 | 0.0364 | Ramping / 電圧差>1 V WARNING |
| 10:13:30.513 | 6 → 4 | 8.38254 | 0.0364 | Ramping / 同WARNING |
| 10:13:32.512 | 4 → 2 | 6.50222 | 0.0364 | Ramping / 同WARNING |
| 10:13:34.513 | 2 → 0 | 4.70510 | 0.0364 | Settling / 同WARNING |
| 10:13:36.502 | 0（書込みなし） | 4.50958 | 0.0364 | Settling / 同WARNING |

降圧設定間隔は約2秒、最初の8 V設定から0 V設定まで約8秒。
0 V設定後も実測電圧はゆっくり低下し、≤1 V条件が成立しなかった。
試験スクリプトの待機上限20秒が既存の60秒整定待ちより短く、
10:13:44に追加のNormal Stop要求ID9を1回送信した。
この追加要求では既に設定0 VのためDAC再書込みはなく、整定待ち開始が更新された。
その後は再要求せず、要求ID9の既存60秒timeoutを確認した。
Result=Indeterminate、Error=`HV stop settling timeout; output state unknown`、
Enabled/SetVoltageState=Unknown、MeasurementValid=falseとなった。
最後の整定測定は約2.12590 V、0.0364 µAで、**通常Stop/Disableは成功していない**。
放電が遅い原因や無負荷条件との関係は今回確定していない。

### 異常時shutdown・最終状態

ユーザー指定の異常時停止手順として、通常GUIのEmergency Shutdownを要求ID10で実行。
10:15:11 JST、shutdownの応答でSucceeded/OFF/Disabledを確認し、
MADCは1.64334 V、0.0364 µA。OFFを残留0 Vとは扱わず監視を継続した。

| Shutdown後のMADC時刻 JST | 実測 V | 電流 µA | 結果 |
|---|---:|---:|---|
| 10:15:11 | 1.64334 | 0.0364 | Succeeded / OFF / Valid |
| 10:15:41 | 1.21486 | 0.0364 | Succeeded / OFF / Valid |
| 10:16:11 | 0.90702 | 0.0364 | Succeeded / OFF / Valid、≤1 V確認 |

≤1 V確認後にHardwareAccessEnabled=falseへ戻し、周期監視停止と
LastUpdateUnixTimeが増加しないことを確認した。最終状態は次のとおり。

- CurrentLimit_uA=10、Commands/HV/TargetVoltage_V=0、Operation=Emergency、RequestPayload=`10 Emergency 0`。
- RequestId=LastHandledRequestId=ResultRequestId=10、ActiveRequestId=0。
- Result=Succeeded、Error空、ResultUnixTimeは10:15:11 JSTのまま保持。
- 最後の有効測定は10:16:11 JST、0.90702 V、0.0364 µA。
- 現在MeasurementValid=false、EnabledState/SetVoltageState/RampStatus=Unknown、
  全操作capability=false、BackendMode=`Hardware (inhibited)`。
- MonitoringStatus=`HV hardware access revoked; state unknown, no automatic hardware action`。
  これは現在の監視無効状態であり、成功済みEmergencyの履歴を上書きしていない。
- 通常GUIの最終READでもResult=Succeededを別欄で表示し、測定値/設定値/EnabledはUnknown、
  validityはunavailable or stale、監視停止理由と最終測定時刻を表示。HV全4ボタン無効。
- 最終照合でも修正版PID940529/hash一致、InputDAC Settings=480・LastApplied=350各64ch、
  ASIC要求/処理済みID=4、その他Settings/Commands/LastApplied/Runinfo/他clientは不変。

今回確認できたのはMADCとコマンド応答に基づく最大10 Vまでの試験である。
62 Vまでの実機試験、意図的過電流、外部計器校正、DAQ読出しは行っていない。
通常停止時の放電時間と60秒整定timeoutの適合は未解決事項として残る。


## 通常降圧10 V/step・各段実測確認への変更（2026-11-04、source/mockのみ）

この節は現行開発ソースの通常降圧仕様を更新する。直前の実機試験記録は当時の
2 V/step・最終60秒整定timeout実装の結果として保持する。
今回は開発ソースの修正、build、offline mockのみ実施した。
実機HV操作・実機通信・ODB変更・稼働frontend再起動は実施していない。
対象は `/home/nagao/midas/midas/online`、開発8181用の通常frontend/Webソース。
8181のODB `/Custom/Path`・実配信先は今回再照会していないため、実画面の動作は未確認。
本番8081、/home/daq、Firmware、MIDAS本体、START排他機構は変更していない。

### 通常降圧と状態遷移

`easiroc_hv.{h,cxx}` にnormal_down（通常降圧の要求）とdown_step_pending
（書込み済みの各段の実測確認待ち）を追加した。
不要になったsettle_started、settled_samples、60秒整定timeoutを撤去した。

- 通常Stopは目標0 Vとし、最初の有効測定後、現在のDAC設定から最大10 V下げる。
  62 Vからは52→42→32→22→12→2→0 V、10 Vからは0 Vの一段。
- 書込み後は同じDAC設定を保持し、既存MADC ch3/ch4で電圧・DC-DC電流を約1秒ごとに測定。
  `abs(MeasuredVoltage - SetVoltage) <= 1 V` の有効測定でのみ次段へ進む。
  +1 V/-1 Vの境界は含み、低すぎる実測値も次段への条件とは扱わない。
- 各段の状態はPending / Settling。未追従のまま60秒以上経過しても
  正常な周期測定が継続していればPendingを保持する。経過時間だけのFaultは発生しない。
- 最終設定0 Vでも測定を継続し、有効な実測≤1 Vを確認したらshutdownを実行する。
  shutdownの応答が成功して初めてSucceeded / Disabledとなる。
  新仕様では≤1 Vの1回の有効測定で成立する。応答失敗ではIndeterminate / Unknown。
- Voltage要求による下降も10 V/step・各段±1 V確認を使う。
  途中の非零目標では実測確認後Steady、0 Vへの下降では通常Stop同様にshutdownして完了する。
- Stopの追加要求でも既に書いた段の確認待ちを保持し、次段へ飛ばさない。
- 通常降圧中の電圧未追従は正常な待機として扱い、電圧差だけのWARNINGを抑止する。
  設定値・実測電圧/電流・RampStatus・更新時刻の既存表示を継続する。
- 通常降圧だけはMADC/DAC callback後の待ち開始時刻をservice開始時刻に合わせ、
  callback遅延により次の1秒serviceを飛ばさないようにした。
  昇圧および保護降圧は既存の書込み完了後最低1秒待機を維持する。

状態遷移は `Pending/Ramping → Pending/Settling（各段） → Succeeded/Disabled`。
測定失敗、無効値、通信異常、無効CurrentLimit、周期serviceのタイミング異常、
STOPPED/ASIC排他条件の喪失では、従来どおりFault/Unknownとして次のDACを書かない。
撤廃したのは整定待機の総時間制限であり、異常な測定や通信を正常待機として扱う変更ではない。

### Emergency・過電流保護・維持仕様

Emergencyは通常降圧の±1 V確認、実測値、CurrentLimit、現在のRamp状態を前提とせず、
既存shutdownコマンドを直接実行する。成功時は通常降圧フラグと各段待ちを破棄する。
既存ODB adapterの優先処理でEmergency自身のIDを処理済みとし、ActiveRequestIdを0にする。
以降のtickで旧StopのDAC書込みを再開しないことをpolicyと実adapter mockの両方で検証した。
STOPPED・遷移なし・登録装置・ASIC Applyとの排他などのアクセス条件は維持する。

Ramp中の電流超過は通常の電圧確認ゲートより先に判定する。
超過時は各段の電圧確認待ちを直ちに解除し、保護10 V/stepで0 Vへ進む。
電流が下がっても保護降圧を継続し、自動再昇圧しない。
保護降圧では中間段・最終段とも通常降圧の電圧到達待ちを適用しない。
通常Stopの途中で保護降圧へ移った場合、0 V設定後の電流確認を経てshutdownし、
残留電圧の追従待ちで保護停止を遅らせない。
通常のVoltage昇圧中の保護降圧は既存どおり0 Vへ戻し、自動shutdownを追加しない。

0–62 V範囲、昇圧最大4 V/step・各段の書込み完了後最低1秒待機と電流確認、
CurrentLimit初期値10 µAとODB変更、厳密な `MeasuredCurrent > CurrentLimit` 判定、
定常時超過のWARNINGのみ、ASIC Save/Applyとの独立性を維持する。
初回Enableに既知0 V DACや事前shutdownなどの追加制約は復活させていない。
HardwareAccessEnabled=false時の監視停止と完了済み操作結果の分離も変更しない。

### GUIとmock

`easiroc.html/.js` の通常降圧説明と確認ダイアログを10 V/step・各段実測±1 V確認へ更新。
既存のhv_settling mockを設定52 V・実測60 V・目標0 V・Pendingの通常降圧待ち表示にした。
htmlのasset versionを更新した。新しい警告表示は追加していない。

frontendが同じ要求IDをActive / Pendingと報告し、測定が新鮮な場合は、
GUIの120秒ACK待ち上限を超えてもPendingを表示する。
未確認要求や測定停止・通信異常のACK timeout表示は維持し、異常を隠さない。
待機中もEmergencyボタンを有効に保つ（STOPPED・ASIC排他など既存条件は維持）。
Emergency完了で旧要求待ち表示を解除し、UIからの旧Stop再送を行わない。

### 変更ファイルと検証結果

- policy: `easiroc_hv.h`, `easiroc_hv.cxx`
- mock: `tests/easiroc_hv_test.cxx`, `tests/easiroc_apply_odb_mock_test.cxx`,
  `tests/easiroc_web_mock_test.js`
- Web: `daq_monitor/web/easiroc.html`, `easiroc.js`, `easiroc-mock-data.js`
- 文書: この設計文書と `daq_monitor/web/README.md`

`make -C easiroc_frontend all check` 成功。HV policy/protocol284 checks、
in-memory MIDAS APIへ接続した実ODB adapter mock9354 checks、その他既存試験全件成功。
コンパイラ警告・エラーなし。
`make -C easiroc_frontend check-web NODE=./build/tools/node-v22.16.0-linux-x64/bin/node`
成功。JS構文検査、DOM/RPC mock125 checks。
新しい表示fixtureの検証時に仮想時計とfixture時刻の差で一度失敗したため、
mock測定時刻を仮想時計へ合わせて再検証した。

10→0と62→52→42→32→22→12→2→0の全段、±1 V境界と範囲外保持、
各段65秒以上の正常待機、最終0 Vの≤1 V確認・shutdown応答失敗、
長時間待機中Emergency、旧Stop非再開、待機中の過電流優先、
MADC/通信/無効値による次段禁止を検証。
実adapter mockでも120秒超Pendingと測定更新・Emergency capabilityを保持し、
Emergency後の要求ID・Active=0・旧DAC非再開・監視無効化後の成功履歴保持を確認。
Webでは300秒待機のPending表示・測定値表示・Emergency優先・再送なしを確認。
既存の昇圧4 V刻み、10 µA境界、DAQ/ASIC排他、IP分離、初回Enableも回帰試験で成功した。

この変更による実機の降圧時間・最終Disable・Emergency割込み、実8181画面と
稼働frontendへの反映は今回未検証。前回の実機放電遅延の原因も未確定のまま。


## 通常降圧修正版の反映と実機検証中断（2026-11-04）

### 対象・反映・不変確認

ユーザーの明示許可により、開発8181・実験daq-dev・通常feeasiroc・IP192.168.10.40を対象とした。
MPPC取り外し、ジャンパ・配線確認済みとのユーザー申告を前提とした。
本番8081、/home/daqにはアクセスせず、Firmware・MIDAS本体・START排他機構を変更していない。
ASIC Save/ApplyやRUN操作、専用HV環境の作成も実施していない。

事前にRUN186=STOPPED、Transition in progress=Requested transition=0を確認。
Custom/Pathは `/home/nagao/midas/midas/online/daq_monitor/web`、
EASI Slow Controlはeasiroc.html。cache-bypass付き8181応答のhtml/js/css/mock-data全4ファイルが
開発ファイルとバイト一致した。

`make -C easiroc_frontend all check` 再実行成功（HV284・ODB adapter9354 checks、既存試験全件成功）。
Web構文・DOM/RPC mock125 checks成功。警告・コンパイラエラーなし。
最新buildはup-to-dateで、稼働中プロセスはまだ旧バイナリだったため切り替えた。

- 切替前PID940529、稼働バイナリSHA256:
  `6c3bd8a449bd4ac1eb9caeea6fcf34fa8edb49da9413846087537f40763b7513`。
- 切替後PID957658、稼働バイナリSHA256:
  `f1d91e1c74af7d0c4f1b3a8a1202e7aa028429ac579613d4c499aa061d8206c9`。
- 同一ホストPID/mount namespaceを照合した承認済み実行環境から通常feeasirocのみ停止・起動した。
  sandbox内から共有メモリODBに接続するclientは起動していない。
- 新PIDの実行中exeのハッシュが最新バイナリと一致。heartbeat更新、HV schema/capabilityを確認。
  FrontendBuild文字列は既存feeasiroc.o由来の `Nov  4 2026 09:57:35` のままであり、
  今回の識別には実行中exeのSHA256を用いた。
- 切替直後はHardwareAccessEnabled=false、全操作capability=false、MeasurementValid=false。
  旧HV要求ID10は処理済みのまま再実行なし、MADC周期監視なし。
- InputDAC Settings=480／LastApplied=350各64ch、ASIC要求/処理済みID=4を保持。
  切替後・試験中・終了時に、非HV Settings/Commands、LastApplied全体、Runinfo、他clientを照合し不変。

旧exe、切替前ODBコピー、PID/hash、各試行の観測JSON、GUI表示はgitignore対象の
`easiroc_frontend/build/backups/hv-normal-down-trial-20261104/` に保存した。
途中の試行はattempt-1/attempt-2へ分けて保存し、最終記録と混同しない。
製品ソースの追加修正や再切替は行っていない。

### 実機操作と中断の経緯

CurrentLimit=10 µA、Commands/HV/TargetVoltage_V=0を確認し、HardwareAccessEnabled=trueにした。
HV要求はすべて通常EASI Slow ControlのCustomページからREAD→HVボタン→IP入り確認ダイアログで送信。
10 Vを超える要求、意図的過電流試験は行っていない。
時刻は2026-11-04 JST。

| 要求ID / 操作 | 観測時刻 | DAC設定観測 V | MADC電圧 V | 電流 µA | 結果 |
|---|---|---:|---:|---:|---|
| 初期MADC | 11:53:59 | Unknown | 0.03758 | 0.0364 | 有効、Errorなし |
| 11 Enable 0 V | 11:54:02.747 | 0 | 0.07086 | 0.0364 | Succeeded / Steady / ON |
| 12 Voltage target=10 V | 11:54:04.613 | 0 | 0.07086 | 0.0364 | Pending / Ramping、昇圧段未観測 |
| 13 Emergency | 11:54:05.745 | 保持値0 | 0.07086 | 0.0364 | Succeeded / Disabled / OFF、有効測定あり |
| 14 状態再確認Emergency | 11:56:20.577 | Unknown | 古い値 | 古い値 | Indeterminate、RBCP reply timeout |
| 15 停止処理Emergency | ODB結果時刻11:56:21 | Unknown | 古い値 | 古い値 | Indeterminate、同timeout |

最初のEnable 0 Vは確認ダイアログ受諾から約2.94秒で成功した。
TargetVoltage=10 Vは指定どおり**単一要求**で送信したが、試験スクリプトの
鮮度判定を3秒未満に限定したため、frontendが最初の昇圧前MADCを実行する直前に中断した。
この時点のデータは有限・範囲内で、通信/MADCエラーはなかった。
昇圧要求受付前のキャッシュ値を最初の測定まで保持する時間を試験側が許容していなかった。
Emergency ID13で停止し、有効な実測0.07086 V・0.0364 µAを確認して監視を無効化した。
観測できたDAC設定値は0 Vのみで、4/8/10 Vの段は確認できていない。

試験スクリプトを「初回キャッシュの許容」と「その後の測定更新停止検出」に分離して修正した。
再許可の際、既存policyはHardwareAccessEnabled=falseによるUnknownをFaultとしてラッチし、
そのままでは周期測定を再開しないことを確認した（attempt-2、HV要求なし）。
ODBで状態を偽装して解除せず、通常GUIの明示Emergencyで状態を再確認してから
Enableへ進む手順とした。

この再確認Emergency ID14は、shutdown書込みに対する
`RBCP reply timeout (after 1 attempts)` でIndeterminateとなった。
既存の停止処理として送ったEmergency ID15も同じtimeoutで終了した。
ID14/15の後は、追加昇圧、DAC再書込み、追加Emergency、リセット、電源操作を行っていない。
停止を推測せず、Unknownとして記録して試験を終了した。
通信異常の原因や現在の物理的出力はこの試験では特定していない。

**今回の0→4→8→10 Vの実機段階確認、10→0 V Normal Stop、60秒超Pending、
0 V設定から実測≤1 Vまでの所要時間、自動shutdownは未検証である。**
Normal Stop要求は送信していない。mockでの成功や最初のID13成功を、
最後の通信異常後の物理的停止成功とは扱わない。

### 最終HV・ODB・GUI

失敗時の状態を保存した後、HardwareAccessEnabled=falseへ戻した。
稼働exeのSHA256、保護対象の不変、最後の測定時刻が増加しないことを再確認した。

- Settings/HV: CurrentLimit_uA=10、HardwareAccessEnabled=false。
- Commands/HV: TargetVoltage_V=0、Enable=false、Operation=Emergency、RequestPayload=`15 Emergency 0`。
- RequestId=LastHandledRequestId=ResultRequestId=15、ActiveRequestId=0、InProgress=false。
- Result=Indeterminate、Error=`RBCP reply timeout (after 1 attempts)`、結果時刻11:56:21 JST。
- EnabledState=SetVoltageState=RampStatus=Unknown、MeasurementValid=false、全操作capability=false。
- 最後の有効測定は11:54:05 JSTの0.07086 V・0.0364 µAであり、**現在値ではない**。
- SetVoltage_Vの保持値は0。Variables/HV/TargetVoltage_Vには中断した昇圧要求の10が残る。
  Emergencyはこの内部目標値を更新しない既存実装だが、Ramp/Activeは解除されており再開しない。
  Commandsの目標0とは区別する。Unknownのため保持値を実機設定の読戻しとは扱わない。
- MonitoringStatusは最後のRBCP timeoutを保持。BackendModeはHardware (inhibited)、
  BlockedReasonはHardwareAccessEnabledによる禁止理由となり、失敗履歴を上書きしていない。
- 最終通常GUIのREADでもIndeterminate/RBCP timeout、目標0、測定・状態Unknown、
  unavailable or stale、監視禁止、全HVボタン無効を確認。READによる追加要求なし。
- 最終照合でもPID957658、設定/LastApplied全64ch、ASIC要求ID4、RUN186 STOPPED、
  transitionなし、他clientを保持。ASIC Save/Applyなし。

残課題は、実機の現在のHV出力・接続状態とRBCP応答の確認、その後の昇降圧/Normal Stop実機検証。
停止未確認のため、通信異常後の追加DAC操作や電源操作は独断で行わない。


## Emergency通信異常後の停止確認（2026-11-04 13:56–13:58 JST）

**判定：shutdown ACKと新しいMADC値のみ確認。外部計測による物理HV停止は未確認です。**

対象は開発8181・daq-dev・通常feeasiroc（PID957658）・192.168.10.40です。時刻は2026-11-04 JSTです。

- 初期確認（13:56:07）：RUN186 STOPPED、transition／requested transitionとも0、HardwareAccessEnabled=false。要求／処理済みID15、Active=0、Indeterminate／Unknown。最後の測定は11:54:05で、現在の停止証拠として扱っていません。
- 外部電圧計：現場担当者から「実施できない」と回答。測定点・時刻・電圧の外部測定記録はありません。
- 通信：既存frontendの周期firmware読出し・TCP診断を観測。13:57:16／27／38の3回ともRBCPCommunicationOK=true、TCPReachable=true、LastError空、heartbeat更新を確認しました。別のControllerや直接通信ツールは起動していません。過去ログには11:58:14の復旧後、13:23台の再障害と13:28:14の復旧があり、長期安定性は未保証です。
- 単発shutdown：13:58:08に一時的にHardwareAccessEnabled=trueとし、既存HV mailbox経路へ`16 Emergency 0`を1回だけ発行しました。13:58:09.746にID16 Succeeded、Error空、OFF／Disabledを観測。実行ファイルSHA256は試験時の`f1d91e1c74af7d0c4f1b3a8a1202e7aa028429ac579613d4c499aa061d8206c9`と一致し、この経路は`0x10003 ← 0x00`の1試行で、DAC書込みを伴いません。パケット採取は行っておらず、ACK成功はfrontendの検証済み成功結果に基づきます。
- 新しいMADC：更新時刻13:58:09、電圧**0.06254 V**、電流**0.0364 µA**、MeasurementValid=true、MonitoringStatus=OKを確認しました。これはソフトウェア経由の測定で、独立した物理停止確認とは区別します。
- 終了状態：13:58:09.746にHardwareAccessEnabled=falseへ戻しました。13:58:24の再確認で要求／処理済み／結果ID16、Active=0、InProgress=false、成功履歴保持。状態Unknown／MeasurementValid=falseは監視禁止によるもので、測定時刻は13:58:09のままです。旧要求・Rampの再実行は観測されませんでした。内部目標10 Vの保持値は実行中の要求ではありません。

昇圧・Ramp・DACゼロ設定・ASIC Apply・frontend再起動・Firmware／schema変更は実施していません。非HV Settings／Commands、ASIC LastApplied、Runinfoの不変を照合しました。本番8081・`/home/daq`にはアクセスしていません。

物理停止の独立確認には、現場担当者による適切な定格の計器での出力・残留電圧測定が必要です。追加のHV操作は行っていません。

結果を[easiroc_slow_control.md](/home/nagao/midas/midas/online/easiroc_frontend/docs/easiroc_slow_control.md)へ追記し、この最終報告と同文を[codex.log](/home/nagao/midas/midas/online/codex.log)へ上書き保存しました。


## 断続的ネットワーク障害の読み取り専用調査（2026-11-04）

開発`/home/nagao`・MIDAS 8181・通常feeasirocを対象に調査しました。**原因は未特定です。EASIROC本体または装置側のネットワーク区間の一時的な到達不能を優先します。** 新しいping・ARP送信・TCP接続・RBCP読出しは実施していません。ODB・設定・サービス・実機は変更せず、本番8081・`/home/daq`にはアクセスしていません。

**1. 障害・復旧の時系列（2026-11-04 JST）**

| 記録時刻 | 確認内容 |
|---|---|
| 11:50:34／44 | frontend再起動／最初の通信正常。 |
| 11:56:24.574～11:56:54.571 | 約10秒間隔で4回、firmware読出し3試行失敗＋TCP 1000 ms timeout。 |
| 11:57:04.581～11:58:04.575 | 7回、firmware読出し3試行失敗＋TCP `No route to host`。 |
| 11:58:14.574 | RBCP・TCPとも復旧を記録。 |
| 13:23:14.581～13:23:34.579 | 3回、firmware読出し3試行失敗＋TCP timeout。 |
| 13:23:44.578～13:28:04.574 | 27回、firmware読出し3試行失敗＋TCP `No route to host`。 |
| 13:28:14.573 | RBCP・TCPとも復旧を記録。 |
| 13:57／13:58 | 前回作業の観測では通信正常、Emergency ID16成功と新しいMADC値を確認。今回の通信試験ではない。 |

診断失敗は計11回・30回、RBCP試行数は各33・90回です。ログはworkerの結果を後から公開する時刻で、実際の障害開始・復旧時刻ではありません。正常継続時はログを省略します。[MIDASログ](/home/nagao/midas/midas/log/midas.log:25343)

診断は**RBCP→TCPの順次実行**です。同じ診断回で双方の失敗を確認していますが、物理的に同時に障害が始まったか、どちらが先かは分かりません。firmware読出しは`0xf0000000`の6 byte、最大3試行・各1秒、続くTCP 24は1秒です。通常の失敗処理は合計約4秒で、結果公開までさらに周期待ちがあります。[診断worker](/home/nagao/midas/midas/online/easiroc_frontend/feeasiroc.cxx:621)

保存ODBでは11:54～11:57のRUN186はSTOPPED、transition／requested transitionとも0。重点時間帯のMIDASログにはRUN遷移や他clientの障害記録はありません。11:56の保存スナップショットではheartbeat更新を確認できますが、13:23～28の連続heartbeat記録はありません。約10秒ごとの失敗記録はworkerが完了・再起動し続けた証拠で、worker停止とは整合しません。

**2. OS／ネットワークの確認結果**

ホスト`nexdaq1`の14:00頃の現在値です。

- 経路：`192.168.10.40 dev enp5s0f0 src 192.168.10.1`。同一`/24`内の直結経路でgatewayなし。NetworkManager設定はmanual、never-default=yes。
- NIC：Intel X710、PCI接続、driver i40e。10,000 Mb/s・Full duplex・link UP。RX／TX errors・drops、CRC、port timeoutは取得値で0。carrier累積値はup=1／down=1で、障害時刻を示す情報ではありません。ethtoolの一部照会は権限エラーでしたが、上記リンク・統計値は取得できました。
- neighbor：対象は`7c:f0:98:01:16:40`、REACHABLE。これは現在値で、障害時のMAC・ARP応答やIP重複の有無は不明です。同NICの`.11`・`.26`は現在FAILEDですが、いつから・なぜか不明で、同時障害とは判定できません。
- 重点時間帯のjournalにenp5s0f0のlink down/up、driver reset、設定変更は見つかりません。11:54:28／13:24:28のDHCP更新は別NICのenp3s0で、同じIPのlease更新です。対象経路の変更を示す証拠ではありません。
- 13:23:02にUSBストレージのresetがありますが、対象NICはPCI接続です。同じresetは正常通信期間も含め約10分ごとに発生しており、通信断の原因とする根拠はありません。

既存[sysstat記録](/var/log/sa/sa04)の11:50～13:30では、約10分粒度でCPU idle約93%、利用可能メモリ約15～16 GB、全ホストsocket数1658～1683でした。対象NICの記録されたerror/drop率は0です。短時間の異常は除外できません。現在のfrontendはRSS約18 MB、FD 8個（socket 5個）、open-file上限4096で、FD枯渇の証拠はありません。障害時のfrontend別CPU・FD履歴はありません。

`connect: No route to host`はLinuxの**EHOSTUNREACH=113**を`strerror()`で表示したものです。connect直後のerrno、または非同期connect後の`SO_ERROR`から生成します。保存文字列では両者を区別できません。ARP解決失敗やICMP unreachable等でも起こり得るため、「経路表からrouteが消えた」とは断定できません。[生成箇所](/home/nagao/midas/midas/online/easiroc_frontend/tcp_probe.cxx:47)

UDPは試行ごとにsocketを生成し、例外時もclose。TCPも成功・失敗時にcloseします。診断workerは最大1つで、失敗してもfrontendは継続し、通信フラグ・LastError・firmware読出しの有効性を更新します。旧Controllerも1秒×最大3試行、試行ごとのsocket生成・closeで、根本的なtimeout方式は共通です。現行のEINTR再待機は絶対期限方式ではありませんが、今回のログ周期に長時間停止の兆候はありません。[現行RBCP](/home/nagao/midas/midas/online/easiroc_frontend/rbcp.cxx:88)、[旧RBCP](/home/nagao/midas/midas/online/easiroc_frontend/reference/controller_20170425/RBCP.rb:106)

**3. 原因候補の優先順位と根拠**

| 優先 | 候補 | 支持する証拠／反証・限界／次の切り分け |
|---|---|---|
| 1 | EASIROC本体・SiTCPの一時停止 | UDP・TCP双方の失敗と自然復旧が整合。装置状態・稼働時間・電源記録がなく未証明。装置側リンク・状態と受信ARPを観測する。 |
| 2 | 装置側ケーブル・スイッチport等 | ホストNICがUPでも下流の断は起こり得る。ホストNIC自体の断・CRC異常の証拠はない。スイッチの対象port・uplinkログとカウンタが必要。 |
| 3 | ARP解決失敗・IP重複 | timeout後のEHOSTUNREACHはARP失敗と整合。ただしARP失敗は装置停止の結果でもあり、独立原因とは限らない。MAC変動・重複応答の保存証拠なし。ARPとneighbor遷移を観測する。 |
| 4 | ホスト経路・設定 | 現在の直結静的設定、対象NICの変更ログ不在は支持しない。障害時の経路スナップショットがなく完全除外不可。次回はroute／link変更を受動記録する。 |
| 5 | frontend通信処理・資源 | 診断周期継続、資源逼迫なし、OS到達不能エラーは単純なACK検証不良よりネットワーク側を支持。ただし周期TCP接続が装置へ与える影響は未評価。実パケットとworker時刻を照合する。 |

1～3の間は現証拠だけでは区別できません。同じNICを使うHUL等について、重点時間帯に対応する通信成功・失敗の連続記録やスイッチ記録は得られていません。他装置のエラーログがないことを正常の証拠にはしていません。

**4. 原因特定に不足する情報**

障害中のARP／ICMP／UDP／TCPパケット、neighbor・MAC変化、スイッチの対象portとuplinkの履歴、EASIROCの電源・リンク・SiTCP状態、同NIC上の他装置の同時観測が不足しています。ログには試行ごとの送信時刻・送信元port・errno数値やworker所要時間もありません。したがって、リンク断・パケット喪失・IP競合は確定事項ではありません。

**5. 次段階の最小限の通信試験（今回は未実施）**

まず別途承認の下で、既存frontendの周期診断を対象に受動キャプチャと`ip monitor link route neigh`を同時記録します。新しい制御接続を加えず、ARP要求への応答、RBCP要求／ACK、TCP SYN／応答、ICMP送信元を比較するのが最小です。スイッチ記録との時刻照合も行います。

追加送信が必要なら、通常frontendを唯一の経路としてfirmware 6 byte読出しとTCP 24接続を各1回に限定し、失敗時に終了する試験を別途承認します。TCP接続は装置の接続枠・状態に影響し得ます。現行workerはRBCP最大3試行なので、無再試行の試験と同一視しません。旧Controller併用、HV／DAC操作、再起動、設定変更は行いません。ここで実通信試験に進まず終了します。

物理HV停止の独立確認は引き続き未実施です。結果を既存[easiroc_slow_control.md](/home/nagao/midas/midas/online/easiroc_frontend/docs/easiroc_slow_control.md)へ追記し、本報告と同文を[codex.log](/home/nagao/midas/midas/online/codex.log)へ上書き保存しました。


## 受動ネットワーク監視の準備と権限不足（2026-11-04）

**監視は開始できていません。tcpdumpのキャプチャ権限不足で終了しました。** Linuxが`socket: Operation not permitted`を返しました。capability付与はなく、`sudo -n`もパスワードを要求するため、権限設定を変更せず停止しています。

- 最終起動試行：2026-11-04 **14:09:18 JST**。監視スクリプトPID **979339**、tcpdump PID **979348**は終了済みです。既存監視との重複、終了後の監視子プロセス残存はありません。
- 保存先：[build/network-diagnostics/20261104T140918+0900](/home/nagao/midas/midas/online/build/network-diagnostics/20261104T140918+0900)。初期・終了状態、実行条件、権限エラー、終了理由を保存しました。**取得パケット種別はなし、pcap未作成**です。
- 初期状態：nexdaq1、`192.168.10.40 dev enp5s0f0 src 192.168.10.1`。対象MACは`7c:f0:98:01:16:40`、neighborはREACHABLE。NICはUP、promiscuity=0、RX／TX error・dropは0。保存先の空きは約316 GBでした。
- frontend：PID957658が継続稼働。14:06:51の開発8181読取りでheartbeat更新、RBCP／TCP正常を確認しました。既存診断周期は10秒です。新しい実機通信要求は送っていません。

[monitor_easiroc_network.py](/home/nagao/midas/midas/online/scripts/monitor_easiroc_network.py)を準備しました。Python構文・BPFのオフラインコンパイル、権限不足時の終了処理を確認しました。ip monitorの短時間の受動購読も権限エラーなく終了しました。権限を伴う継続キャプチャと実際のローテーションは未検証です。

スクリプトは非promiscuousの`tcpdump -p`を使用し、対象IPのARP、RBCP UDP 4660、TCP、ICMP、および引用された元IPが対象のICMP到達不能等に限定します。pcapは10 MB×10ファイルの循環保存で、切替時に1パケット分超過し得ます。状態変更の受動記録、既存feeasirocログ、初期・終了のroute／neighbor／link・NIC統計を並行保存し、テキストにJSTを付けます。pcapの時刻は解析時にJST表示します。

最大4時間以内に子プロセスを終了する設計で、停止シグナル・子プロセス異常終了・空き容量150 MB未満でも終了します。補助ログも循環保存し、1回の監視は概ね125 MB以内です。ローテーションにより古い記録は上書きされます。保存物は既存のbuild除外規則でGit管理対象外です。

開始には、担当者が**nexdaq1の実ホストシェル**で次を実行する必要があります。管理者権限はキャプチャ開始と子プロセス管理に使用し、tcpdumpはリポジトリ所有者へ権限を落とします。端末を維持して実行し、途中停止はCtrl-Cで行います。

```bash
sudo /usr/bin/python3 /home/nagao/midas/midas/online/scripts/monitor_easiroc_network.py --seconds 14400
```

終了後は`finished.json`、tcpdumpのdrop統計、循環pcapの残存時間範囲を確認し、JSTでRBCP要求／ACK、TCP SYN／応答、対象ARP、MAC変化、ICMPとlink／neighbor変化を既存診断ログに照合します。送信側でのパケット観測を装置到達の証明とはせず、未観測だけでも装置故障と断定しません。

HV・DAC・ASIC操作、ネットワーク設定変更、サービス再起動、本番8081・`/home/daq`への変更は行っていません。監視方法を既存[easiroc_slow_control.md](/home/nagao/midas/midas/online/easiroc_frontend/docs/easiroc_slow_control.md)へ追記し、本報告と同文を[codex.log](/home/nagao/midas/midas/online/codex.log)へ上書き保存しました。


## 受動監視pcapの初回解析（2026-11-04 14:18時点）

**解析範囲では通信障害の再発はなく、原因は未特定です。後続の受動監視は継続しています。** 新しいping・RBCP・TCP接続、HV操作、ODB変更、frontend再起動は行っていません。本番8081・`/home/daq`は変更していません。

**1. 監視・パケット取得状況**

指定の監視PID982904は、2026-11-04 **14:16:23 JST**に停止シグナルを受けて終了済みでした。今回の解析では停止していません。指定フォルダのtcpdump終了統計は64 packets captured／64 received by filter／**kernel drop 0**です。

現在は後続監視PID **982980**、tcpdump **982988**、ip monitor **982992**、ログ追尾 **982993**が稼働しています。保存先は`build/network-diagnostics/20261104T141647+0900/`です。稼働中tcpdumpのdrop統計はまだ出力されておらず、0とは判定していません。

14:18:20 JSTにpcapのコピーを作り、その固定スナップショットを解析しました。不完全な末尾レコードはありませんでした。

| 取得元フォルダ | パケット時刻（JST） | 件数 | 内訳 |
|---|---|---:|---|
| `20261104T141503+0900` | 14:15:04.571349～14:16:14.571627 | 64 | UDP 16、TCP 48 |
| `20261104T141647+0900` | 14:16:54.571335～14:18:14.571639 | 72 | UDP 18、TCP 54 |

監視の切替により14:16:23～47頃は取得に空白があります。コピー、ハッシュ、集計は[解析フォルダ](/home/nagao/midas/midas/online/build/network-diagnostics/analysis-20261104T141820+0900)に保存しました。原本は変更していません。

**2. 通信障害再発の有無**

読取り時点の[MIDASログ](/home/nagao/midas/midas/log/midas.log:25391)には14:15以降の新しい通信異常・復旧記録はなく、最後の復旧記録は13:28:14です。正常継続時はログを省略する実装ですが、今回のpcapには約10秒周期の正常応答が直接記録されています。取得空白とスナップショット以降は、この解析の判定範囲外です。

**3. RBCP・TCP・ARPの解析結果**

- **RBCP：要求17件／対応ACK17件。** すべて`192.168.10.1 → 192.168.10.40:4660`のfirmware読出しで、address=`0xf0000000`、length=6、packet ID=0。応答のcommand=`0xc8`、ID・address・長さ・戻りportの対応を確認しました。返却データは全件`40 10 20 15 02 20`で一定。観測往復時間は**51～68 µs**、再試行は観測していません。RBCP書込み要求はありません。
- **TCP/24：SYN17件すべてに対応するSYN-ACK。** 接続成立後、PC側FIN→装置側FIN/ACK→PC側ACKで終了し、TCPデータは0 byte。SYN応答時間は**55～77 µs**です。RSTや再送は観測していません。既存frontendの接続診断と整合します。
- **ARP：要求・応答とも0件。** 初期neighborは対象MACでREACHABLEで、キャッシュ利用中と整合しますが、この範囲ではARP交換の成立自体は確認できません。
- **MAC：装置からの全IPv4応答が`7c:f0:98:01:16:40`。** 観測中のMAC変化はありません。IP重複の完全な否定にはなりません。
- **ICMP：0件。** unreachableは観測していません。
- **link／route／neighbor：対象経路の変化記録なし。** 指定監視の開始・終了状態は直結経路、NIC UP、同じneighbor MACで一致し、NIC error/dropの増加もありません。後続network.logの14:17:06の変化は別NICのwlp6s0で、対象enp5s0f0の変化ではありません。

送信UDPにtcpdumpの`bad udp cksum`表示がありますが、NICのTX checksum offloadが有効です。全件に正常ACKが返っており、送信前キャプチャに伴う表示と整合します。実際の回線上のchecksum破損とは判定していません。

**4. 原因候補と根拠**

この取得範囲ではPC側の要求と装置側の応答が対で確認でき、継続的な装置停止・ACK欠落・TCP無応答の証拠はありません。過去の断続的な本体／SiTCP停止、装置側リンク障害、ARP解決失敗・IP競合は、障害中のpcapがないため区別できません。ホスト経路変化やfrontend資源問題を新たに支持する証拠もありません。

**5. 次に必要な調査**

既存監視を継続し、再発時のMIDASエラー時刻を中心にpcap、network.log、スイッチ側port履歴を照合します。要求の送信側観測、ACK帰還、SYN応答、ARP応答、MAC変化、ICMPの送信元を順に確認します。パケット未観測だけで装置故障とは断定せず、capture dropと監視の空白も確認します。監視終了後には終了統計と最終NIC状態を追加確認する必要があります。

解析結果を既存[easiroc_slow_control.md](/home/nagao/midas/midas/online/easiroc_frontend/docs/easiroc_slow_control.md)へ追記し、本報告と同文を[codex.log](/home/nagao/midas/midas/online/codex.log)へ上書き保存しました。


## 受動監視終了後の全pcap解析（2026-11-04 14:23時点）

**取得範囲では通信障害の再発はありません。原因は未特定です。指定監視は既に終了していました。**

**1. 監視・取得範囲**

監視PID982980は2026-11-04 **14:22:06 JST**に`stop signal received`で終了しています。tcpdump982988は終了コード0、ip monitor982992とログ追尾982993も終了済みです。今回、停止・再起動操作は行っていません。14:23頃の確認では別のtcpdump／ip monitorも見つかりませんでした。通常feeasiroc PID957658は稼働継続しています。

対象フォルダの全pcapは`capture.pcap0`の1ファイル、19,032 byteです。**256 packets captured／256 received by filter／kernel drop 0**で、ファイル内も256件、不完全レコード・切詰めパケットは0件でした。10 MBに達しておらず、ローテーション・循環上書きの兆候はありません。

パケット範囲は**14:16:54.571335～14:22:04.571641 JST（約5分10秒）**。約10秒周期の診断32回が連続し、周期の欠落はありません。監視開始14:16:47から最初の診断まで、最後の診断から停止14:22:06までの間隔も周期と整合します。前回監視との切替空白14:16:23～47頃、および今回の終了後は取得対象外です。

固定コピー・SHA256・集計を[解析フォルダ](/home/nagao/midas/midas/online/build/network-diagnostics/analysis-20261104T142319+0900)へ保存しました。原本は変更していません。

**2. MIDASログとパケットの照合**

読取り時点のMIDASログには14:15以降の通信異常・復旧記録はありません。正常継続時はログを省略するため、正常性の根拠には以下の実パケットを用いました。

| 項目 | 全取得範囲の結果 |
|---|---|
| firmware RBCP | 要求32件、対応ACK32件、未対応0件 |
| RBCP応答時間 | 53～71 µs、中央値60.5 µs |
| RBCP要求間隔 | 9.999968～10.000022秒、欠落周期なし |
| TCP/24 | SYN32件、対応SYN-ACK32件。全32接続が成立・FINで終了 |
| SYN応答時間 | 50～77 µs、中央値61 µs |
| TCP再送・RST・データ | 再送・RSTなし、データ0 byte |
| ARP／ICMP | どちらも0件、unreachable未観測 |
| 装置応答元MAC | 全件`7c:f0:98:01:16:40`で一定 |

RBCPは全件address=`0xf0000000`、length=6、ID=0の読出しです。ACKのcommand・ID・address・長さ・対応portを照合し、返却値は全件`40 10 20 15 02 20`でした。書込み要求はありません。

RBCP中央値は前半59 µs、後半61.5 µsで、最大値はいずれも71 µs。前回正常値51～68 µsとも近く、持続的な遅延増加は見られません。障害自体がないため、「障害直前の兆候」は評価できません。

**3. ネットワーク状態と原因の切り分け**

開始・終了とも`192.168.10.40 dev enp5s0f0 src 192.168.10.1`、neighborは同じMACでREACHABLE、NICはUPでした。RX／TX error・drop、CRC、driverのport timeoutは0のままでした。network.logに対象NIC・経路・neighborの変化はなく、記録された変化は別NICのwlp6s0のみです。

この範囲では要求がホスト側で観測され、対応する装置応答も戻っています。「要求未送信」「応答なし」「MAC変化」を支持する証拠はありません。ARPパケットはないため、ARP交換の成立や、過去のARP失敗・IP重複は判定できません。ホスト側送信キャプチャだけを回線上の送出・装置到達の証明とはしていません。

今回の正常記録から、過去のEASIROC／SiTCP停止と装置側ケーブル・スイッチ・ARP障害を区別することはできません。ホストNIC・設定やfrontend処理を原因として優先する新しい証拠もありません。

**4. 次に優先する最小限の調査**

優先するのは、**装置側のケーブル・スイッチ・ARPとEASIROC／SiTCPを区別する、障害中の受動記録**です。今回の約5分では過去の断続障害を捉えていません。別途監視を行う際はpcapと対象スイッチport履歴を時刻同期して残し、再発前30秒～復旧後30秒の要求・ACK・ARP・ICMP・neighbor変化を照合するのが最小です。ホスト設定変更やfrontend修正を先行させる根拠はありません。

今回は監視の再開、新規ping・RBCP・TCP通信、HV・DAC・ASIC操作、ODB・ネットワーク設定・サービス変更を行っていません。本番8081・`/home/daq`は変更していません。

結果を既存[easiroc_slow_control.md](/home/nagao/midas/midas/online/easiroc_frontend/docs/easiroc_slow_control.md)へ追記し、本報告と同文を[codex.log](/home/nagao/midas/midas/online/codex.log)へ上書き保存しました。

## HVパラメータ変更・周期仕様の確認待ち（2026-11-04、開発source/mockのみ）

以下は当時の経緯。周期仕様の確認待ちは、末尾の「HV段階別測定方式」で解消した。

CurrentLimit_uAの新規キー初期値を20 µAへ変更。既存キーは
`ensure_odb_value()`が保持するため、既存値は上書きしない。今回の開発8181の
読み取りでは10 µAであった。ODB変更・frontend再起動・実機操作は実施していない。

通常降圧の各段到達判定を設定±5 V、0 V設定後のshutdown前判定を実測≤5 Vへ変更。
**最終段で最大5 V程度の残留電圧を許容し得る。shutdown ACKは物理停止の独立確認ではない。**
Enableは0 V指定、受入残留電圧≤1 Vを維持。設定値不明時のStop受入も≤1 Vを維持する。
通信・測定・shutdown ACK失敗時のFault/Indeterminate、過電流保護は従来どおり。
GUIはEnable／Disableへ改名し、Disableダイアログに±5 V／≤5 V／Shutdownを記載。

測定周期は未変更（従来のランプ1秒、通常監視30秒）。要求された3秒測定と
「過電流保護を遅延させない」は、現行の1秒MADCによる過電流検出ではそのまま両立しない。
保護測定を1秒維持して通常の段進行を3秒にするか、全MADCを3秒にして検出周期の延長を
許容するかをユーザーへ確認中。応答待ちのため、この部分を完了扱いしない。

検証：make all check成功、HV287 checks、ODB adapter9386 checks、既存試験全件成功。
check-webはJS構文と145 checks成功。git diff --check成功。コンパイラ警告・エラーなし。
実機の通信・HV動作と3秒周期は未検証。新バイナリはbuild済みだが稼働frontendは未切替。
適用には周期仕様確定後の承認済みfrontend切替と、必要なら既存ODB CurrentLimitの明示変更が必要。
本番8081・/home/daqには変更していない。

## HV段階別測定方式（2026-11-04、開発source/offline mockのみ）

今回の指示で固定3秒測定は採用しないことが確定し、前節の確認待ちは解消した。
測定は各DAC段の進行判定と到達待ちに従う。通常監視は従来の30秒周期を保持する。

- 昇圧は4 V/step（最終段は目標まで）。DAC通信完了後に約1秒待ってMADCで
  電圧・電流を測定し、電流がCurrentLimit以下の場合に次段へ進む。最終段も測定後に完了する。
- 通常降圧は10 V/step（最終段は目標まで）。各DAC段の後にMADCで測定し、
  実測が設定±5 V以内の場合のみ次段へ進む。未到達なら約1秒間隔で再測定する。
  通常降圧の次回測定基準をservice開始時刻からDAC通信完了時刻へ修正し、
  通信時間を含めてもDAC完了後の約1秒待機を確保する。
- 0 Vへの通常降圧は実測≤5 Vの有効測定後にShutdownし、ACK成功後に完了する。
  到達待ちは従来どおり期限を設けない。各進行判定で過電流を先に判定し、
  検出時は既存の保護降圧へ移行して通常の電圧到達待ちを迂回する。
- 明示EmergencyはMADC測定や到達待ちを挟まずshutdownを要求する既存経路を維持する。
  通信・測定・時刻異常やshutdown ACK失敗はFault/Indeterminateとなり、正常完了にしない。
- 新規CurrentLimit_uA初期値20 µA、既存値を上書きしないschema、GUIのEnable／Disableと
  ±5 V／≤5 V表示、Enableと設定不明Stopの受入≤1 Vを維持する。

検証：`make -C easiroc_frontend all check` 成功。HV304 checks、in-memory ODB adapter9386 checks、
既存C++試験全件成功。今回のコンパイル警告・エラーなし。
各昇圧段（最終段を含む）の過電流、通信時間を含む降圧後待機、約1秒の到達再測定、
±5 V境界、0 V設定後の5.001 V待機／5 V Shutdownを確認した。
既存の長時間到達待ち、測定・通信失敗、Emergency割込みも成功。
`check-web`はPATHにnodeがなく初回起動失敗したが、既存の同梱Nodeを指定した
`make -C easiroc_frontend check-web NODE=./build/tools/node-v22.16.0-linux-x64/bin/node`
でJS構文とGUI mock145 checks成功。`git diff --check`成功。

対象は開発 `/home/nagao` のソースとoffline mockのみ。GUIソースは今回未変更で、
稼働8181のserved files検証や配備は行っていない。実機HV操作・実機通信・ODB変更・
frontend再起動は行っておらず、既存ODB値10 µAも変更していない。
本番8081・`/home/daq`は変更していない。ビルド済みバイナリの実機動作は未検証。

## 開発8181反映前確認・HV ONによる中断（2026-11-04）

**HVがON表示のため、指定の安全条件に従い開発8181への反映を中断しました。** frontend停止・起動、ODB変更、HV操作は行っていません。

開発実験`daq-dev`、対象IP`192.168.10.40`はSTOPPED、遷移要求・処理とも0。HVは`EnabledState=ON`、`InProgress=false`、設定0 Vです。最新の有効測定は2026-11-04 19:48:29 JST、0.07918 V／0.0364 µAでした。ONはACK済みコマンドに基づく状態で、低い実測電圧を理由にOFFとは扱っていません。

- frontend PID：前後とも`957658`（再起動なし）。実行ファイルは`/home/nagao/midas/midas/online/easiroc_frontend/bin/feeasiroc (deleted)`。稼働SHA256は`f1d91e1c74af7d0c4f1b3a8a1202e7aa028429ac579613d4c499aa061d8206c9`。
- 反映候補：同ディレクトリのビルド済みバイナリ。SHA256は`1bd248a9019b6de3d80d7bb3815b84e91fb75b4754a82ab684c32c36611e579b`。稼働版とは異なります。
- 既存起動方法：`MIDAS_EXPT_NAME=daq-dev scripts/start_feeasiroc.sh -D`。
- ODB変更：なし。`/Equipment/EASIROC/Settings/HV/CurrentLimit_uA`（DOUBLE）は10→10 µA、同階層の`HardwareAccessEnabled`はtrue→trueです。

`make all check`成功（HV304項目、ODBモック9386項目）、GUIモック145項目成功。ビルドエラー・警告なし。8181の`/Custom/Path`は開発webディレクトリで、配信HTML／JS／CSS／mock-dataの4ファイルは開発ファイルと一致しました。Enable／Disable表示、昇圧4 V・降圧10 V、±5 V・最終≤5 V、通常監視30秒、既存Emergency経路をソース・モックで確認しました。

**変更版HV制御・CurrentLimit 20 µAでGUI手動試験できる状態への反映は未完了です。** HVがOFFであることを確認した後に反映を再実施する必要があります。実機動作・GUIボタン操作は未検証です。他サービス・ASIC等の設定・本番8081・`/home/daq`は変更していません。

本報告と完全に同じ文章を[codex.log](/home/nagao/midas/midas/online/codex.log)へ上書き保存しました。

## 段階別HV制御の開発8181反映完了（2026-11-04）

**開発MIDAS 8181（daq-dev）への反映が完了し、GUIから手動試験できる状態です。** 対象は通常feeasiroc、192.168.10.40です。

反映直前にSTOPPED・遷移なし・HV OFF・昇降圧なし、有効な実測0.07502 Vを確認しました。同一ホストPID／mount namespaceから、既存の`MIDAS_EXPT_NAME=daq-dev scripts/start_feeasiroc.sh -D`で通常feeasirocだけを再起動しました。

- PID：`957658` → `1043613`。旧プロセス終了、新client登録、heartbeat更新を確認。
- バイナリ：`/home/nagao/midas/midas/online/easiroc_frontend/bin/feeasiroc`。
- 旧稼働SHA256：`f1d91e1c74af7d0c4f1b3a8a1202e7aa028429ac579613d4c499aa061d8206c9`。
- 新稼働SHA256：`1bd248a9019b6de3d80d7bb3815b84e91fb75b4754a82ab684c32c36611e579b`。ビルド済み変更版と一致。
- ODB：`/Equipment/EASIROC/Settings/HV/CurrentLimit_uA`（DOUBLE）を**10→20 µA**へ変更し、読戻し確認。同階層の`HardwareAccessEnabled`は**true→true**を維持。

RBCP通信・TCP到達診断は正常、取得動作は停止中です。再起動後の有効測定は19:54:48 JST、0.07502 V／0.0364 µAでした。HV状態は再起動に伴い設計どおりUnknownです。GUIのEnableは利用可能で、電圧変更はEnable完了後に利用可能になります。UnknownをOFFとして復元する操作は行っていません。

8181の配信HTML／JS／CSS／mock-dataは開発ファイルと一致。Enable／Disable表示、昇圧4 V/step、降圧10 V/step、±5 V到達判定、最終≤5 V、通常監視30秒、既存Emergency経路をソース・モックで確認しました。固定3秒測定はありません。

`make all check`成功（HV304項目、ODBモック9386項目）、GUIモック145項目成功。警告・エラーなし。CurrentLimit以外のSettings、Commands、ASIC LastApplied、Runinfo、他client登録は反映前後で不変でした。

HV Enable・電圧変更・Disable・Emergency、GUIボタン操作試験、ASIC等の設定変更は行っていません。実機の昇降圧・過電流保護・到達待ち・最終Shutdown動作は未検証です。他frontend・Logger・mhttpdは再起動せず、本番8081・`/home/daq`は変更していません。

反映前データと旧バイナリは`easiroc_frontend/build/backups/hv-stage-deploy-20261104T195417/`に保存しました。結果を既存ドキュメントへ追記し、本報告と完全に同じ文章を[codex.log](/home/nagao/midas/midas/online/codex.log)へ上書き保存しました。

## HV測定後の非ブロッキング1秒待機（2026-11-04、開発8181）

`easiroc_hv.{h,cxx}` のStateへ `step_sample_pending` と `step_wait` を追加した。
DAC書込み完了から約1秒待つ既存処理は1回だけ維持する。各段のMADC測定が成功し、
電流がCurrentLimit以下（通常降圧はさらに実測が設定±5 V以内）で、次段がある場合に
測定完了時刻から約1秒待ち、次のDACを設定する。待機中は関数からMIDASへ戻り、
追加のMADC測定やブロッキングsleepは行わない。frontend serviceの刻みによって
実際の待機時間は1秒を超え得る。最初のDAC前の既存確認と、最終段の完了処理は保持する。

未到達の降圧は従来どおり約1秒で再測定し、到達した測定の後に1秒待機する。
最終0 V段では実測≤5 V確認後、そのままShutdownし、追加の測定後待機を設けない。
測定失敗はFault/Indeterminateで停止し、過電流は新しい待機に入らず従来の保護降圧へ進む。
待機終了時にもCurrentLimitと保存測定の有効性を確認し、無効なら次段へ進めない。
通常Stopで待機中のDAC予約を取り消して再確認し、Emergency／Fault／完了時には
待機フラグを消去する。Emergency mailboxはtickより先に処理される既存経路を維持し、
新しい待機中もshutdownを直接要求できる（既存service間隔・通信所要時間は残る）。

4 V/step・10 V/step、CurrentLimit20 µA、±5 V／最終≤5 V、通常監視30秒、
既存の鮮度判定、GUI／ODB schemaは変更していない。固定周期の測定は新設していない。
Unknown表示の原因が待機不足であることや、この変更で改善することは実機未検証。

`make -C easiroc_frontend all check check-web NODE=./build/tools/node-v22.16.0-linux-x64/bin/node`
成功。HV389 checks、in-memory ODB adapter9417 checks、GUI mock145 checks、既存C++試験全件成功。
コンパイラ警告・エラーなし。新タイミングに合わせ既存試験時刻を更新し、測定通信時間を
含む各1秒待機、待機中のゼロI/O、追加測定なしの次DAC、昇降圧の両待機中Emergency、
測定失敗・過電流・無効CurrentLimit、実ODB adapterの待機途中Emergencyを検証した。

反映前にdaq-dev／開発8181／192.168.10.40、RUN186 STOPPED、遷移要求・処理0、
HV OFF、InProgress=false、有効実測0.10830 V、CurrentLimit20 µA／HardwareAccessEnabled=trueを確認。
同一ホストPID/mount namespaceを照合した環境から通常feeasirocのみ既存起動方法で再起動した。
PID1043613 → 1044582、実行中exeのSHA256は
`d00188ea0fc3c4c86e2da169a91aa8b0324dfdd140ab503a4567d333403dd15a`で変更版と一致。
反映前ODB・旧exeは `build/backups/hv-sleep-deploy-20261104T201024/` に保存した。
HV操作／GUIボタン操作は行っていない。本番8081・`/home/daq`は対象外で変更していない。

### 反映後確認と最終報告

**測定後の1秒待機を追加し、開発MIDAS 8181の通常feeasirocへ反映しました。**

- 昇圧：DAC設定→既存の1秒待機→MADC測定・過電流判定→成功時に1秒待機→次段。
- 通常降圧：DAC設定→既存の1秒待機→MADC測定→設定±5 Vへ到達後に1秒待機→次段。未到達時は約1秒で再測定し、最終0 V段は実測≤5 VでShutdownします。
- 追加待機は状態管理で実装し、ブロッキングSleepや固定周期測定は追加していません。Emergencyは待機を取り消して既存のshutdown経路を実行できます。測定失敗時は停止し、過電流時は通常段進行を行わず既存の保護降圧へ移ります。

`make all check`とGUIテスト成功（HV389項目、ODBモック9417項目、GUIモック145項目）。通信時間を含む待機、待機中のEmergency、測定失敗・過電流、最終Shutdownを確認しました。コンパイラ警告・エラーなし。

反映直前にSTOPPED・遷移なし・HV OFF・昇降圧なし、有効実測0.10830 Vを確認。通常feeasirocだけを再起動し、PIDは**1043613→1044582**です。稼働バイナリのSHA256は`d00188ea0fc3c4c86e2da169a91aa8b0324dfdd140ab503a4567d333403dd15a`で変更版と一致しました。

反映後はRBCP／TCP診断正常、有効測定0.09998 V／0.0364 µA（20:10:55 JST）。HV状態は再起動により設計どおりUnknownですが、測定は有効でEnableは利用可能です。CurrentLimit **20 µA**、HardwareAccessEnabled **true**を維持し、ODB設定・GUI・通常監視30秒・既存の鮮度判定は変更していません。Settings、Commands、ASIC LastApplied、Runinfo、他client登録の不変も確認しました。

**HV操作・GUIボタン操作試験は行っていません。断続的なMeasured Voltage／CurrentのUnknown表示が改善するかは、ユーザーによる実機手動試験が必要です。** 他サービスの再起動、本番8081・`/home/daq`への変更は行っていません。

結果を[既存ドキュメント](/home/nagao/midas/midas/online/easiroc_frontend/docs/easiroc_slow_control.md)へ反映し、この最終報告と完全に同じ文章を[codex.log](/home/nagao/midas/midas/online/codex.log)へ上書き保存しました。

## HV実測値が直後にUnknownとなる経路の調査（2026-11-05、開発8181）

### 結論と修正の扱い

正常値→Unknown→次回測定で復帰する経路を、既存HV状態機械・実ODB adapter・GUIの
オフラインモックで再現した。ただし、ユーザーが観測した瞬間のODB／ブラウザ応答記録が
なく、実際の症状が以下のどの経路だったかは確定できない。今回の読み取り中はHV OFF／
Disabledで症状が発生しなかった。**制御・GUIの動作ソースは修正していない。**
鮮度期限を推測で延長したり、古い値を最新として扱ったりせず、原因を切り分ける
モックと観測記録を追加した。今回追加したテストは現行挙動の特性確認であり、
Unknownが解消されたことを示す回帰テストではない。

### 対象と読み取り証拠

- HTTP `127.0.0.1:8181` の `/Experiment/Name=daq-dev`、
  `/Custom/Path=/home/nagao/midas/midas/online/daq_monitor/web`、
  `/Custom/EASI Slow Control=easiroc.html` を読み取り確認した。
- キャッシュ回避GETで配信HTML／JS／CSS／mock-dataを取得し、4ファイルとも
  開発ファイルとのバイト一致を確認した（今回動作ソースは未変更）。
- `/Equipment/EASIROC/Settings/Network/IPAddress=192.168.10.40`、
  CurrentLimit=20 µA、HardwareAccessEnabled=true、RuninfoはSTOPPED・遷移0。
- 2026-11-05 00:50:09.887～00:50:44.968 JSTに既存HTTP経由でHVサブツリーを
  約0.5秒ごとに71回読み取り、すべてMeasurementValid=true、MonitoringStatus=OK、
  EnabledState=OFF、RampStatus=Disabledだった。LastUpdateUnixTimeは
  `0x6aeb542b`（00:50:03 JST）→`0x6aeb5449`（00:50:33 JST）と30秒で更新した。
  最終値は0.07086 V／0.0364 µA。通常監視中の鮮度切れはこの範囲で観測しなかった。
- 証拠はgitignore対象 `easiroc_frontend/build/hv-unknown-investigation/` の
  `identity.json`、`assets.json`、`odb-samples.jsonl`、`tests.log` に保存した。
  ODB読み取りは既存mhttpdのdb_get_valuesだけで、ローカルMIDAS clientは起動していない。

### 成功測定からUnknownへの経路

1. `easiroc_hv.cxx:28` のsampleはMADC電圧・電流の有限値／範囲、通信完了までの
   1秒予算を確認し、成功時だけStateのmeasured／valid／updatedを更新する。
   updatedは測定完了のmonotonic msであり、DAC書込みはこの測定時刻を更新しない。
   `easiroc_odb.cxx:1672` で測定時刻の変化があった場合だけLastUpdateUnixTimeを更新する。
   実backendのUnix時刻は `feeasiroc.cxx:1348` のtime(nullptr)由来で整数秒。
2. `easiroc_hv.cxx:71` のfreshは昇降圧中2,000 ms、通常65,000 msを上限とする。
   `easiroc_odb.cxx:1711` はfreshをMeasurementValidとして公開するため、
   **State.validがtrueでも、測定失敗なしでODBのMeasurementValidがfalseになる。**
   数値そのものを文字列Unknownに変更する処理はなく、GUIが無効と判定して文字列を表示する。
3. frontend_loop（`feeasiroc.cxx:1326`）のserviceは約1秒刻み。tickは通信完了時刻から
   1秒未満なら戻り、測定後待機中には追加測定しない。
   同じ正常な190 ms測定／10 ms DACを注入した昇圧・通常降圧モックでは次の順となった。

   | 相対時刻 | 動作・測定状態 |
   | --- | --- |
   | 4,190 ms | MADC成功、updated=4,190、測定後待機開始 |
   | 5,000 ms | 経過810 msのため待機継続、Valid |
   | 6,010 ms | 次段DAC完了、測定から1,820 msでValid |
   | 7,000 ms | DAC後990 msで測定せず、測定から2,810 msとなり公開Valid=false |
   | 8,190 ms | 次のMADC成功、Validへ復帰 |

   7,000 msではState.valid=true、faulted=false、MonitoringStatus=OKのまま。
   実ODB adapterモックでも、通信を一切呼ばないtickでMeasurementValidだけがfalseになり、
   LastUpdateUnixTimeは保持、Result=Pending、次回測定でtrue復帰を確認した。
   この周期と2秒鮮度の不整合は現行ソースで確実に再現するが、報告された瞬間との
   対応は未確認。測定後1秒待機を追加しても改善しないことと矛盾しない。
4. `easiroc.js:632` のhvMeasurementFreshはODB Validに加え、ブラウザ時計と整数秒の
   LastUpdateUnixTimeを比較する。Ramping／Settling／EmergencyDownは2秒、その他65秒。
   **ODB Valid=true／MonitoringStatus=OKでもGUIだけUnknownになる。**
   例：実測成功1,000.900秒、ODB時刻1,000秒、ブラウザ1,002.100秒では実年齢1.2秒だが、
   GUI年齢2.1秒としてUnknown。秒丸めだけで最大約1秒早く無効になる。
   時計ずれやHTTP応答遅延もGUIの年齢に加わるため、実ブラウザの時刻記録が必要。
5. MADC通信例外／飽和／不正値／1秒予算超過はfault（`easiroc_hv.cxx:62`）へ進み、
   valid=false、RampStatus=Unknown、MonitoringStatusへ原因を記録する。
   進行中コマンドはIndeterminateとなる。これは上記の正常測定の期限切れとは別経路。
   DAQ遷移／登録／ASIC Apply等のアクセス条件失効（同:140）、HardwareAccessEnabled失効、
   状態公開失敗も別の無効化経路である。保持された数値を表示可能とは扱わない。

### 通常監視・公開途中・READ・ポーリング

- 通常30秒監視と昇降圧監視は同じtickの排他的な分岐であり、独立した30秒タイマーが
  ramp測定を上書きする構造ではない。診断workerはHV通信前にjoinされる。
- 公開は一度MeasurementValid=falseにしてから最後にfreshを公開するが、全体が
  ApplyOdbLockのODB排他内（`easiroc_odb.cxx:1534`）。通常の外部読取りから
  この一時falseだけが見えると断定する根拠はない。GUIは複数キーを別々に読むので、
  service境界をまたいだ複数世代の組合せは別途観測対象となる。
- 自動refreshは応答後に約1秒待って次のODB読取りを行う。READもODBだけを読み、
  MADC測定やLastUpdateUnixTime更新を要求しない（`easiroc.js:320,500`）。
  READ中はstate.readingにより一時Unknown、完了後は同じ鮮度条件で復帰する。
  IP不一致、statusUnavailable、非有限値、未来時刻でもUnknownとなる。
- `/home/nagao/midas/midas/log/midas.log` と `dev-log/feeasiroc.log` を読んだ。
  midas.logの最終行は2026-11-04 20:10:45.027の通信復旧であり、13:28:14.573にも
  通信復旧、20:10:35.028にはTCP診断timeoutが記録されている。これらは通常の診断で、
  HVの測定成功・Valid切替を記録した行ではない。現HV経路は測定ごとのcm_msgを出さず、
  過去のMADC失敗をログの不在から否定できない。ODBは現在値なので過去履歴を復元できない。

### 未確定点と必要な観測

実際のUnknown切替と同じ時刻に、以下を保存すれば三経路を区別できる。
今回、実機操作を伴う再現は実施していない。新しいHV操作は本調査に含めない。

- 同一HVサブツリーのdb_get_values応答全体を、Unknown前後の時刻付きで保存する。
  MeasurementValid、MeasuredVoltage_V／MeasuredCurrent_uA、LastUpdateUnixTime、
  HeartbeatUnixTime、InProgress、RampStatus、MonitoringStatus、Error、Result、
  Active／ResultRequestIdを含め、別途Runinfo／アクセス条件も記録する。
- 同時にブラウザのDate.now、選択IP、state.reading、statusUnavailable、受信した
  hvValid／hvUpdated／hvRamp、hvMeasurementFreshの判定、HTTP送受信時刻・console errorを保存する。
- ODB Valid=false／MonitoringStatus=OK／Pendingならモックの鮮度切れと照合する。
  ODB Valid=trueならGUIの時計・鮮度・READ条件を照合する。
  MonitoringStatusがMADC timeout等でRampStatus=Unknownなら測定失敗経路を照合する。
  実症状がSteady／Disabled中なら、昇降圧2秒問題だけを原因としない。
- 必要になった場合の開発frontendログ追加候補は、I/Oを増やさず、測定完了・Valid遷移時の
  monotonic now／updated／last_tick、step_wait／step_sample_pending、無効理由を記録するもの。
  今回はログ追加・配備・再起動を行っていない。

### 検証と制限

`make -C easiroc_frontend all check check-web NODE=./build/tools/node-v22.16.0-linux-x64/bin/node`
成功。HV400 checks、実ODB adapter9908 checks、GUI156 checks、既存C++試験全件成功。
今回再コンパイルした試験の警告・エラーなし。通常ビルドallは既存成果物が最新だった。
C++は昇圧・降圧の期限切れ／次測定復帰、通常65秒・ramp2秒境界、過去時刻、無効測定を確認。
adapterは期限切れと注入MADC timeoutの別経路を確認。
GUIは整数秒による早期Unknown、全ramp状態、通常値保持／期限切れ、Valid=false、
READ中・READ完了・自動更新を検証した。既存通信失敗・飽和・Emergency・CurrentLimit試験も成功。

今回の変更は上記3つの既存テストと本ドキュメント、最終報告codex.logのみ。
Sleepは追加していない。HV制御、CurrentLimit、Emergency、鮮度規則は未変更。
実機通信・HV操作・ODB変更・frontend再起動・本番8081／`/home/daq`変更は行っていない。
症状解消・実機の昇降圧表示・稼働frontendへの修正反映は未検証であり、未完了である。

## HV Unknown表示の修正（2026-11-05、開発8181ソース）

前節の調査で再現した、正常な段階待機中のODB Invalidと、整数秒丸めによるGUIの
早期Unknownを修正した。制御用の鮮度と表示用の有効性を分離した。
前節の「動作ソース未修正」は調査時点の記録であり、本節が今回の修正結果である。

### frontend

- `easiroc_hv.cxx` の既存 `fresh()`、request／tickの制御分岐は変更していない。
  DAC進行・過電流保護に用いる2秒条件、通信1秒予算、昇圧4 V／降圧10 V、
  到達判定、Sleep、CurrentLimit、Emergency経路は維持する。
- 新しい `measurementFresh()` をODB MeasurementValidの公開だけに使用する。
  valid=false、Fault、測定時刻の逆転は直ちに無効。
  ramp中は「最後の段階処理から2秒以内」かつ「実際の測定から6秒以内」の両方を要求する。
  6秒は既存の上限を合計したもの：測定後待機≤2秒＋DAC通信≤1秒＋
  DAC後待機≤2秒＋次MADC通信≤1秒。待機の長さ・制御の進行条件を変更したものではない。
  段階が停止すれば2秒で無効になり、段階が進んでも測定年齢6秒の上限を延ばさない。
  正常な到達再測定は毎回の成功時刻で更新されるため、長いSettlingも新しい測定が必要。
- 通常状態の鮮度65秒・監視周期30秒を維持する。
  MADC／RBCP例外・不正値・飽和・予算超過は既存faultで直ちにvalid=falseとなり、
  6秒の表示上限を待たない。正常終了／Shutdown時の既存の無効化と残留電圧再測定も維持する。
- `Backend::unix_time_ms` を追加し、通常frontendではsystem_clockのUnix msを返す。
  `sample()` がMADC成功確認直後に `State::sampled_unix_ms` を記録する。
  ODB adapterはこれをQWORD `Variables/HV/LastUpdateUnixTimeMs` として公開し、
  従来の `LastUpdateUnixTime` は同じ成功時刻を秒に切り捨てた値とする。
  DAC書込み・待機・状態公開だけでは、どちらの測定時刻も更新しない。
  Backendのms時計を省略した既存のオフラインモックは従来の秒時計／service時刻を使用する。
  実稼働用backendは必ずms時計を供給する。

### GUI

- `hvMeasurementFresh()` はMeasurementValidを必須とし、新しいLastUpdateUnixTimeMsが
  存在するときは整数msで年齢を計算する。小数秒と切り捨て秒を混在させない。
  GUIの上限はramp中6,000 ms、通常65,000 msでfrontendの表示用上限と整合する。
  frontendが段階停止やFaultで先にInvalidを出した場合は、その応答でUnknownとなる。
- LastUpdate表示にも同じms時刻を使い、端数をISO時刻で表示する。
  未来・非有限・不正時刻、無効測定、HTTP失敗ではUnknown。無効時に保持された数値を
  表示し続けたり、ポーリング受信時刻を測定成功時刻として扱ったりしない。
  キャッシュのValidだけが残っても、年齢上限を超えればUnknownとなる。
- 新キーのない旧frontendでは、整数秒同士で比較する互換処理を設ける。
  この場合のramp上限は従来の2秒とし、旧frontendのMeasurementValidにも従う。
  秒時刻しかないため端数の精度は得られず、秒境界で最大1秒未満の不確実性が残る。
  再現済みのfrontend側経路の解消には、新frontendとGUIの組合せが必要。
- IP切替、READ中のUnknown、操作禁止条件、要求送信、通常ポーリングは変更していない。
  原因未確定の他の無効化経路や、既存の複数キー読取り方式は今回変更していない。

### 検証

既存テスト3本を拡張し、前節の特性確認を修正後の回帰試験へ変更した。

- HV状態機械：昇圧／降圧とも測定から2,810 msの正常待機中に表示用Validを維持し、
  制御用freshは従来どおりfalseであることを同時に確認。
  段階停止2秒／測定年齢6秒、逆転時刻、Fault即時Invalid、通常65秒境界も確認した。
  完了まで表示を維持し、完了後29,999 msでは追加測定なし、30,000 msで測定することを確認。
- 成功測定の時刻が秒境界直前、DAC完了が次秒でも、測定のms時刻を保持することを確認。
- 実ODB adapter：通信なしの正常待機でMeasurementValid=true、測定時刻不変。
  注入MADC timeoutでは直ちにInvalid、時刻不変、Indeterminateとなる。
  Fault中もEmergency shutdownが実行され、残留電圧の監視失敗は隠されないことを確認。
  初回追加テストではshutdown後の即時残留電圧監視を考慮せずOFF維持を期待して失敗したが、
  既存仕様に合わせてテストを修正した。制御実装のEmergency経路は変更していない。
- GUI：実年齢1.2秒の整数秒再現例、端数付きms時刻、全ramp状態で2秒超の値保持、
  6秒境界／超過、通常65秒境界／超過、Valid=false、HTTP失敗、不正／未来時刻、
  IP不一致、READ中／完了、自動更新を検証した。
- 既存のEmergency割込み、過電流の即時保護降圧、CurrentLimit、通信失敗・飽和、
  ASIC／DAQ／GUI操作制限の試験も全件成功。

`make -C easiroc_frontend all check check-web NODE=./build/tools/node-v22.16.0-linux-x64/bin/node`
成功：HV429 checks、実ODB adapter9941 checks、GUI164 checks、その他既存C++試験全件成功。
frontendと変更C++ソースをビルド済み。コンパイラ警告・エラーなし。
最終再実行時allは最新、全試験を再実行して成功した。
テスト出力はgitignore対象 `easiroc_frontend/build/hv-unknown-fix/tests.log` に保存。

### 対象・反映状態

修正前に開発8181のdaq-dev、/Custom/Pathが開発webディレクトリ、
配信HTML／JS／CSS／mock-dataと開発ファイルの一致を読み取り確認した。
GUIのJSはこの開発配信ディレクトリ内のソースを変更したため、再読込で読まれ得る。
ただし通常frontendは再起動しておらず、新しいMeasurementValid処理とmsキーの
稼働frontendへの反映は行っていない。実機による改善・昇降圧表示は未検証。
今回、実機通信・HV操作・ODB変更・frontend再起動を行っていない。
本番8081・`/home/daq`は変更していない。コミット・pushも行っていない。

## HV Unknown表示修正版の開発8181反映（2026-11-05）

開発実験daq-devの通常feeasirocだけを再起動し、前節の修正版を反映した。
対象IPは192.168.10.40。HV操作・GUIの実機操作試験は行っていない。

### 事前確認とバックアップ

再起動直前のHTTP読取りでRun186 STOPPED、Transition in progress=0、
Requested transition=0、HV EnabledState=OFF、InProgress=false、ActiveRequestId=0を確認。
MeasurementValid=true、0.07086 V／0.0364 µA、測定時刻01:05:03 JSTで、
65秒以内の新しい測定かつ既存の低電圧基準1 V以下だった。
CurrentLimit_uA=20、HardwareAccessEnabled=trueを確認し、そのまま維持した。

既存起動方法は `/Programs/feeasiroc/Start command` と起動スクリプトから確認した：
`MIDAS_EXPT_NAME=daq-dev scripts/start_feeasiroc.sh -D`。
実行環境はuid1001（nagao）。再起動操作を行うプロセスと旧・新feeasirocについて
PID namespace `pid:[4026531836]`、mount namespace `mnt:[4026531841]` の一致を確認した。
通常のサンドボックスからMIDAS clientを接続することはせず、ホストnamespace側で再起動した。

旧exe、ODB読取り結果、PID／start ticks／namespace、テスト結果を
`easiroc_frontend/build/backups/hv-unknown-deploy-20261105/` に保存した（gitignore対象）。
`feeasiroc.before` は `/proc/1044582/exe` から保存し、旧SHA256との一致を確認した。
保存後に安全条件・旧プロセス同一性・反映候補のSHA256を再確認してから停止した。

### 再起動・バイナリ記録

- 旧PID：1044582。exeは
  `/home/nagao/midas/midas/online/easiroc_frontend/bin/feeasiroc (deleted)`。
- 旧稼働SHA256：`d00188ea0fc3c4c86e2da169a91aa8b0324dfdd140ab503a4567d333403dd15a`。
- 01:05:30.972 JST、旧feeasirocをSIGINTで正常終了。
  旧プロセス終了を確認し、既存コマンドで新frontendを起動した。
- 新PID：1085730。01:05:31.019 JSTにdaq-devのclientとして登録された。
  exeは `/home/nagao/midas/midas/online/easiroc_frontend/bin/feeasiroc`。
- 新稼働SHA256：`d12faf142d9c5098354f163b270887a1f9c4a16b4aaf2f99f041a1c822ccccd7`。
  反映候補のビルド済みバイナリと一致し、終了後の再確認でも同じSHA256だった。
- FrontendBuild：`Nov  5 2026 00:56:15`。

### 反映後の読み取り確認

既存のmhttpd db_get_valuesだけで状態を読んだ。外部のRBCP／TCP診断コマンドは実行せず、
通常frontendが既存周期で実施する診断・測定結果を観測した。

- startup時のTCP診断は01:05:41.028 JSTにtimeoutを記録したが、
  01:05:51.027 JSTに既存のRBCP firmware read／TCP port24診断が正常復帰した。
  最終ODBはRBCPCommunicationOK=true、TCPReachable=true、LastError空、
  AcquisitionRunning=false、AcquisitionFault=false。
- 01:05:49～01:06:24 JSTの36回読取りでHV heartbeatは毎回更新した。
- 初回の通常30秒測定待ち中はMeasurementValid=false／時刻0であり、古い測定を復元していない。
  01:06:01.191 JSTに新しい有効測定0.07086 V／0.0364 µAを確認。
  `LastUpdateUnixTimeMs=0x000001a1a74f66e7`、
  `LastUpdateUnixTime=0x6aeb57e9`、MeasurementValid=true、MonitoringStatus=OK。
  ms値を1000で割って切り捨てた値が秒値と一致することを確認した。
- HV EnabledState／SetVoltageState／RampStatusは再起動により設計どおりUnknown、
  InProgress=false、Result=Idle、Error空。測定値が低いことを理由にOFFを復元していない。
- EASIROC Settings全体（CurrentLimit20 µA／HardwareAccessEnabled=trueを含む）、
  Commands、ASIC LastApplied、Runinfoは前後で不変（ODB更新メタデータを除いて比較）。
  操作用のODB書込みは行っていない。frontend自身の通常の状態更新と新しいms変数公開は発生する。
- feeasiroc以外のclient登録はPIDを含め不変：mhttpd602942、Logger602946、
  ana602951、fevme626813、daq_monitor628754。他frontend・Logger・mhttpdは再起動していない。

### GUI・ソース・テスト確認

8181のExperiment/Name=daq-dev、/Custom/Pathは開発webディレクトリ、
EASI Slow Controlの対象はeasiroc.htmlであることを確認。
反映後にキャッシュ回避GETでHTML／JS／CSS／mock-dataの4ファイルを取得し、
すべて開発ファイルとのバイト一致を確認した。
配信JSのSHA256は `9c834d1d16c68acf4fafc8e64e67a838362cda9e692eafed8bd019d3122c95cc`。
ブラウザからのHVボタン操作はユーザーによる試験対象として残している。

`make -C easiroc_frontend all check check-web NODE=./build/tools/node-v22.16.0-linux-x64/bin/node`
成功（HV429 checks、実ODB adapter9941 checks、GUI164 checks、既存C++全件成功）。
allはビルド成果物が最新、今回のテスト実行に警告・エラーなし。
制御用freshの2秒条件は不変、表示用measurementFreshは段階処理2秒／測定年齢6秒、
GUIはLastUpdateUnixTimeMsで整数ms比較することをソースとモックで再確認した。
正常待機中の2秒超表示、期限切れ・通信失敗、30秒監視復帰、Emergency・過電流保護の
既存条件をモックで確認した。昇降圧中の実GUI表示や実機保護動作を試験したとは扱わない。

バックアップフォルダにbefore.json、after-final.json、identity-before／after.json、
post-observations.jsonl、verification.json、assets.json、tests.logを保存した。
HV操作・操作用ODB変更・追加の実機診断通信は行っていない。
本番8081・`/home/daq`は変更していない。コミット・pushは行っていない。
