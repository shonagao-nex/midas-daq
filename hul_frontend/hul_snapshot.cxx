#include "hul_snapshot.h"

#include <algorithm>
#include <limits>

namespace hul {
void SnapshotAccumulator::reset(int32_t run_number) {
  *this = SnapshotAccumulator{};
  run_number_ = run_number;
}

void SnapshotAccumulator::accept(const ScalerEvent& event,
                                 Clock::time_point when, uint64_t unix_ms) {
  if (have_value_) {
    const uint16_t step = static_cast<uint16_t>(event.self_counter - self_counter_);
    if (step > 1) missing_events_ += step - 1;
  }
  latest_ = event.count;
  self_counter_ = event.self_counter;
  received_at_ = when;
  unix_ms_ = unix_ms;
  have_value_ = true;
  ++sequence_;
}

Record SnapshotAccumulator::take(Clock::time_point now, uint32_t freshness_ms,
                                 bool final, bool force_stale) {
  Record out;
  out.run_number = run_number_;
  out.final = final;
  out.fault = force_stale;
  out.accepted_events = sequence_;
  out.missing_events = missing_events_;
  out.cumulative_valid = have_value_;
  if (have_value_) {
    out.cumulative = latest_;
    out.self_counter = self_counter_;
    out.receive_unix_ms = unix_ms_;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - received_at_).count();
    out.age_ms = static_cast<uint32_t>(std::min<int64_t>(
        std::max<int64_t>(ms, 0), std::numeric_limits<uint32_t>::max()));
  }
  out.fresh = !force_stale && have_value_ && sequence_ != reported_sequence_ &&
              out.age_ms <= freshness_ms;
  if (out.fresh && have_previous_) {
    for (size_t ch = 0; ch < kChannels; ++ch) {
      if (latest_[ch] < previous_[ch]) continue;
      out.delta[ch] = latest_[ch] - previous_[ch];
      if (ch < 32) out.valid_low |= 1u << ch;
      else out.valid_high |= 1u << (ch - 32);
    }
  }
  if (out.fresh) previous_ = latest_;
  have_previous_ = out.fresh;
  reported_sequence_ = sequence_;
  return out;
}
}  // namespace hul
