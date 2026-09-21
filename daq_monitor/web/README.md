# DAQ general-user Custom page

`daq.html` is the general-user MIDAS dashboard. It reads status from
`/DAQ/Status` and the standard MIDAS equipment statistics records. Start and
Stop use the existing MIDAS transitions. The only direct ODB writes are module
Enable settings while the run state is STOPPED; the page never writes hardware.

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
`easiroc.css`, `easiroc.js`, and `easiroc-mock-data.js` into that existing
directory, then set only `/Custom/DAQ` to `daq.html`.

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

Enable controls are locked for RUNNING and PAUSED. While STOPPED, a change is
written to the existing setting and then read back before the displayed state
is accepted. The module-name links use mhttpd's installed
`?cmd=odb&odb_path=...` URL form and point to the corresponding Settings tree.

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

The Alarm ON/OFF control affects only the browser beep and is stored in
`localStorage`. It does not change any MIDAS alarm. A short Web Audio beep is
requested only on a new transition into global ERROR.

## EASIROC slow-control page

`easiroc.html` is linked from the dashboard and provides a STOPPED-only
staging view for discriminator thresholds and both 32-channel Input DAC
arrays. Editing does not write ODB or hardware. `Save to ODB` writes the six
Settings values and requires an ODB readback of every value before accepting
the save. `Apply to hardware` is disabled until the staged values equal ODB,
the run is STOPPED, EASIROC is enabled, and the frontend mailbox is idle.

Apply writes one new monotonic `ApplyRequestId` to the existing command
mailbox and waits for the frontend terminal acknowledgement; it never writes
hardware directly. Since the frontend compares request IDs using ordinary
ordered DWORD values, the page refuses to wrap at `0xffffffff` rather than
replaying an old token. LastApplied is explicitly labelled as the last
successfully transmitted configuration, not ASIC readback. `ApplyAtBOR` is
read-only in an Advanced/Deprecated panel and is never writable from this UI.

Open `easiroc.html?mock=1` for offline preview. Its scenario buttons cover
Match, Mismatch, Unknown, Indeterminate, Applying, Succeeded, Failed,
Rejected, and Unsaved changes without contacting MIDAS.

## Data sources

The page uses these monitor-owned records:

- `/DAQ/Status/Global/{Severity,Summary,CanStart,CanStartReason}`
- `/DAQ/Status/Run/{RunNumber,State,DurationSec}`
- `/DAQ/Status/Frontends/VME/{Severity,Reason,Connected,EventSlipCount}`
- `/DAQ/Status/Frontends/EASIROC/{Severity,Reason,Connected}`
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

Start and Stop call the mhttpd JSON-RPC method `cm_transition` with
`TR_START` or `TR_STOP`. The Start button directly follows the published
`CanStart` value; the browser does not recalculate policy or severity.
