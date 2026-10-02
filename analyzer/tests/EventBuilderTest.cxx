#include "EventBuilder.h"

#include "midasio.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <set>

namespace {

bool Check(bool condition, const char* message) {
  if (condition) return true;
  std::fprintf(stderr, "FAIL: %s\n", message);
  return false;
}

TMEvent VmeEvent(std::uint32_t serial) {
  TMEvent event;
  event.Init(1, 0, serial);
  const std::array<std::uint32_t, 2> adc_words{0x02000000u,
                                               0x04000000u | serial};
  event.AddBank("ADC0", TID_DWORD,
                reinterpret_cast<const char*>(adc_words.data()),
                sizeof(adc_words));
  return event;
}

TMEvent EasirocEvent(std::uint32_t serial) {
  TMEvent event;
  event.Init(1, 0, serial);
  const std::array<std::uint16_t, 64> adc_words{};
  event.AddBank("EAHG", TID_WORD,
                reinterpret_cast<const char*>(adc_words.data()),
                sizeof(adc_words));
  event.AddBank("ETLE", TID_WORD, "", 0);
  event.AddBank("ETTR", TID_WORD, "", 0);
  return event;
}

}  // namespace

int main() {
  bool okay = true;
  std::size_t emitted = 0;
  ana::EventBuilder builder([&emitted](const ana::DecodedEvent&) { ++emitted; });

  builder.SetExpectedSources(ana::EventBuilder::ExpectedSources::kVmeOnly);
  auto vme1 = VmeEvent(1);
  builder.AddEvent(vme1, ana::RawEventSource::kVme);
  okay &= Check(emitted == 1 && builder.PendingCount() == 0 &&
                    builder.GetStatistics().vme_only == 1,
                "VME-only mode must emit immediately after VME decode");

  builder.Clear();
  emitted = 0;
  builder.SetExpectedSources(ana::EventBuilder::ExpectedSources::kEasirocOnly);
  auto easiroc2 = EasirocEvent(2);
  builder.AddEvent(easiroc2, ana::RawEventSource::kEasiroc);
  okay &= Check(emitted == 1 && builder.PendingCount() == 0 &&
                    builder.GetStatistics().easiroc_only == 1,
                "EASIROC-only mode must emit immediately after EASIROC decode");

  builder.Clear();
  emitted = 0;
  auto vme4 = VmeEvent(4);
  auto easiroc4 = EasirocEvent(4);
  auto vme6 = VmeEvent(6);
  auto easiroc6 = EasirocEvent(6);
  auto vme9 = VmeEvent(9);
  builder.AddEvent(vme4, ana::RawEventSource::kVme);
  okay &= Check(emitted == 0 && builder.PendingCount() == 1,
                "both-source mode must wait after VME alone");
  builder.AddEvent(easiroc4, ana::RawEventSource::kEasiroc);
  okay &= Check(emitted == 1 && builder.PendingCount() == 0 &&
                    builder.GetStatistics().paired == 1,
                "matching EASIROC serial must emit one paired event");
  builder.AddEvent(vme6, ana::RawEventSource::kVme);
  auto easiroc7 = EasirocEvent(7);
  builder.AddEvent(easiroc7, ana::RawEventSource::kEasiroc);
  okay &= Check(emitted == 1 && builder.PendingCount() == 2,
                "different serials must remain unpaired");
  builder.AddEvent(easiroc6, ana::RawEventSource::kEasiroc);
  okay &= Check(emitted == 2 && builder.PendingCount() == 1,
                "matching serial must pair without consuming mismatched event");
  builder.AddEvent(vme9, ana::RawEventSource::kVme);
  okay &= Check(emitted == 2 && builder.PendingCount() == 2,
                "single-source events must remain pending until Finish");
  builder.Finish();

  const auto& stats = builder.GetStatistics();
  okay &= Check(stats.paired == 2 && stats.vme_only == 1 &&
                    stats.easiroc_only == 1 && emitted == 4 &&
                    builder.PendingCount() == 0,
                "paired and single-source counts must match emitted events");
  okay &= Check(stats.vme_only_counters == std::set<std::uint32_t>{9},
                "single-source serial must be retained");
  okay &= Check(stats.easiroc_only_counters == std::set<std::uint32_t>{7},
                "unpaired EASIROC serial must be retained by Finish");
  okay &= Check(stats.v792.values == std::set<std::int64_t>({4, 6, 9}),
                "decoded hardware counters must be retained for gap reporting");
  okay &= Check(stats.decoder_errors == 0,
                "synthetic banks must decode without errors");
  return okay ? 0 : 1;
}
