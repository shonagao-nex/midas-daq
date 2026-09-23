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
     -> RootTreeWriter -> ROOT file / Events TTree
     -> HistogramManager -> ExpressionResolver -> ROOT file / Histograms/
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

## Offline run

Load the ROOT runtime environment, then pass the raw file directly to
manalyzer:

```sh
source /home/daq/root/current/bin/thisroot.sh
cd /home/daq/midas/midas/online/analyzer
./build/midas_analyzer --no-profiler -O/tmp/run00062.root \
  /home/daq/midas/midas/data/run00062.mid.lz4
```

Use manalyzer's `-O` option for an explicit ROOT filename or `-D` for its
standard per-run output directory and filename. The first few records are
printed in detail. The end-of-run summary contains
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
Configuration is loaded only at analyzer startup/BOR. Live add, delete, enable,
or binning reload is not implemented yet. The standard mhttpd ODB editor can be
used to edit the schema; there is no custom HTML page.

Expressions are parsed during validation/booking, not once per event. Missing
values (`-999`), absent hits, and absent waveform samples are not filled.

Supported expressions are the scalar counters `event`, `vme_counter`,
`easiroc_counter`, `v792_counter`, `v775_counter`, `v1190_counter`,
`v1720_counter`, and `easiroc_raw_counter`; fixed data `qdc0[ch]`, `tdc0[ch]`,
and `eadc0[ch]`; and indexed data `tle0[ch][hit]`, `ttr0[ch][hit]`,
`etle0[ch][hit]`, `ettr0[ch][hit]`, and `fadc0[ch][sample]`.

## Extension points

- Add live histogram reload by repeating load, validation, and apply at a safe
  run boundary.
- Add a run-level `RunInfo`/ODB snapshot object without changing event data.
- Keep expression-facing names in `DecodedEvent`: `qdc0`, `tdc0`, `tle0`,
  `ttr0`, `fadc0`, `eadc0`, `etle0`, and `ettr0`.
- Add new raw-bank interpretation only inside the corresponding decoder.
