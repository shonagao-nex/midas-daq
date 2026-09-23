#ifndef ANA_DECODED_EVENT_H
#define ANA_DECODED_EVENT_H

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace ana {

constexpr std::int64_t kInvalidCounter = -999;
constexpr std::int32_t kInvalidValue = -999;

struct EventCounters {
  std::int64_t event = kInvalidCounter;
  std::int64_t vme = kInvalidCounter;
  std::int64_t easiroc = kInvalidCounter;
  std::int64_t v792 = kInvalidCounter;
  std::int64_t v775 = kInvalidCounter;
  std::int64_t v1190 = kInvalidCounter;
  std::int64_t v1720 = kInvalidCounter;
  std::int64_t nim_easiroc = kInvalidCounter;

  void Clear() { *this = EventCounters{}; }
};

struct V792Data {
  std::array<std::int32_t, 32> qdc0{};
  void Clear() { qdc0.fill(kInvalidValue); }
  V792Data() { Clear(); }
};

struct V775Data {
  std::array<std::int32_t, 32> tdc0{};
  void Clear() { tdc0.fill(kInvalidValue); }
  V775Data() { Clear(); }
};

struct V1190Data {
  std::vector<std::vector<std::int32_t>> tle0;
  std::vector<std::vector<std::int32_t>> ttr0;

  V1190Data() {
    tle0.resize(128);
    ttr0.resize(128);
  }

  void Clear() {
    for (auto& hits : tle0) hits.clear();
    for (auto& hits : ttr0) hits.clear();
    assert(tle0.size() == 128);
    assert(ttr0.size() == 128);
  }
};

struct V1720Data {
  std::vector<std::vector<std::int32_t>> fadc0;

  V1720Data() { fadc0.resize(8); }

  void Clear() {
    for (auto& samples : fadc0) samples.clear();
    assert(fadc0.size() == 8);
  }
};

struct EasirocData {
  std::array<std::int32_t, 64> eadc0{};
  std::vector<std::vector<std::int32_t>> etle0;
  std::vector<std::vector<std::int32_t>> ettr0;

  EasirocData() {
    etle0.resize(64);
    ettr0.resize(64);
    Clear();
  }

  void Clear() {
    eadc0.fill(kInvalidValue);
    for (auto& hits : etle0) hits.clear();
    for (auto& hits : ettr0) hits.clear();
    assert(etle0.size() == 64);
    assert(ettr0.size() == 64);
  }
};

struct DecodedEvent {
  EventCounters counters;
  V792Data v792;
  V775Data v775;
  V1190Data v1190;
  V1720Data v1720;
  EasirocData easiroc;

  void Clear() {
    counters.Clear();
    v792.Clear();
    v775.Clear();
    v1190.Clear();
    v1720.Clear();
    easiroc.Clear();
  }
};

}  // namespace ana

#endif
