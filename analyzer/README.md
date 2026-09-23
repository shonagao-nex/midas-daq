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

  -f FILE    offline MIDAS input
  -w FILE    offline ROOT output
  -n N       maximum emitted DecodedEvent count (0 means unlimited)
  -h         help
```

Online monitoring starts the standard ROOT `THttpServer` on localhost port
8081 by default:

```sh
./build/midas_analyzer --no-profiler
```

No online ROOT file is opened. `RootTreeWriter` is not constructed, and live
histograms are placed in manalyzer's in-memory `TARootHelper::fgDir`. The
process remains in the foreground and manalyzer keeps it connected while the
run is stopped; BOR creates a run object and EOR destroys it without exiting
the process. Use `-RPORT` to select another ROOT web port. To expose the
standard JSROOT page through mhttpd, configure the documented proxy entries,
for example `/WebServer/Proxy/rootana = http://localhost:8081` and an
appropriate `/Alias/rootana` entry.

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

At begin-of-run, `HistogramConfigLoader` reads one subdirectory per histogram
below `/Analyzer/Histograms`. It uses the `MVOdb` supplied by
`TARunInfo::fOdb`; `HistogramManager` and `ExpressionResolver` do not know an
ODB path or use an ODB API. A typical entry is:

```text
/Analyzer/Histograms/h_qdc0_ch0/
    HistName    = "h_qdc0_ch0"
    Type        = "TH1D"
    Expression  = "qdc0[0]"
    Bins        = 4096
    Min         = 0
    Max         = 4096
    Cut         = ""
    Enabled     = true
```

The fields mean:

- `HistName`: ROOT object name. It is deliberately separate from the ODB
  subkey name so a future GUI can rename or copy definitions.
- `Type`: histogram class; currently only `TH1D` is supported.
- `Expression`: one supported `DecodedEvent` value to fill.
- `Bins`, `Min`, `Max`: axis bin count and limits (`Bins > 0`, `Min < Max`).
- `Cut`: selection expression; currently only the empty string is supported.
- `Enabled`: a false value keeps the definition but does not book or fill it.

Each definition is validated before `HistogramManager` sees it. An unsupported
type or cut, an empty name, invalid binning, or an invalid expression produces
a warning and skips only that histogram. If several subkeys contain the same
`HistName`, the first definition in ODB directory enumeration order wins and
later definitions are warned and skipped. This avoids a silent overwrite and
makes the selected definition deterministic for a given ODB layout.

If `/Analyzer/Histograms` is absent in writable online ODB, the analyzer creates
the default definitions at begin-of-run. Offline input falls back to the same
static defaults and does not require or contact an ODB server. (The installed
XML/JSON `MVOdb` snapshot backends do not implement directory enumeration, so
ODB histogram subkeys are loaded from the live `MidasOdb` backend only.) The
defaults are `h_event`,
`h_qdc0_ch0`, `h_eadc0_ch0`, `h_tle0_ch0_hit0`, and
`h_fadc0_ch0_sample0`; the first three are the baseline regression histograms.
At BOR, configuration is loaded, validated, and freshly booked. During an
online run, the event loop polls at most once per second and reloads the ODB
directory. It does not read ODB once per event. If raw configuration changes,
all enabled valid histograms are safely rebooked under a mutex; contents reset
at that point. This implements add, delete, HistName change, binning/expression
change, and Enabled toggling without stale objects. EOR clears pending builder
state and deletes live histogram objects. The standard mhttpd ODB editor can be
used to edit the schema; there is no custom HTML page.

Expressions are parsed during validation/booking, not once per event. Missing
values (`-999`), absent hits, and absent waveform samples are not filled.

Supported expressions are the scalar counters `event`, `vme_counter`,
`easiroc_counter`, `v792_counter`, `v775_counter`, `v1190_counter`,
`v1720_counter`, and `easiroc_raw_counter`; fixed data `qdc0[ch]`, `tdc0[ch]`,
and `eadc0[ch]`; and indexed data `tle0[ch][hit]`, `ttr0[ch][hit]`,
`etle0[ch][hit]`, `ettr0[ch][hit]`, and `fadc0[ch][sample]`.

## Online PDF export

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
