# MIDAS common analyzer

This directory contains the initial offline/online common analysis layer for
the VME and NIM-EASIROC frontends. `EventInspector` classifies physics events
by bank composition (not event ID), reports the raw MIDAS structure, and feeds
`EventBuilder`. `EventBuilder` joins equal frontend serial numbers without
dropping one-sided events. Hardware decoders write only to `DecodedEvent`.
`RootTreeWriter` stores each built `DecodedEvent` as one entry in the `Events`
TTree. No live ODB access or hardware access is performed when an input file is
specified. Histogram and ODB snapshot writers are not implemented yet.

## Data flow

```text
TMEvent
  -> EventInspector (classification and raw-bank statistics)
  -> EventBuilder (join by frontend serial)
  -> module decoders
  -> DecodedEvent
  -> RootTreeWriter
  -> ROOT file / Events TTree
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
builder pairing statistics. Missing counterparts produce `WARNING` messages
and are still delivered as one-sided `DecodedEvent` objects.

## Extension points

- Attach additional output modules through `EventBuilder::Consumer`.
- Add a run-level `RunInfo`/ODB snapshot object without changing event data.
- Keep expression-facing names in `DecodedEvent`: `qdc0`, `tdc0`, `tle0`,
  `ttr0`, `fadc0`, `eadc0`, `etle0`, and `ettr0`.
- Add new raw-bank interpretation only inside the corresponding decoder.
