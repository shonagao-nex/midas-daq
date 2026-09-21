#ifndef EASIROC_FRONTEND_EASIROC_RUN_SETTINGS_H
#define EASIROC_FRONTEND_EASIROC_RUN_SETTINGS_H

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace easiroc {

constexpr int kDefaultDiscriminatorDacCode = 600;
constexpr int kDefaultDiscriminatorDacSlope = 1;
constexpr int kDefaultInputDac = 350;
constexpr std::size_t kInputDacChannelCount = 32;
constexpr int kInputDacMaximum = 511;
constexpr std::uint32_t kEasirocRunSnapshotSchemaVersion = 5;
inline constexpr std::string_view kApplyAtBorDeprecatedError =
    "ApplyAtBOR is deprecated and must be FALSE; use manual slow-control "
    "Apply while STOPPED";

constexpr std::array<int, kInputDacChannelCount> defaultInputDacValues() {
  std::array<int, kInputDacChannelCount> values{};
  for (auto& value : values) value = kDefaultInputDac;
  return values;
}

struct AsicDiscriminatorSettings {
  int dac_code = kDefaultDiscriminatorDacCode;
  int dac_slope = kDefaultDiscriminatorDacSlope;
  std::array<int, kInputDacChannelCount> input_dac =
      defaultInputDacValues();
};

struct AsicSlowControlSettings {
  bool apply_at_bor = false;
  std::array<AsicDiscriminatorSettings, 2> asic;
};

// These relative paths are shared by the RunSnapshot publisher and its
// offline schema test. The complete RunSnapshot subtree is serialized into
// the ECFG bank by the frontend.
inline constexpr std::array<std::string_view, 7>
    kAsicSlowControlRequestedSnapshotPaths{{
        "ASICSlowControl/ApplyAtBOR",
        "ASIC1/DiscriminatorDACCode",
        "ASIC1/DiscriminatorDACSlope",
        "ASIC2/DiscriminatorDACCode",
        "ASIC2/DiscriminatorDACSlope",
        "ASIC1/InputDAC",
        "ASIC2/InputDAC",
    }};

std::optional<std::string> validateAsicSlowControlSettings(
    const AsicSlowControlSettings& settings);

// Disabled frontends do not validate unused hardware configuration.
std::optional<std::string> validateAsicSlowControlBorSettings(
    bool frontend_enabled, const AsicSlowControlSettings& settings);

}  // namespace easiroc

#endif
