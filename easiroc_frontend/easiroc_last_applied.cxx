#include "easiroc_last_applied.h"

#include <string>

namespace easiroc {
namespace {

HardwareConfigurationComparison mismatch(std::string detail) {
  return {HardwareConfigurationStatus::kMismatch, std::move(detail)};
}

}  // namespace

HardwareConfigurationComparison compareAsicSlowControlSettings(
    const AsicSlowControlSettings& settings,
    const AppliedAsicSlowControlSettings& last_applied,
    bool hardware_state_indeterminate) {
  if (hardware_state_indeterminate) {
    return {HardwareConfigurationStatus::kIndeterminate,
            "ASIC slow-control hardware state is indeterminate after an "
            "incomplete apply"};
  }
  if (!last_applied.valid) {
    return {HardwareConfigurationStatus::kUnknown,
            "No successfully applied ASIC slow-control configuration is "
            "recorded"};
  }

  for (std::size_t asic = 0; asic < settings.asic.size(); ++asic) {
    const std::string prefix = "ASIC" + std::to_string(asic + 1) + " ";
    const auto& desired = settings.asic[asic];
    const auto& applied = last_applied.asic[asic];
    if (desired.dac_code != applied.dac_code)
      return mismatch(prefix + "DiscriminatorDACCode differs");
    if (desired.dac_slope != applied.dac_slope)
      return mismatch(prefix + "DiscriminatorDACSlope differs");
    for (std::size_t channel = 0; channel < desired.input_dac.size();
         ++channel) {
      if (desired.input_dac[channel] != applied.input_dac[channel]) {
        return mismatch(prefix + "InputDAC[" + std::to_string(channel) +
                        "] differs");
      }
    }
  }
  return {HardwareConfigurationStatus::kMatch, ""};
}

AsicSlowControlConsistencySnapshot snapshotAsicSlowControlConsistency(
    const AsicSlowControlSettings& settings,
    const AppliedAsicSlowControlSettings& last_applied,
    bool hardware_state_indeterminate) {
  const auto comparison = compareAsicSlowControlSettings(
      settings, last_applied, hardware_state_indeterminate);
  return {last_applied.valid,
          comparison.status == HardwareConfigurationStatus::kMatch,
          hardware_state_indeterminate,
          last_applied.request_id,
          last_applied.apply_unix_time,
          comparison.status,
          comparison.detail};
}

bool shouldWarnForAsicSlowControlConsistency(
    bool frontend_enabled,
    const AsicSlowControlConsistencySnapshot& consistency) {
  return frontend_enabled && !consistency.configuration_match;
}

const char* hardwareConfigurationStatusName(
    HardwareConfigurationStatus status) {
  switch (status) {
    case HardwareConfigurationStatus::kUnknown:
      return "Unknown";
    case HardwareConfigurationStatus::kMatch:
      return "Match";
    case HardwareConfigurationStatus::kMismatch:
      return "Mismatch";
    case HardwareConfigurationStatus::kIndeterminate:
      return "Indeterminate";
  }
  return "Indeterminate";
}

}  // namespace easiroc
