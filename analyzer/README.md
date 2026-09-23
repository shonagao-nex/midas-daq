# MIDAS common analyzer

This directory contains the initial offline/online common analysis layer for
the VME and NIM-EASIROC frontends. `EventInspector` classifies physics events
by bank composition (not event ID), reports the raw MIDAS structure, and feeds
`EventBuilder`. `EventBuilder` joins equal frontend serial numbers without
dropping one-sided events. Hardware decoders write only to `DecodedEvent`.
`RootTreeWriter` stores each built `DecodedEvent` as one entry in the `Events`
TTree. `HistogramManager` fills configuration-driven `TH1D` objects from the
same `DecodedEvent` through `ExpressionResolver`. No live ODB access or
hardware access is performed when an input file is specified. Histogram
configuration can be loaded from ODB; ROOT ODB snapshot writing is not
implemented.

## Data flow

```text
TMEvent
  -> EventInspector (classification and raw-bank statistics)
  -> EventBuilder (join by frontend serial)
  -> module decoders
  -> DecodedEvent
     -> HistogramManager -> ExpressionResolver
     -> RootTreeWriter -> ROOT file / Events TTree  (offline only)
```

VME physics events are recognized by `ADC0`, `TDC0`, `TDC1`, or `FADC`.
EASIROC physics events are recognized by `EAHG`. The current frontend mapping
is `ADC0` = V792, `TDC0` = V1190, `TDC1` = V775, and `FADC` = V1720E.

The V792, V775, V1190, V1720E, and EASIROC bank formats implemented here are
based on the existing frontend and installed ROOTANA decoder sources. The
NIM-EASIROC raw hardware event counter is not present in the current EAHG/ETLE/
ETTR payload, so `nim_easiroc` remains `-999`.

## Build

```sh
cd /home/daq/midas/midas/online/analyzer
export MIDASSYS=/home/daq/midas/midas_src
export ROOTSYS=/home/daq/root/current
cmake -S . -B build -DROOTANA_DIR=/home/daq/midas/rootana
cmake --build build -j
```

The build is out-of-source and uses the installed MIDAS/manalyzer targets and
`/home/daq/midas/rootana/lib/librootana.a`; it does not write into either
dependency source tree.

## Command line and operating modes

`-f` selects offline mode. With no `-f` or positional input file, the analyzer
uses manalyzer's live MIDAS mode. The same event builder, decoders, decoded
event type, expression resolver, and histogram manager are used in both modes.

```text
Usage:
  midas_analyzer [online options]
  midas_analyzer -f INPUT [offline options]
  midas_analyzer --init-hist-odb [connection options]

  -f FILE    offline MIDAS input
  -w FILE    offline ROOT output
  -n N       maximum emitted DecodedEvent count (0 means unlimited)
  -h         help

  --init-hist-odb
      create default histogram configuration in MIDAS ODB and exit;
      existing configuration is never overwritten
```

The MIDAS `mhttpd` default port is 8081. Online monitoring starts the standard
ROOT `THttpServer` on `0.0.0.0:8082` by default, avoiding that port:

```sh
./build/midas_analyzer --no-profiler
```

No online ROOT file is opened. `RootTreeWriter` is not constructed, and live
histograms are placed in manalyzer's in-memory `TARootHelper::fgDir`. The
process remains in the foreground and manalyzer keeps it connected while the
run is stopped; BOR creates a run object and EOR destroys it without exiting
the process. Use `-RPORT` to select another ROOT web port; an explicit value,
for example `-R9090`, takes precedence over the 8082 wrapper default and
also binds to all interfaces. The analyzer constructs THttpServer after
manalyzer has created its ROOT directory, since the installed manalyzer's own
`-R` implementation forces a `127.0.0.1` bind. Offline mode does not start
THttpServer, and `-R` is rejected with offline input. No mhttpd proxy or alias
change is required for direct validation of the analyzer server.

When the online analyzer is connected, the ROOT server also serves the
histogram channel editor at
`http://133.11.162.51:8082/Analyzer/HistogramEnable/` (substitute an explicitly
selected `-R` port or a different DAQ host address). The ROOT Web homepage
has a separate **Histogram Enable** link to this URL; the JSROOT hierarchy
item is not a navigation link. It lists QDC0, TDC0, TLE0, TTR0, EADC0, ETLE0, ETTR0,
and FADC0 by group. Checking or unchecking a channel sends one explicit
WebSocket request that writes only the corresponding element of the existing
`/Analyzer/Histograms/<Group>/Enabled[]` array. The handler verifies the
group, channel count, and read-back value. Opening the page only reads ODB.
If the compact tree is absent or malformed, the page reports an error and
does not create keys. Event is intentionally excluded from this editor.
The normal online poll applies a successful change to histogram booking.
The page does not edit titles, page layouts, or other histogram fields.
External access also requires the host firewall to permit the selected port.

For offline analysis, load ROOT and use:

```sh
source /home/daq/root/current/bin/thisroot.sh
cd /home/daq/midas/midas/online/analyzer
./build/midas_analyzer -f /home/daq/midas/midas/data/run00062.mid.lz4
```

The default output is `/home/daq/midas/midas/rootfiles/run00062.root`. A `-w`
basename is placed below that directory; an absolute `-w` path is used as-is.
The wrapper translates this to manalyzer's `-O` option. Legacy positional
input and `-O`/`-D` remain available, but must not be combined with `-f`/`-w`.
Manalyzer's `-e` counts raw records; use `-n` for the required decoded-event
limit. `--mt` is rejected because live ROOT publication/rebooking is designed
for manalyzer's single-thread event loop.

The first few records are printed in detail. The end-of-run summary contains
event counts, serial ranges and gaps, per-bank type/length distributions, and
builder pairing statistics. Missing counterparts are still delivered as
one-sided `DecodedEvent` objects and are summarized by count and counter range;
they do not produce one warning per event. A warning is reserved for paired
events whose two frontend counters disagree.

## Histogram configuration

At begin-of-run, `HistogramConfigLoader` reads channel arrays in each group
below `/Analyzer/Histograms`. It uses the `MVOdb` supplied by
`TARunInfo::fOdb`; `HistogramManager` and `ExpressionResolver` do not know an
ODB path or use an ODB API. Channel index 0 is the first element of each array:

```text
/Analyzer/Histograms/QDC0/
    HistName    = ["h_qdc0_ch00", "h_qdc0_ch01", ...]
    Title       = ["QDC0 Ch.0", "QDC0 Ch.1", ...]
    XTitle      = ["QDC raw value", ...]
    YTitle      = ["Counts", ...]
    Type        = ["TH1D", ...]
    Expression  = ["qdc0[0]", "qdc0[1]", ...]
    Bins        = [4096, ...]
    Min         = [0, ...]
    Max         = [4096, ...]
    Cut         = ["", ...]
    Enabled     = [false, ...]
```

The fields mean:

- `HistName`: ROOT object name at each channel index.
- `Type`: histogram class; currently only `TH1D` is supported.
- `Expression`: one supported `DecodedEvent` value to fill.
- `Bins`, `Min`, `Max`: axis bin count and limits (`Bins > 0`, `Min < Max`).
- `Cut`: selection expression; currently only the empty string is supported.
- `Enabled`: a false value keeps the definition but does not book or fill it.
- `Title`, `XTitle`, and `YTitle` are copied to the ROOT histogram and axes.

The default schema has 521 slots in these groups: `Event` (1), `QDC0` (32),
`TDC0` (32), `TLE0` (128), `TTR0` (128), `EADC0` (64), `ETLE0` (64),
`ETTR0` (64), and `FADC0` (8). Every slot is created by the shared
`DefaultHistogramConfigs()` generator. `Event[0]` is enabled by default;
all channel slots are disabled until explicitly enabled in ODB. TLE/TTR and
ETLE/ETTR defaults use the first hit (`[0]`), and FADC uses the first sample.
The loader expands 99 ODB array fields (11 per group) into the 521 internal
`HistogramConfig` objects, assigning `Ch00`, `Ch01`, and so on from array
indices. All arrays in a group must have the same nonzero length; an
incomplete group causes a non-writing in-memory fallback. The previous
per-channel-subkey layout is not
read, migrated, or overwritten automatically.

Each definition is validated before `HistogramManager` sees it. An unsupported
type or cut, an empty name, invalid binning, or an invalid expression produces
a warning and skips only that histogram. If several entries contain the same
`HistName`, the first definition in ODB directory enumeration order wins and
later definitions are warned and skipped. This avoids a silent overwrite and
makes the selected definition deterministic for a given ODB layout.

Online analyzer startup does not create `/Analyzer/Histograms` automatically.
If the tree is absent, this is a non-fatal state: the analyzer logs that it is
using the in-memory defaults and that ODB was not modified. ODB writability does
not grant permission to initialize the schema. Default-schema creation is a
separate explicit administrative command; normal startup and live reload never
invoke it. Offline input uses the
same static defaults and does not require or contact an ODB server. The initial
read-only check runs after MIDAS connects even when the run is stopped. (The
installed XML/JSON `MVOdb` snapshot backends do not implement directory
enumeration, so ODB histogram groups are loaded from the live `MidasOdb`
backend only.) The ROOT output mirrors this grouping below `Histograms/`, for
example `Histograms/QDC0/h_qdc0_ch05`. Disabled slots have no ROOT object;
enabling a slot books and fills it, including during live reload. The ROOT web
server is only the standard THttpServer and does not create custom pages or
canvases unless page definitions are present under `/Analyzer/Pages`.

## ROOT Web pages

Page layouts are independent of histogram definitions and Enabled flags. A
page is stored read-only from ODB as:

```text
/Analyzer/Pages/TriggerMonitor/
    Rows    = 3
    Columns = 3
    Pad01   = "QDC0/h_qdc0_ch00"
    Pad02   = "TDC0/h_tdc0_ch00"
    Pad03   = "TLE0/h_tle0_ch05"
```

The supported layouts are 3x3, 4x4, and 4x5. At runtime the analyzer exposes
the canvases below the ROOT `Pages/` directory (for example,
`Pages/TriggerMonitor`). A pad containing an empty or missing histogram path
is left blank; histogram objects are drawn directly, without cloning or
resetting their contents. Page settings are polled with the other online ODB
controls and are rebuilt when the page definition changes. Removing a
histogram or a page therefore only leaves an empty/removed canvas and does not
stop the analyzer. No drag-and-drop or custom HTML editor is provided.
At BOR, configuration is loaded, validated, and freshly booked. During an
online run, the event loop polls at most once per second and reloads the ODB
directory using only `create=false` reads. A missing path is never created by
polling; if an administrator later creates it explicitly, the next poll
switches to the ODB configuration. It does not read ODB once per event. If raw
configuration changes, all enabled valid histograms are safely rebooked under
a mutex; contents reset at that point. This implements add, delete, HistName
change, binning/expression change, and Enabled toggling without stale objects.
EOR clears pending builder state and deletes live histogram objects. The
standard mhttpd ODB editor can be used to edit the schema; there is no custom
HTML page.

To initialize the tree explicitly, run the following only from the real DAQ
host environment:

```sh
./build/midas_analyzer --init-hist-odb
```

This management command connects to MIDAS/ODB, creates definitions from the
same `DefaultHistogramConfigs()` used by offline and in-memory fallback, reads
them back, validates every definition, disconnects, and exits. It does not
enter manalyzer, subscribe to the SYSTEM buffer, install run-transition
handling, create an event builder or ROOT writer, or start `THttpServer`.
If `/Analyzer/Histograms` already exists, including a partial or older tree, the command
returns an error without creating, repairing, or overwriting anything. After a
successful initialization, each group's `HistName`, `Title`, `XTitle`,
`YTitle`, `Type`, `Expression`, `Bins`, `Min`, `Max`, `Cut`, and
`Enabled` arrays can be edited with the standard mhttpd ODB editor.

Expressions are parsed during validation/booking, not once per event. Missing
values (`-999`), absent hits, and absent waveform samples are not filled.

Supported expressions are the scalar counters `event`, `vme_counter`,
`easiroc_counter`, `v792_counter`, `v775_counter`, `v1190_counter`,
`v1720_counter`, and `easiroc_raw_counter`; fixed data `qdc0[ch]`, `tdc0[ch]`,
and `eadc0[ch]`; and indexed data `tle0[ch][hit]`, `ttr0[ch][hit]`,
`etle0[ch][hit]`, `ettr0[ch][hit]`, and `fadc0[ch][sample]`.

## Online PDF export

In online mode, STOP retains the last run's booked histograms and page canvases
in ROOT Web. The next BOR reloads the latest histogram and page ODB settings,
rebooks histograms, and starts them with empty contents. Offline EOR still
writes and clears its ROOT objects. The retained online state supports PDF
export after STOP through `OnlineHistogramState::ExportPdf()`. When the
`/Analyzer` ODB tree exists at analyzer startup, an ODB watch also processes
the existing PDF `Request` control while STOPPED. The watch only reacts to
changes; it does not create ODB keys or generate a PDF without a request.

`HistogramPdfWriter` clones the active histograms under the manager mutex and
writes one histogram per page using ROOT's multipage PDF support. Pages include
the run number, histogram name, and generation timestamp. PDF output is never
periodic. To request one from the standard ODB editor, create:

```text
/Analyzer/HistogramPdf/
    Request    = true
    OutputFile = "optional_name.pdf"
```

The online poll writes the PDF only when `Request` is true, then resets the flag
to false to acknowledge it. A basename is written below
`/home/daq/midas/midas/plots/`; an empty OutputFile generates a timestamped
name there. No request keys are automatically created, and no PDF is generated
without an explicit request.

## Extension points

- Add a dedicated mhttpd custom page if ODB-editor control is no longer enough.
- Add a run-level `RunInfo`/ODB snapshot object without changing event data.
- Keep expression-facing names in `DecodedEvent`: `qdc0`, `tdc0`, `tle0`,
  `ttr0`, `fadc0`, `eadc0`, `etle0`, and `ettr0`.
- Add new raw-bank interpretation only inside the corresponding decoder.
