#ifndef EASIROC_FRONTEND_EASIROC_LAST_APPLIED_H
#define EASIROC_FRONTEND_EASIROC_LAST_APPLIED_H

#include "easiroc_run_settings.h"

#include <array>
#include <cstdint>
#include <string>

namespace easiroc {

// This is a record of the last successfully transmitted configuration, not an
// ASIC hardware readback.
struct AppliedAsicSlowControlSettings {
  bool valid = false;
  std::uint32_t request_id = 0;
  std::uint64_t apply_unix_time = 0;
  std::array<AsicDiscriminatorSettings, 2> asic;
};

enum class HardwareConfigurationStatus {
  kUnknown,
  kMatch,
  kMismatch,
  kIndeterminate,
};

struct HardwareConfigurationComparison {
  HardwareConfigurationStatus status = HardwareConfigurationStatus::kUnknown;
  std::string detail;
};

// A value snapshot created at BOR. It deliberately contains only the
// LastApplied identity and comparison result: Requested already carries the
// full desired setting snapshot.
struct AsicSlowControlConsistencySnapshot {
  bool last_applied_valid = false;
  bool configuration_match = false;
  bool hardware_state_indeterminate = false;
  std::uint32_t last_applied_request_id = 0;
  std::uint64_t last_applied_unix_time = 0;
  HardwareConfigurationStatus status = HardwareConfigurationStatus::kUnknown;
  std::string detail;
};

// ApplyAtBOR is deliberately excluded: it is a deprecated policy field, not
// a hardware configuration value.
HardwareConfigurationComparison compareAsicSlowControlSettings(
    const AsicSlowControlSettings& settings,
    const AppliedAsicSlowControlSettings& last_applied,
    bool hardware_state_indeterminate);

AsicSlowControlConsistencySnapshot snapshotAsicSlowControlConsistency(
    const AsicSlowControlSettings& settings,
    const AppliedAsicSlowControlSettings& last_applied,
    bool hardware_state_indeterminate);

bool shouldWarnForAsicSlowControlConsistency(
    bool frontend_enabled,
    const AsicSlowControlConsistencySnapshot& consistency);

const char* hardwareConfigurationStatusName(
    HardwareConfigurationStatus status);

}  // namespace easiroc

#endif
