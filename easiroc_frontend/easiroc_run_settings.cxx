#include "easiroc_run_settings.h"

namespace easiroc {

std::optional<std::string> validateAsicSlowControlSettings(
    const AsicSlowControlSettings& settings) {
  for (std::size_t index = 0; index < settings.asic.size(); ++index) {
    const auto& asic = settings.asic[index];
    const std::string prefix = "ASIC" + std::to_string(index + 1) + " ";
    if (asic.dac_code < 0 || asic.dac_code > 1023)
      return prefix + "DiscriminatorDACCode must be in range 0..1023";
    if (asic.dac_slope != 0 && asic.dac_slope != 1)
      return prefix + "DiscriminatorDACSlope must be 0 (coarse) or 1 (fine)";
    if (!isValidFeedbackCapacitanceFemtofarads(
            asic.hg_feedback_capacitance))
      return prefix + "HGFeedbackCapacitance=" +
             std::to_string(asic.hg_feedback_capacitance) +
             " fF must be one of 0 (NoC), 100, 200, ..., 1500 fF";
    if (!isValidFeedbackCapacitanceFemtofarads(
            asic.lg_feedback_capacitance))
      return prefix + "LGFeedbackCapacitance=" +
             std::to_string(asic.lg_feedback_capacitance) +
             " fF must be one of 0 (NoC), 100, 200, ..., 1500 fF";
    if (!isValidShapingTimeNanoseconds(asic.hg_shaping_time))
      return prefix + "HGShapingTime=" + std::to_string(asic.hg_shaping_time) +
             " ns must be one of 25, 50, 75, 100, 125, 150, 175 ns";
    if (!isValidShapingTimeNanoseconds(asic.lg_shaping_time))
      return prefix + "LGShapingTime=" + std::to_string(asic.lg_shaping_time) +
             " ns must be one of 25, 50, 75, 100, 125, 150, 175 ns";
    for (std::size_t channel = 0; channel < asic.input_dac.size(); ++channel) {
      if (asic.input_dac[channel] < 0 ||
          asic.input_dac[channel] > kInputDacMaximum) {
        return prefix + "InputDAC[" + std::to_string(channel) +
               "] must be in range 0..511";
      }
    }
  }
  return std::nullopt;
}

std::optional<std::string> validateAsicSlowControlBorSettings(
    bool frontend_enabled, const AsicSlowControlSettings& settings) {
  if (!frontend_enabled) return std::nullopt;
  if (const auto validation_error =
          validateAsicSlowControlSettings(settings))
    return validation_error;
  if (settings.apply_at_bor)
    return std::string(kApplyAtBorDeprecatedError);
  return std::nullopt;
}

}  // namespace easiroc
