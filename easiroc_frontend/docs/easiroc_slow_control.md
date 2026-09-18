# NIM-EASIROC Slow Control investigation and architecture

This document is based only on the legacy implementation in
`reference/controller_20170425/`. Generic EASIROC and SiTCP behavior is not
used to fill gaps. No hardware access or register write was performed while
preparing it.

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
- `SlowControlEncoder` validates field widths/counts and produces exactly one
  57-byte image using the legacy field order, per-field bit order, active-low
  transformation, per-byte bit reversal, and final byte reversal.
- `SlowControlConfig` groups two ASIC configurations, probe/read-register
  selections, pedestal thresholds, selectable logic, trigger width, time
  window, and optional trigger mode/delays.
- `SlowControlPolicy::buildApplyPlan` produces an ordered vector of write and
  delay descriptions. A write contains a register address and bytes; a delay
  contains milliseconds. Neither type has an execute method.

The aggregate plan follows the legacy `slowcontrol` order: ASIC image/latch,
probe image/latch, read-register reset/selection, pedestal suppression,
selectable logic, trigger width, and time window. Trigger mode/delays are added
only when explicitly present because the legacy normal startup leaves that
call disabled.

`easiroc_slow_control_test` compares both ASIC images for the supplied YAML
settings against a fixed 57-byte golden vector derived independently from the
legacy `ConfigLoader` algorithm. It also checks raw threshold/Input DAC limits,
MSB-to-LSB and active-low effects, direct-control start/load transitions, both
100 ms delay positions, probe/read-register selection, big-endian pedestal and
time-window data, selectable-logic layout, trigger width, and default trigger
delays. No hardware or network operation is involved.
