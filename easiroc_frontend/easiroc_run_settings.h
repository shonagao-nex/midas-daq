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
// ODB stores the physical capacitance in fF. Zero denotes the legacy "NoC"
// option; the non-linear raw ASIC encoder codes remain explicit below.
struct FeedbackCapacitanceOption {
  int femtofarads;
  std::uint16_t encoder_code;
};

inline constexpr std::array<FeedbackCapacitanceOption, 16>
    kFeedbackCapacitanceOptions{{
        {0, 0}, {100, 8}, {200, 4}, {300, 12}, {400, 2}, {500, 10},
        {600, 6}, {700, 14}, {800, 1}, {900, 9}, {1000, 5}, {1100, 13},
        {1200, 3}, {1300, 11}, {1400, 7}, {1500, 15},
    }};
constexpr int kDefaultFeedbackCapacitanceFemtofarads = 100;
// The legacy encoder aliases use consecutive 3-bit codes 1..7 for these
// physical shaping times. ODB deliberately stores the physical time in ns.
struct ShapingTimeOption {
  int nanoseconds;
  std::uint16_t encoder_code;
};

inline constexpr std::array<ShapingTimeOption, 7> kShapingTimeOptions{{
    {25, 1}, {50, 2}, {75, 3}, {100, 4}, {125, 5}, {150, 6}, {175, 7},
}};
constexpr int kDefaultHighGainShapingTimeNanoseconds = 100;
constexpr int kDefaultLowGainShapingTimeNanoseconds = 50;
constexpr std::uint32_t kEasirocRunSnapshotSchemaVersion = 8;
inline constexpr std::string_view kApplyAtBorDeprecatedError =
    "ApplyAtBOR is deprecated and must be FALSE; use manual slow-control "
    "Apply while STOPPED";

constexpr std::array<int, kInputDacChannelCount> defaultInputDacValues() {
  std::array<int, kInputDacChannelCount> values{};
  for (auto& value : values) value = kDefaultInputDac;
  return values;
}

// ODB/UI stores the operator-facing enabled state. In the ASIC's logical
// Discriminator Mask field, false means mask=1 and true means mask=0.
constexpr std::array<bool, kInputDacChannelCount>
defaultChannelEnabledValues() {
  std::array<bool, kInputDacChannelCount> values{};
  for (auto& value : values) value = true;
  return values;
}

constexpr bool areAllChannelsEnabled(
    const std::array<bool, kInputDacChannelCount>& values) {
  for (const bool value : values)
    if (!value) return false;
  return true;
}

constexpr bool isValidFeedbackCapacitanceFemtofarads(int value) {
  for (const auto& option : kFeedbackCapacitanceOptions)
    if (option.femtofarads == value) return true;
  return false;
}

constexpr std::uint16_t feedbackCapacitanceEncoderCode(int femtofarads) {
  for (const auto& option : kFeedbackCapacitanceOptions)
    if (option.femtofarads == femtofarads) return option.encoder_code;
  return 0xffff;
}

constexpr bool isValidShapingTimeNanoseconds(int value) {
  for (const auto& option : kShapingTimeOptions)
    if (option.nanoseconds == value) return true;
  return false;
}

constexpr std::uint16_t shapingTimeEncoderCode(int nanoseconds) {
  for (const auto& option : kShapingTimeOptions)
    if (option.nanoseconds == nanoseconds) return option.encoder_code;
  return 0xffff;
}

struct AsicDiscriminatorSettings {
  int dac_code = kDefaultDiscriminatorDacCode;
  int dac_slope = kDefaultDiscriminatorDacSlope;
  int hg_feedback_capacitance = kDefaultFeedbackCapacitanceFemtofarads;
  int lg_feedback_capacitance = kDefaultFeedbackCapacitanceFemtofarads;
  int hg_shaping_time = kDefaultHighGainShapingTimeNanoseconds;
  int lg_shaping_time = kDefaultLowGainShapingTimeNanoseconds;
  std::array<int, kInputDacChannelCount> input_dac =
      defaultInputDacValues();
  std::array<bool, kInputDacChannelCount> channel_enabled =
      defaultChannelEnabledValues();
};

struct AsicSlowControlSettings {
  bool apply_at_bor = false;
  std::array<AsicDiscriminatorSettings, 2> asic;
};

// These relative paths are shared by the RunSnapshot publisher and its
// offline schema test. The complete RunSnapshot subtree is serialized into
// the ECFG bank by the frontend.
inline constexpr std::array<std::string_view, 17>
    kAsicSlowControlRequestedSnapshotPaths{{
        "ASICSlowControl/ApplyAtBOR",
        "ASIC1/DiscriminatorDACCode",
        "ASIC1/DiscriminatorDACSlope",
        "ASIC1/HGFeedbackCapacitance",
        "ASIC1/LGFeedbackCapacitance",
        "ASIC1/HGShapingTime",
        "ASIC1/LGShapingTime",
        "ASIC1/InputDAC",
        "ASIC1/ChannelEnabled",
        "ASIC2/DiscriminatorDACCode",
        "ASIC2/DiscriminatorDACSlope",
        "ASIC2/HGFeedbackCapacitance",
        "ASIC2/LGFeedbackCapacitance",
        "ASIC2/HGShapingTime",
        "ASIC2/LGShapingTime",
        "ASIC2/InputDAC",
        "ASIC2/ChannelEnabled",
    }};

std::optional<std::string> validateAsicSlowControlSettings(
    const AsicSlowControlSettings& settings);

// Disabled frontends do not validate unused hardware configuration.
std::optional<std::string> validateAsicSlowControlBorSettings(
    bool frontend_enabled, const AsicSlowControlSettings& settings);

}  // namespace easiroc

#endif
