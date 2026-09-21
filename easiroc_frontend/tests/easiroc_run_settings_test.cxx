#include "easiroc_run_settings.h"

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
    }

    for (const int dac_code : {0, 600, 1023}) {
      for (const int dac_slope : {0, 1}) {
        auto settings = defaults;
        settings.asic[0] = {dac_code, dac_slope};
        settings.asic[1] = {dac_code, dac_slope};
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

    auto apply_requested = defaults;
    apply_requested.apply_at_bor = true;
    check(!easiroc::validateAsicSlowControlBorSettings(true,
                                                       apply_requested),
          "enabled frontend rejected valid ApplyAtBOR settings");

    auto disabled_invalid = apply_requested;
    disabled_invalid.asic[0] = {-1, -1};
    disabled_invalid.asic[1] = {1024, 2};
    check(!easiroc::validateAsicSlowControlBorSettings(false,
                                                       disabled_invalid),
          "disabled frontend rejected unused slow-control settings");

    const std::array<std::string_view, 5> expected_paths{{
        "ASICSlowControl/ApplyAtBOR",
        "ASIC1/DiscriminatorDACCode",
        "ASIC1/DiscriminatorDACSlope",
        "ASIC2/DiscriminatorDACCode",
        "ASIC2/DiscriminatorDACSlope",
    }};
    check(easiroc::kAsicSlowControlRequestedSnapshotPaths == expected_paths,
          "RunSnapshot Requested slow-control path order differs");
    const std::set<std::string_view> actual_paths(
        easiroc::kAsicSlowControlRequestedSnapshotPaths.begin(),
        easiroc::kAsicSlowControlRequestedSnapshotPaths.end());
    check(actual_paths.size() ==
              easiroc::kAsicSlowControlRequestedSnapshotPaths.size(),
          "RunSnapshot Requested slow-control paths are duplicated");

    std::cout << "easiroc_run_settings_test: " << tests_run
              << " checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << "easiroc_run_settings_test: FAILED: " << error.what()
              << '\n';
    return 1;
  }
  return 0;
}
