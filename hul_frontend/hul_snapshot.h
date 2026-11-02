#pragma once

#include "hul_stream.h"

#include <array>
#include <chrono>
#include <cstdint>

namespace hul {

using Clock = std::chrono::steady_clock;

struct Record {
  std::array<uint32_t, kChannels> cumulative{};
  std::array<uint32_t, kChannels> delta{};
  uint32_t valid_low = 0;
  uint32_t valid_high = 0;
  uint64_t receive_unix_ms = 0;
  uint64_t accepted_events = 0;
  uint64_t missing_events = 0;
  uint32_t age_ms = 0;
  uint16_t self_counter = 0;
  bool cumulative_valid = false;
  bool fresh = false;
  bool fault = false;
  bool final = false;
  int32_t run_number = 0;
};

class SnapshotAccumulator {
 public:
  void reset(int32_t run_number);
  void accept(const ScalerEvent& event, Clock::time_point when,
              uint64_t unix_ms);
  Record take(Clock::time_point now, uint32_t freshness_ms, bool final,
              bool force_stale = false);
  bool has_value() const { return have_value_; }

 private:
  std::array<uint32_t, kChannels> latest_{};
  std::array<uint32_t, kChannels> previous_{};
  Clock::time_point received_at_{};
  uint64_t unix_ms_ = 0;
  uint64_t sequence_ = 0;
  uint64_t reported_sequence_ = 0;
  uint64_t missing_events_ = 0;
  uint16_t self_counter_ = 0;
  int32_t run_number_ = 0;
  bool have_value_ = false;
  bool have_previous_ = false;
};

}  // namespace hul
