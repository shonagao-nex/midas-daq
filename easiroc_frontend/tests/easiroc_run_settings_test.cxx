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
      check(asic.input_dac.size() == 32,
            "InputDAC default does not contain 32 channels");
      check(std::all_of(asic.input_dac.begin(), asic.input_dac.end(),
                        [](int value) { return value == 350; }),
            "InputDAC default is not 350 for every channel");
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
    disabled_invalid.asic[0].input_dac[0] = -1;
    disabled_invalid.asic[1].input_dac[31] = 512;
    check(!easiroc::validateAsicSlowControlBorSettings(false,
                                                       disabled_invalid),
          "disabled frontend rejected unused slow-control settings or "
          "deprecated ApplyAtBOR");

    const std::array<std::string_view, 7> expected_paths{{
        "ASICSlowControl/ApplyAtBOR",
        "ASIC1/DiscriminatorDACCode",
        "ASIC1/DiscriminatorDACSlope",
        "ASIC2/DiscriminatorDACCode",
        "ASIC2/DiscriminatorDACSlope",
        "ASIC1/InputDAC",
        "ASIC2/InputDAC",
    }};
    check(easiroc::kAsicSlowControlRequestedSnapshotPaths == expected_paths,
          "RunSnapshot Requested slow-control path order differs");
    const std::set<std::string_view> actual_paths(
        easiroc::kAsicSlowControlRequestedSnapshotPaths.begin(),
        easiroc::kAsicSlowControlRequestedSnapshotPaths.end());
    check(actual_paths.size() ==
              easiroc::kAsicSlowControlRequestedSnapshotPaths.size(),
          "RunSnapshot Requested slow-control paths are duplicated");
    check(easiroc::kEasirocRunSnapshotSchemaVersion == 5,
          "RunSnapshot schema version is not 5");

    std::cout << "easiroc_run_settings_test: " << tests_run
              << " checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << "easiroc_run_settings_test: FAILED: " << error.what()
              << '\n';
    return 1;
  }
  return 0;
}
