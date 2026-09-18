# NIM-EASIROC communication protocol

This note records only details confirmed from the legacy controller under
`reference/controller_20170425/`. It does not fill gaps from generic SiTCP or
RBCP documentation.

## Primary sources

- `RBCP.rb`: RBCP header, UDP transactions, reply validation, IDs and timeout.
- `VME-EASIROC.rb`: transports, register addresses, version read, DAQ control,
  TCP receive logic, frame decoding and event framing.
- `Controller.rb`: TCP/UDP port selection and controller startup.
- `hist.cc`: independent decoding of saved event words.
- `README`: controller/firmware vintage and invocation.

## Transport and ports

The controller constructs `VmeEasiroc.new(ipaddr, 24, 4660)`
(`Controller.rb:644-652`):

- **TCP port 24** carries event data. `readEvent` opens a `TCPSocket` to it
  (`VME-EASIROC.rb:294-308`).
- **UDP port 4660** carries RBCP register transactions. It is passed to
  `RBCP.new` (`VME-EASIROC.rb:14-17`) and used as the datagram destination
  (`RBCP.rb:119-136`).

The legacy default IP is `192.168.10.16` (`Controller.rb:644` and
`README:11-14`). The current default `192.168.10.26` is specified by the local
`AGENTS.md`, not the controller source.

## RBCP request packet

RBCP is used over UDP. A request is an 8-byte header followed by data for a
write, or no payload for a read (`RBCP.rb:3-29,119-129`).

| Offset | Size | Field | Encoding |
|---:|---:|---|---|
| 0 | 1 | version/type | `0xff` |
| 1 | 1 | command/flags | read `0xc0`; write `0x80` |
| 2 | 1 | packet ID | low 8 bits |
| 3 | 1 | data length | 1--255 bytes in normal controller use |
| 4 | 4 | address | unsigned 32-bit, network byte order (big-endian) |
| 8 | length | write data | absent from a read request |

The constants and serialization, including address `pack('N')`, are in
`RBCP.rb:3-29`. Longer transfers are split into consecutive transactions
(`RBCP.rb:41-49,65-83`). Typed helpers use network byte order for 16-bit
(`n*`) and 32-bit (`N*`) data (`RBCP.rb:53-63,86-104`). Raw 8-bit data has no
additional swapping.

## RBCP reply, ID and timeout

The implementation expects an 8-byte header followed by exactly the requested
number of bytes for both reads and writes (`RBCP.rb:136-164`). It requires:

- version/type `0xff`;
- command equal to request command OR `0x08`: read reply `0xc8`, write reply
  `0x88`;
- matching packet ID, data length and address;
- total datagram size `8 + data length`.

If the command mismatches and bit 0 is set, it reports a bus error; otherwise
an invalid command (`RBCP.rb:154-159`). The read result is the bytes following
the header (`RBCP.rb:142-143`). The legacy code does not compare a write reply
payload to the transmitted data.

The ID starts at zero, is checked in the reply, and increments modulo 256 when
each attempt finishes, including a failed attempt (`RBCP.rb:34-39,124,138-141,
161`). A retry therefore has a new ID.

Each attempt creates a UDP socket on an ephemeral local port, waits **1 second**
with `IO::select`, receives at most 263 bytes and closes the socket
(`RBCP.rb:119-140`). Errors are retried up to three total attempts
(`RBCP.rb:106-116`).

## Safe firmware-version read

The controller's `version` method performs this read-only RBCP operation:

- address **`0xF0000000`** (`VME-EASIROC.rb:484-485`);
- length **6 bytes** (`VME-EASIROC.rb:274-276`).

Bytes 0--1 contain major, minor, hotfix and patch as four nibbles. Bytes 2--3,
4 and 5 are decoded nibble-by-nibble as decimal year, month and day
(`VME-EASIROC.rb:278-291`). `Controller.rb:620-624` exposes this as `version`.
This is the only clearly read-only firmware/status operation supplied that
does not first write registers. Monitor-ADC reads write controls before reading
(`VME-EASIROC.rb:352-370`). The register named `statusRegisterAddress` at
`0x00000077` is written, not read (`VME-EASIROC.rb:444-445,542-557`).

### Verified on hardware

The standalone read-only diagnostic was run against `192.168.10.26` and the
RBCP firmware read succeeded. The device returned:

```text
NIM-EASIROC at 192.168.10.26
Firmware version: v.4.0.1-p0
Synthesized on: 2015-2-20
Raw bytes: 40 10 20 15 02 20
```

This verifies UDP/RBCP communication, the read at `0xF0000000`, reply parsing,
and the version/date interpretation on the current hardware. It does not verify
register writes, TCP event transfer, or DAQ readout.

The connection-only diagnostic was subsequently run against the same device.
The TCP connection to data port 24 succeeded:

```text
RBCP firmware read: OK
TCP data connection: OK (port 24)
```

The TCP socket was closed immediately without sending a command or waiting for
event data. This confirms only that a TCP connection can be established; it
does not verify event delivery, event framing, DAQ start, or DAQ readout.

## TCP event reception and event format

`readEvent` connects to TCP port 24, discards buffered bytes, enters DAQ mode
by an RBCP write, and reads the requested events (`VME-EASIROC.rb:294-308,
560-577`). The standalone diagnostic does not exercise this path because DAQ
start is prohibited.

`receiveNbyte` loops on `recv` until exactly the requested count is accumulated
(`VME-EASIROC.rb:589-598`). Normal header/data reads have no explicit timeout
and do not handle an empty `recv`. Only buffered-data discard uses a 0.1-second
`IO::select` timeout (`VME-EASIROC.rb:615-623`).

The read unit is therefore one 4-byte wire word for the header, followed by one
request for exactly `4 * dataSize` bytes. Because TCP can return partial data,
each request is internally satisfied by as many `recv` calls as necessary
(`VME-EASIROC.rb:589-610`).

Wire words are four bytes in network order (`unpack('N')` / `unpack('N*')`,
`VME-EASIROC.rb:600-610`). `decodeWord` requires
`(wire_word & 0x80808080) == 0x80000000`, strips those per-byte framing bits,
and compacts four 7-bit groups into a 28-bit logical word
(`VME-EASIROC.rb:579-586`).

An event is one decoded header word with bit 27 set, followed by
`header & 0x0fff` decoded data words with bit 27 clear
(`VME-EASIROC.rb:600-612`). Decoded words are saved as big-endian 32-bit values
(`Controller.rb:357-375`). `hist.cc:91-108` independently checks header bit 27
and uses its low 12 bits as the following-word count.

### Decoded data words

`hist.cc:19-42,107-134` establishes these fields:

| Type | Recognition | Channel | Value / flag |
|---|---|---|---|
| high-gain ADC | `(word & 0x00680000) == 0x00000000` | bits 18:13 | bit 12 over-threshold; bits 11:0 value |
| low-gain ADC | `(word & 0x00680000) == 0x00080000` | bits 18:13 | bit 12 over-threshold; bits 11:0 value |
| leading TDC | `(word & 0x00601000) == 0x00201000` | bits 18:13 | bits 11:0 value |
| trailing TDC | `(word & 0x00601000) == 0x00200000` | bits 18:13 | bits 11:0 value |
| scaler | `(word & 0x00600000) == 0x00400000` | bits 20:14 | bits 13:0 value |

ADC high/low gain differs in logical bit 19, as also expressed by the Ruby
predicates in `VME-EASIROC.rb:625-631`. For ADC only, logical bit 12 is named
`otr` by `hist.cc` and is treated as an over-threshold/overflow indication;
the active histogram path accepts the ADC value only when it is clear
(`hist.cc:109-122`). TDC leading/trailing differs in logical bit 12, so that bit
is part of the TDC type rather than an overflow flag (`hist.cc:29-37,123-130`).

The active scaler decoder uses all 14 low bits as its value. An inactive
`#else` block in `hist.cc:142-168` contains older logic that calls bit 13 an
overflow flag and uses bits 12:0 as a count, but it then forcibly clears that
flag. The offline decoder follows the active code (`hist.cc:131-140`) and does
not claim a scaler overflow flag.

No meaning for other header bits is established by the supplied source.

### Malformed-frame checks

The legacy controller detects these malformed conditions:

- per-byte framing pattern other than raw bits 31/23/15/7 = `1/0/0/0`
  (`VME-EASIROC.rb:579-586`);
- logical bit 27 clear in the expected header (`VME-EASIROC.rb:600-604`);
- logical bit 27 set in any expected data word (`VME-EASIROC.rb:609-612`).

The standalone decoder performs the same checks and also rejects a byte count
that is not a whole non-empty sequence of 4-byte words, or whose number of data
words differs from header bits 11:0. The legacy live receiver obtains exactly
the count from the header rather than accepting a caller-supplied event buffer,
so that last consistency check is specific to the offline buffer interface.

## DAQ start/stop register sequence (documented, not executed)

The DAQ/status register address is **`0x00000077`**
(`VME-EASIROC.rb:444-445`). `writeStatusRegister` composes one byte as follows
and writes it through RBCP (`VME-EASIROC.rb:542-557`):

| Bit | Mask | Meaning |
|---:|---:|---|
| 0 | `0x01` | DAQ mode |
| 1 | `0x02` | send ADC |
| 2 | `0x04` | send TDC |
| 3 | `0x08` | send scaler |

The three enable masks are defined at `VME-EASIROC.rb:488-501`. The normal
post-construction settings are ADC ON, TDC ON, scaler OFF
(`VME-EASIROC.rb:21-23`), giving **DAQ ON `0x07`** and **DAQ OFF `0x06`**.
These values depend on the enable selections: in general ON is
`0x01 | enable_bits`, and OFF is `enable_bits`. The `adc`, `tdc`, and `scaler`
commands only update these selections (`Controller.rb:281-315`); the composed
value is written when entering or leaving DAQ mode.

There is one initialization nuance: the constructor calls `exitDaqMode` before
assigning the default enable booleans (`VME-EASIROC.rb:14-23`), so that initial
write is `0x00`. This is distinct from the normal stop after a read (`0x06`).

The acquisition order in `readEvent` is (`VME-EASIROC.rb:294-308`):

1. open the TCP connection to port 24;
2. discard already-buffered TCP bytes until 0.1 seconds pass without data;
3. set DAQ mode and write the ON value to `0x00000077`;
4. receive the requested events;
5. in an `ensure` block, clear DAQ mode and write the OFF value;
6. discard trailing buffered bytes and close TCP.

`readAndThrowPreviousData` repeatedly uses `IO::select` with a 0.1-second
timeout and consumes up to 256 bytes per `recv`; draining ends after one
0.1-second interval without readable data (`VME-EASIROC.rb:615-623`). Before
DAQ start this discards bytes already present on the newly opened connection;
after DAQ stop it discards trailing buffered bytes. The implementation does not
interpret drained bytes.

`enterDaqMode` wraps only the event-reading block in `begin`/`ensure`, so an
exception during event reception or the caller's event callback triggers
`exitDaqMode` and an OFF write (`VME-EASIROC.rb:560-571`). If the initial ON
write itself raises, execution has not yet entered that protected block and no
OFF write is attempted there. The outer `readEvent` ensure still closes the TCP
socket (`VME-EASIROC.rb:294-308`). If the OFF write raises, the post-stop drain
is not reached, but the outer socket close still runs.

The legacy class has no explicit destructor/finalizer that stops DAQ. Its
constructor does have the implicit OFF write described above. `reloadSetting`
in that constructor loads configuration into memory; the other hardware writes
are issued later by the top-level controller startup calls
(`Controller.rb:652-660`): Slow Control, probe/read-register selection,
pedestal suppression, selectable logic, trigger width, time window, and user
clock output. These startup calls include operations outside DAQ status control
and must not be mistaken for a side-effect-free object construction sequence.

### Local DAQ-control policy

The local `DaqControl` class preserves the legacy address and bit composition
but deliberately does not reproduce its implicit I/O behavior. It stores only
three enable booleans and provides:

- `statusByte(false)`: enable bits only;
- `statusByte(true)`: the same enable bits OR `0x01`;
- `startValue()`: plain `{0x00000077, statusByte(true)}` data;
- `stopValue()`: plain `{0x00000077, statusByte(false)}` data.

It has no network address, socket, RBCP object, transport pointer, or callback.
Construction, enable changes, start-value generation, stop-value generation,
and destruction perform no I/O. A future transport layer must be invoked
separately and explicitly to execute a returned register value. This separation
also means merely creating a frontend-side DAQ policy can never start, stop, or
otherwise write to the hardware.

`easiroc_daq_control_test` verifies the address and ON/OFF values for the
legacy default and other enable combinations, bit-0 behavior, enable updates,
and the policy's transport-free construction/destruction contract. It performs
no network or hardware access.

## Offline decoder verification

`easiroc_event_test` uses deterministic words constructed solely from the
confirmed masks above. It covers a valid header, high- and low-gain ADC (and
ADC overflow), leading and trailing TDC, scaler, explicit raw-to-logical word
conversion, an invalid header, a header/data-size mismatch, and invalid
per-byte framing. It performs no network or hardware access.

## Incremental TCP byte-stream reconstruction

The legacy `receiveNbyte` correctly acknowledges that one `recv` need not
return the requested byte count: it appends repeated results until it has 4
header bytes or `4 * dataSize` data bytes (`VME-EASIROC.rb:589-610`). The local
`EventStreamParser` generalizes this for arbitrary TCP chunks rather than
assuming the caller requests one header/event at a time.

The parser is socket-independent. `push` accepts any byte count and appends it
to an internal byte buffer. Parsing then proceeds as follows:

1. Fewer than 4 buffered bytes are retained as an incomplete wire word.
2. Once 4 bytes are present, the first word is decoded and must have logical
   header bit 27 set.
3. Header bits 11:0 determine the complete event size:
   `4 * (1 + dataSize)` bytes.
4. If fewer bytes are buffered, the complete partial event is retained for the
   next `push`.
5. Once enough bytes exist, exactly that event is passed to `decodeEvent`,
   removed from the front of the buffer, and returned to the caller.
6. Parsing repeats, so one chunk can return any number of complete events while
   retaining a trailing partial event.

This makes word and event boundaries independent of `recv` boundaries. It
handles a word split at any byte, an event split over any number of chunks, and
multiple adjacent events in one chunk without scanning payload words for a new
header. Event boundaries come only from a validated header's data-size field.

Malformed per-byte framing, a non-header at an event boundary, or a header word
inside the declared data region is reported by exception. After such an error
the parser enters a failed state and must be discarded; it does not guess a
resynchronization position. A short chunk is not an error and remains buffered.
At known end-of-stream, `finish` rejects any remaining partial word or event,
which is how a truncated event/header-size mismatch is distinguished from a
temporarily incomplete TCP fragment.

`easiroc_stream_test` verifies whole-event input, splits inside words and the
header, splits inside event data, one-byte chunks, two and three events in one
chunk, every possible split position across two adjacent events, malformed
framing, a non-header at an event boundary, truncation relative to the header
size, retention/completion across calls, and ordered delivery of consecutive
events. All inputs are locally constructed; no socket or hardware is used.

## Standalone diagnostic scope

`easiroc_test` performs two independent diagnostics: the six-byte
firmware-version RBCP read at `0xF0000000`, and a finite-time TCP connection
attempt to data port 24. The TCP probe uses a non-blocking connect with a
one-second timeout, immediately closes a successful connection, and neither
sends nor receives data. The diagnostic has no write method and does not reset
the hardware, operate HV, alter slow control, or start DAQ.

`easiroc_read_one` is the explicit write-capable one-event diagnostic. It uses
ADC ON, TDC ON and scaler OFF, connects to TCP port 24, performs a bounded
pre-acquisition drain, writes `0x07` to `0x00000077`, and waits at most 30
seconds for an event. It passes received chunks through `EventStreamParser`
and the common event decoder, then attempts to write `0x06` to the same
register on every path after a DAQ-ON write was attempted. This includes an
ON-write exception, where the hardware state is reported as unknown. The TCP
connection is closed after the OFF attempt. No other hardware register is
written by this program.

`easiroc_read_many` uses the same transport, DAQ values, bounded drain, parser,
decoder, and stop-on-all-exit-paths policy for a continuous multi-event run.
DAQ is enabled once for the whole run. The default is 100 events; the event
count and then the IP address can be supplied as positional arguments. Each
event must contain exactly one HG and one LG ADC word for every channel, no
scaler or unknown words, while leading and trailing TDC counts may vary. A
30-second deadline starts at DAQ ON and restarts after each TCP chunk that
completes at least one event. A timeout, disconnect, malformed stream word, or
failed event-content check terminates reception immediately and proceeds to
the DAQ-OFF attempt. The program retains only per-channel running sums and
sums of squares for its final mean and population-RMS report.

## MIDAS frontend event readout

`feeasiroc` owns all direct NIM-EASIROC access in one process. At BOR it waits
for any stopped-state read-only diagnostic to finish, reads ODB settings,
requires ADC ON / TDC ON / scaler OFF, opens TCP port 24, performs the bounded
pre-acquisition drain, creates a fresh `EventStreamParser`, and explicitly
writes DAQ ON (`0x07`). A failure at any step rejects the run transition. If
the ON write was attempted, cleanup also attempts DAQ OFF and reports the
hardware state as unknown if that write fails.

During RUNNING, the polled Physics Equipment first serves any decoded events
in its FIFO. When the FIFO is empty it performs a non-blocking readability
check, a receive bounded to 100 ms, then passes the bytes to the common stream
parser and decoder. Every complete event returned from one TCP chunk is
organized and appended to the FIFO. The MIDAS readout callback removes exactly
one FIFO entry, so one MIDAS event always represents one NIM-EASIROC event even
when one TCP receive completes multiple events. A receive, framing, decode, or
content error immediately attempts DAQ OFF, closes TCP, clears pending events,
and requests an asynchronous MIDAS run stop.

The decoded readout representation contains channel-ordered 64-element HG and
LG arrays. TDC entries contain channel, 12-bit value, and a one-based hit index
counted independently for each channel and independently for leading and
trailing edges; counters are local to one event. No leading/trailing pulse
matching is performed.

The current MIDAS banks use `TID_WORD`:

| Bank | Payload |
|---|---|
| `EAHG` | 64 HG ADC values in channel 0--63 order |
| `EALG` | 64 LG ADC values, prepared but compile-time disabled |
| `ETLE` | variable-length repeating `channel, hit, value` leading triplets |
| `ETTR` | variable-length repeating `channel, hit, value` trailing triplets |

Zero-hit TDC events contain valid zero-length `ETLE` and/or `ETTR` banks. At
EOR the frontend writes DAQ OFF (`0x06`), performs a bounded post-acquisition
drain after a confirmed stop, and closes TCP. Frontend exit and acquisition
error paths also attempt DAQ OFF whenever a DAQ-ON write had been attempted.
The periodic read-only Status Equipment remains present, but it does not start
new RBCP/TCP probes while acquisition owns the hardware connections.
