#include "EventDiagnostics.h"

#include "midasio.h"

#include <cstdio>

namespace {

bool Check(bool condition, const char* message) {
  if (condition) return true;
  std::fprintf(stderr, "FAIL: %s\n", message);
  return false;
}

}  // namespace

int main() {
  bool okay = true;
  ana::EventDiagnostics normal;
  TMEvent special;
  special.Init(0x8000);
  for (int i = 0; i < 9; ++i) normal.RecordSpecialEvent(special);
  okay &= Check(normal.EventIdCount(0x8000) == 9,
                "special event IDs must be counted");
  okay &= Check(normal.DetailedPrinted() == 0,
                "normal mode must not print event details");

  ana::EventDiagnostics debug(true);
  for (int i = 0; i < 9; ++i) debug.RecordSpecialEvent(special);
  okay &= Check(debug.DetailedPrinted() == 8,
                "debug output must stop after eight events");

  TMEvent vme;
  vme.Init(1, 0, 4);
  okay &= Check(debug.BeginEvent(vme), "valid event must be accepted");
  debug.RecordSourceEvent(vme, ana::RawEventSource::kVme);
  vme.serial_number = 6;
  debug.RecordSourceEvent(vme, ana::RawEventSource::kVme);
  okay &= Check(debug.EventIdCount(1) == 1,
                "ordinary event IDs must be counted");
  okay &= Check(debug.SerialGaps(ana::RawEventSource::kVme) == 1,
                "missing serial number must count as a gap");
  okay &= Check(debug.SerialGaps(ana::RawEventSource::kEasiroc) == 0,
                "empty source must have no gaps");
  okay &= Check(debug.DetailedPrinted() == 8,
                "ordinary events must share the debug output limit");

  ana::EventBuilder::Statistics::RawCounters raw_counters;
  raw_counters.values = {4, 6, 9};
  okay &= Check(ana::EventDiagnostics::RawCounterGaps(raw_counters) == 3,
                "hardware raw counter gaps must use decoded counter values");

  debug.Reset();
  okay &= Check(debug.EventIdCount(1) == 0 &&
                    debug.SerialGaps(ana::RawEventSource::kVme) == 0 &&
                    debug.DetailedPrinted() == 0,
                "new run must clear diagnostics");
  return okay ? 0 : 1;
}
