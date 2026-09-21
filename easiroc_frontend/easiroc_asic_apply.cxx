#include "easiroc_asic_apply.h"

#include <exception>
#include <stdexcept>

namespace easiroc {
namespace {

constexpr std::size_t kAsicApplyTransactionCount = 7;

std::string failureMessage(std::size_t transaction_index,
                           const std::string& detail) {
  return "ASIC slow-control transaction " +
         std::to_string(transaction_index + 1) + "/" +
         std::to_string(kAsicApplyTransactionCount) + " failed: " + detail +
         "; ASIC slow-control state may be unknown after partial apply "
         "failure";
}

}  // namespace

AsicApplyResult executeAsicApplyPlan(
    const std::vector<Transaction>& plan, const SlowControlWrite& write,
    const SlowControlDelay& delay) {
  AsicApplyResult result;
  result.attempted = true;
  if (plan.size() != kAsicApplyTransactionCount) {
    result.error =
        "ASIC-only apply plan must contain exactly 7 transactions; ASIC "
        "slow-control state may be unknown after partial apply failure";
    return result;
  }

  for (std::size_t index = 0; index < plan.size(); ++index) {
    const auto& transaction = plan[index];
    try {
      switch (transaction.type) {
        case TransactionType::kWrite:
          write(transaction.address, transaction.data);
          break;
        case TransactionType::kDelay:
          delay(transaction.delay_ms);
          break;
      }
      ++result.completed_transactions;
    } catch (const std::exception& error) {
      result.error = failureMessage(index, error.what());
      return result;
    } catch (...) {
      result.error = failureMessage(index, "unknown exception");
      return result;
    }
  }

  result.sequence_succeeded = true;
  return result;
}

AsicApplyResult applyAsicSlowControlAtBor(
    const AsicSlowControlSettings& settings, const SlowControlWrite& write,
    const SlowControlDelay& delay) {
  if (!settings.apply_at_bor) return {};

  AsicApplyResult result;
  result.attempted = true;
  try {
    if (const auto validation_error =
            validateAsicSlowControlSettings(settings)) {
      result.error = *validation_error;
      return result;
    }
    const auto images = SlowControlPolicy::encodeLegacySiteThresholdOverlay(
        static_cast<std::uint16_t>(settings.asic[0].dac_code),
        static_cast<std::uint16_t>(settings.asic[0].dac_slope),
        static_cast<std::uint16_t>(settings.asic[1].dac_code),
        static_cast<std::uint16_t>(settings.asic[1].dac_slope));
    static_assert(SlowControlEncoder::Image{}.size() == 57,
                  "ASIC slow-control image must be 57 bytes");
    return executeAsicApplyPlan(SlowControlPolicy::buildAsicApplyPlan(images),
                                write, delay);
  } catch (const std::exception& error) {
    result.error = std::string("ASIC slow-control preparation failed: ") +
                   error.what();
    return result;
  }
}

bool asicApplyPermitsDaqOn(const AsicApplyResult& result) {
  return !result.attempted || result.sequence_succeeded;
}

}  // namespace easiroc
