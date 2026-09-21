#ifndef EASIROC_FRONTEND_EASIROC_ASIC_APPLY_H
#define EASIROC_FRONTEND_EASIROC_ASIC_APPLY_H

#include "easiroc_run_settings.h"
#include "easiroc_slow_control.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace easiroc {

using SlowControlWrite = std::function<void(
    std::uint32_t, const std::vector<std::uint8_t>&)>;
using SlowControlDelay = std::function<void(unsigned)>;

struct AsicApplyResult {
  bool attempted = false;
  bool sequence_succeeded = false;
  std::size_t completed_transactions = 0;
  std::string error;
};

AsicApplyResult executeAsicApplyPlan(
    const std::vector<Transaction>& plan, const SlowControlWrite& write,
    const SlowControlDelay& delay);

// Legacy preparation wrapper retained for offline coverage. The production
// BOR path does not call this function. ApplyAtBOR=false returns without
// encoding, planning, writing, or delaying.
AsicApplyResult applyAsicSlowControlAtBor(
    const AsicSlowControlSettings& settings, const SlowControlWrite& write,
    const SlowControlDelay& delay);

bool asicApplyPermitsDaqOn(const AsicApplyResult& result);

}  // namespace easiroc

#endif
