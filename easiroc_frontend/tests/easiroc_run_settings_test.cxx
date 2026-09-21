#include "easiroc_run_settings.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {
int tests_run = 0;

void check(bool condition, const std::string& message) {
  ++tests_run;
  if (!condition) throw std::runtime_error(message);
}

bool valid(const easiroc::AsicSlowControlSettings& settings) {
  return !easiroc::validateAsicSlowControlSettings(settings).has_value();
}
}  // namespace

int main() {
  try {
    const easiroc::AsicSlowControlSettings defaults;
    check(!defaults.apply_at_bor, "ApplyAtBOR default is not false");
    for (const auto& asic : defaults.asic) {
      check(asic.dac_code == 600, "DACCode default is not 600");
      check(asic.dac_slope == 1, "DACSlope default is not fine (1)");
      check(asic.hg_feedback_capacitance == 100 &&
                asic.lg_feedback_capacitance == 100,
            "feedback capacitance default is not legacy 100 fF");
      check(asic.hg_shaping_time == 100 && asic.lg_shaping_time == 50,
            "shaping time defaults are not legacy HG=100 ns/LG=50 ns");
      check(asic.input_dac.size() == 32,
            "InputDAC default does not contain 32 channels");
      check(std::all_of(asic.input_dac.begin(), asic.input_dac.end(),
                        [](int value) { return value == 350; }),
            "InputDAC default is not 350 for every channel");
      check(std::all_of(asic.channel_enabled.begin(),
                        asic.channel_enabled.end(),
                        [](bool value) { return value; }),
            "ChannelEnabled default is not TRUE for every channel");
    }
    check(easiroc::areAllChannelsEnabled(defaults.asic[0].channel_enabled),
          "all-enabled Channel Mask default was not accepted");
    for (const std::size_t channel : {std::size_t{0}, std::size_t{16},
                                      std::size_t{31}}) {
      auto masked = defaults.asic[0].channel_enabled;
      masked[channel] = false;
      check(!easiroc::areAllChannelsEnabled(masked),
            "masked ChannelEnabled value was not detected at channel " +
                std::to_string(channel));
    }
    auto independent_masks = defaults;
    independent_masks.asic[0].channel_enabled[16] = false;
    check(!easiroc::areAllChannelsEnabled(
              independent_masks.asic[0].channel_enabled) &&
              easiroc::areAllChannelsEnabled(
                  independent_masks.asic[1].channel_enabled),
          "ASIC1 ChannelEnabled modification affected ASIC2");

    for (const auto& option : easiroc::kShapingTimeOptions) {
      check(easiroc::isValidShapingTimeNanoseconds(option.nanoseconds) &&
                easiroc::shapingTimeEncoderCode(option.nanoseconds) ==
                    option.encoder_code,
            "shaping time physical/code mapping differs from legacy aliases");
    }
    for (const int invalid_shaping : {0, 24, 26, 200}) {
      auto settings = defaults;
      settings.asic[0].hg_shaping_time = invalid_shaping;
      const auto error = easiroc::validateAsicSlowControlSettings(settings);
      check(error && error->find("ASIC1 HGShapingTime=") != std::string::npos,
            "invalid ASIC1 HG shaping lacks field/value diagnostic");
      settings = defaults;
      settings.asic[1].lg_shaping_time = invalid_shaping;
      const auto lg_error = easiroc::validateAsicSlowControlSettings(settings);
      check(lg_error && lg_error->find("ASIC2 LGShapingTime=") !=
                            std::string::npos,
            "invalid ASIC2 LG shaping lacks field/value diagnostic");
    }

    for (const auto& option : easiroc::kFeedbackCapacitanceOptions) {
      check(easiroc::isValidFeedbackCapacitanceFemtofarads(option.femtofarads) &&
                easiroc::feedbackCapacitanceEncoderCode(option.femtofarads) ==
                    option.encoder_code,
            "feedback capacitance physical/code mapping differs from legacy aliases");
    }
    for (const int invalid_feedback : {-1, 1, 99, 1600}) {
      auto settings = defaults;
      settings.asic[0].hg_feedback_capacitance = invalid_feedback;
      const auto error = easiroc::validateAsicSlowControlSettings(settings);
      check(error && error->find("ASIC1 HGFeedbackCapacitance=") !=
                         std::string::npos,
            "invalid ASIC1 HG feedback lacks field/value diagnostic");
      settings = defaults;
      settings.asic[1].lg_feedback_capacitance = invalid_feedback;
      const auto lg_error = easiroc::validateAsicSlowControlSettings(settings);
      check(lg_error && lg_error->find("ASIC2 LGFeedbackCapacitance=") !=
                            std::string::npos,
            "invalid ASIC2 LG feedback lacks field/value diagnostic");
    }

    for (const int dac_code : {0, 600, 1023}) {
      for (const int dac_slope : {0, 1}) {
        auto settings = defaults;
        settings.asic[0].dac_code = dac_code;
        settings.asic[0].dac_slope = dac_slope;
        settings.asic[1].dac_code = dac_code;
        settings.asic[1].dac_slope = dac_slope;
        check(valid(settings), "valid DACCode/DACSlope boundary was rejected");
      }
    }

    for (const int invalid_code : {-1, 1024}) {
      auto settings = defaults;
      settings.asic[0].dac_code = invalid_code;
      check(!valid(settings), "invalid ASIC1 DACCode was accepted");
      settings = defaults;
      settings.asic[1].dac_code = invalid_code;
      check(!valid(settings), "invalid ASIC2 DACCode was accepted");
    }
    for (const int invalid_slope : {-1, 2}) {
      auto settings = defaults;
      settings.asic[0].dac_slope = invalid_slope;
      check(!valid(settings), "invalid ASIC1 DACSlope was accepted");
      settings = defaults;
      settings.asic[1].dac_slope = invalid_slope;
      check(!valid(settings), "invalid ASIC2 DACSlope was accepted");
    }

    for (const std::size_t channel : {std::size_t{0}, std::size_t{17},
                                      std::size_t{31}}) {
      for (const int value : {0, 511}) {
        auto settings = defaults;
        settings.asic[0].input_dac[channel] = value;
        settings.asic[1].input_dac[channel] = value;
        check(valid(settings),
              "valid InputDAC boundary/channel was rejected");
      }
    }

    auto invalid_input_dac = defaults;
    invalid_input_dac.asic[0].input_dac[17] = 512;
    const auto asic1_input_error =
        easiroc::validateAsicSlowControlSettings(invalid_input_dac);
    check(asic1_input_error &&
              *asic1_input_error ==
                  "ASIC1 InputDAC[17] must be in range 0..511",
          "ASIC1 upper-bound InputDAC error lacks ASIC/channel/range");

    invalid_input_dac = defaults;
    invalid_input_dac.asic[1].input_dac[31] = -1;
    const auto asic2_input_error =
        easiroc::validateAsicSlowControlSettings(invalid_input_dac);
    check(asic2_input_error &&
              *asic2_input_error ==
                  "ASIC2 InputDAC[31] must be in range 0..511",
          "ASIC2 negative InputDAC error lacks ASIC/channel/range");

    auto apply_requested = defaults;
    apply_requested.apply_at_bor = true;
    const auto deprecated_apply_error =
        easiroc::validateAsicSlowControlBorSettings(true, apply_requested);
    check(deprecated_apply_error &&
              *deprecated_apply_error ==
                  easiroc::kApplyAtBorDeprecatedError,
          "enabled frontend did not reject deprecated ApplyAtBOR before "
          "the DAQ path");

    check(!easiroc::validateAsicSlowControlBorSettings(true, defaults),
          "ApplyAtBOR=false blocked the normal DAQ path");

    auto invalid_with_deprecated_apply = apply_requested;
    invalid_with_deprecated_apply.asic[0].dac_code = 1024;
    const auto ordered_validation_error =
        easiroc::validateAsicSlowControlBorSettings(
            true, invalid_with_deprecated_apply);
    check(ordered_validation_error &&
              *ordered_validation_error ==
                  "ASIC1 DiscriminatorDACCode must be in range 0..1023",
          "BOR did not validate settings before the deprecated ApplyAtBOR "
          "check");

    auto disabled_invalid = apply_requested;
    disabled_invalid.asic[0].dac_code = -1;
    disabled_invalid.asic[0].dac_slope = -1;
    disabled_invalid.asic[1].dac_code = 1024;
    disabled_invalid.asic[1].dac_slope = 2;
    disabled_invalid.asic[0].hg_feedback_capacitance = 99;
    disabled_invalid.asic[1].lg_feedback_capacitance = 1600;
    disabled_invalid.asic[0].hg_shaping_time = 24;
    disabled_invalid.asic[1].lg_shaping_time = 200;
    disabled_invalid.asic[0].input_dac[0] = -1;
    disabled_invalid.asic[1].input_dac[31] = 512;
    disabled_invalid.asic[0].channel_enabled[0] = false;
    check(!easiroc::validateAsicSlowControlBorSettings(false,
                                                       disabled_invalid),
          "disabled frontend rejected unused slow-control settings or "
          "deprecated ApplyAtBOR");

    const std::array<std::string_view, 17> expected_paths{{
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
    check(easiroc::kAsicSlowControlRequestedSnapshotPaths == expected_paths,
          "RunSnapshot Requested slow-control path order differs");
    const std::set<std::string_view> actual_paths(
        easiroc::kAsicSlowControlRequestedSnapshotPaths.begin(),
        easiroc::kAsicSlowControlRequestedSnapshotPaths.end());
    check(actual_paths.size() ==
              easiroc::kAsicSlowControlRequestedSnapshotPaths.size(),
          "RunSnapshot Requested slow-control paths are duplicated");
    check(easiroc::kEasirocRunSnapshotSchemaVersion == 8,
          "RunSnapshot schema version is not 8");

    std::cout << "easiroc_run_settings_test: " << tests_run
              << " checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << "easiroc_run_settings_test: FAILED: " << error.what()
              << '\n';
    return 1;
  }
  return 0;
}
