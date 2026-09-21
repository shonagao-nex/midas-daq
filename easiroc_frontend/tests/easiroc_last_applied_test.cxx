#include "easiroc_last_applied.h"
#include "easiroc_manual_apply.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_checks = 0;

void expect(bool condition, const char* message) {
  ++g_checks;
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

easiroc::AsicSlowControlSettings desiredSettings() {
  return {};
}

easiroc::AppliedAsicSlowControlSettings appliedFrom(
    const easiroc::AsicSlowControlSettings& settings) {
  easiroc::AppliedAsicSlowControlSettings applied;
  applied.valid = true;
  applied.request_id = 42;
  applied.apply_unix_time = 123456;
  applied.asic = settings.asic;
  return applied;
}

void expectComparison(const easiroc::HardwareConfigurationComparison& result,
                      easiroc::HardwareConfigurationStatus status,
                      const std::string& detail, const char* message) {
  expect(result.status == status, message);
  expect(result.detail == detail, message);
}

bool sameAsic(const easiroc::AsicDiscriminatorSettings& lhs,
              const easiroc::AsicDiscriminatorSettings& rhs) {
  return lhs.dac_code == rhs.dac_code && lhs.dac_slope == rhs.dac_slope &&
         lhs.hg_feedback_capacitance == rhs.hg_feedback_capacitance &&
         lhs.lg_feedback_capacitance == rhs.lg_feedback_capacitance &&
         lhs.hg_shaping_time == rhs.hg_shaping_time &&
         lhs.lg_shaping_time == rhs.lg_shaping_time &&
         lhs.input_dac == rhs.input_dac &&
         lhs.channel_enabled == rhs.channel_enabled;
}

bool sameAsics(const std::array<easiroc::AsicDiscriminatorSettings, 2>& lhs,
               const std::array<easiroc::AsicDiscriminatorSettings, 2>& rhs) {
  return sameAsic(lhs[0], rhs[0]) && sameAsic(lhs[1], rhs[1]);
}

}  // namespace

int main() {
  auto settings = desiredSettings();
  easiroc::AppliedAsicSlowControlSettings empty;
  auto result = easiroc::compareAsicSlowControlSettings(settings, empty, false);
  expectComparison(result, easiroc::HardwareConfigurationStatus::kUnknown,
                   "No successfully applied ASIC slow-control configuration "
                   "is recorded",
                   "invalid LastApplied is unknown, not a fake match");

  empty.asic = settings.asic;
  result = easiroc::compareAsicSlowControlSettings(settings, empty, false);
  expect(result.status == easiroc::HardwareConfigurationStatus::kUnknown,
         "fake LastApplied values remain unknown while Valid is false");

  auto applied = appliedFrom(settings);
  result = easiroc::compareAsicSlowControlSettings(settings, applied, false);
  expectComparison(result, easiroc::HardwareConfigurationStatus::kMatch, "",
                   "identical ASIC1 and ASIC2 settings match");

  auto changed = settings;
  changed.asic[0].dac_code = 601;
  result = easiroc::compareAsicSlowControlSettings(changed, applied, false);
  expectComparison(result, easiroc::HardwareConfigurationStatus::kMismatch,
                   "ASIC1 DiscriminatorDACCode differs",
                   "ASIC1 threshold mismatch is diagnosed");

  changed = settings;
  changed.asic[0].hg_feedback_capacitance = 200;
  result = easiroc::compareAsicSlowControlSettings(changed, applied, false);
  expectComparison(result, easiroc::HardwareConfigurationStatus::kMismatch,
                   "ASIC1 HGFeedbackCapacitance differs",
                   "ASIC1 HG feedback mismatch is diagnosed");
  changed = settings;
  changed.asic[1].lg_feedback_capacitance = 200;
  result = easiroc::compareAsicSlowControlSettings(changed, applied, false);
  expectComparison(result, easiroc::HardwareConfigurationStatus::kMismatch,
                   "ASIC2 LGFeedbackCapacitance differs",
                   "ASIC2 LG feedback mismatch is diagnosed");

  changed = settings;
  changed.asic[0].hg_shaping_time = 75;
  result = easiroc::compareAsicSlowControlSettings(changed, applied, false);
  expectComparison(result, easiroc::HardwareConfigurationStatus::kMismatch,
                   "ASIC1 HGShapingTime differs",
                   "ASIC1 HG shaping mismatch is diagnosed");
  changed = settings;
  changed.asic[1].lg_shaping_time = 75;
  result = easiroc::compareAsicSlowControlSettings(changed, applied, false);
  expectComparison(result, easiroc::HardwareConfigurationStatus::kMismatch,
                   "ASIC2 LGShapingTime differs",
                   "ASIC2 LG shaping mismatch is diagnosed");

  changed = settings;
  changed.asic[0].channel_enabled[0] = false;
  result = easiroc::compareAsicSlowControlSettings(changed, applied, false);
  expectComparison(result, easiroc::HardwareConfigurationStatus::kMismatch,
                   "ASIC1 ChannelEnabled[0] differs",
                   "ASIC1 ch0 mask mismatch is diagnosed");
  changed = settings;
  changed.asic[1].channel_enabled[17] = false;
  result = easiroc::compareAsicSlowControlSettings(changed, applied, false);
  expectComparison(result, easiroc::HardwareConfigurationStatus::kMismatch,
                   "ASIC2 ChannelEnabled[17] differs",
                   "ASIC2 middle-channel mask mismatch is diagnosed");
  changed = settings;
  changed.asic[1].channel_enabled[31] = false;
  result = easiroc::compareAsicSlowControlSettings(changed, applied, false);
  expectComparison(result, easiroc::HardwareConfigurationStatus::kMismatch,
                   "ASIC2 ChannelEnabled[31] differs",
                   "ASIC2 ch31 mask mismatch is diagnosed");

  for (const std::size_t channel : {std::size_t{0}, std::size_t{17},
                                    std::size_t{31}}) {
    changed = settings;
    ++changed.asic[0].input_dac[channel];
    result = easiroc::compareAsicSlowControlSettings(changed, applied, false);
    expectComparison(result, easiroc::HardwareConfigurationStatus::kMismatch,
                     "ASIC1 InputDAC[" + std::to_string(channel) +
                         "] differs",
                     "ASIC1 InputDAC mismatch is diagnosed");
  }

  changed = settings;
  ++changed.asic[1].input_dac[17];
  result = easiroc::compareAsicSlowControlSettings(changed, applied, false);
  expectComparison(result, easiroc::HardwareConfigurationStatus::kMismatch,
                   "ASIC2 InputDAC[17] differs",
                   "ASIC2 is compared independently");

  const auto applied_before_settings_change = applied;
  ++changed.asic[1].dac_slope;
  expect(sameAsics(applied.asic, applied_before_settings_change.asic),
         "changing Settings cannot mutate LastApplied");

  result = easiroc::compareAsicSlowControlSettings(settings, applied, true);
  expectComparison(result, easiroc::HardwareConfigurationStatus::kIndeterminate,
                   "ASIC slow-control hardware state is indeterminate after an "
                   "incomplete apply",
                   "indeterminate hardware state overrides a matching record");

  const auto match_snapshot = easiroc::snapshotAsicSlowControlConsistency(
      settings, applied, false);
  expect(match_snapshot.last_applied_valid &&
             match_snapshot.configuration_match &&
             !match_snapshot.hardware_state_indeterminate &&
             match_snapshot.last_applied_request_id == 42 &&
             match_snapshot.last_applied_unix_time == 123456 &&
             match_snapshot.status == easiroc::HardwareConfigurationStatus::kMatch &&
             match_snapshot.detail.empty(),
         "BOR consistency snapshot records a complete match");
  expect(!easiroc::shouldWarnForAsicSlowControlConsistency(true,
                                                            match_snapshot),
         "matching enabled BOR does not warn");

  const auto mismatch_snapshot = easiroc::snapshotAsicSlowControlConsistency(
      changed, applied, false);
  expect(!mismatch_snapshot.configuration_match &&
             mismatch_snapshot.status ==
                 easiroc::HardwareConfigurationStatus::kMismatch &&
             mismatch_snapshot.detail == "ASIC2 DiscriminatorDACSlope differs",
         "BOR consistency snapshot records threshold mismatch");
  expect(easiroc::shouldWarnForAsicSlowControlConsistency(true,
                                                           mismatch_snapshot),
         "mismatching enabled BOR warns");
  expect(!easiroc::shouldWarnForAsicSlowControlConsistency(false,
                                                            mismatch_snapshot),
         "disabled BOR does not warn for mismatch");

  const auto unknown_snapshot = easiroc::snapshotAsicSlowControlConsistency(
      settings, empty, false);
  expect(!unknown_snapshot.configuration_match &&
             unknown_snapshot.status ==
                 easiroc::HardwareConfigurationStatus::kUnknown,
         "invalid LastApplied produces BOR Unknown without rejecting it");
  expect(easiroc::shouldWarnForAsicSlowControlConsistency(true,
                                                           unknown_snapshot),
         "enabled BOR warns for unknown LastApplied");

  const auto indeterminate_snapshot =
      easiroc::snapshotAsicSlowControlConsistency(settings, applied, true);
  expect(!indeterminate_snapshot.configuration_match &&
             indeterminate_snapshot.status ==
                 easiroc::HardwareConfigurationStatus::kIndeterminate,
         "indeterminate BOR snapshot cannot be a match");
  expect(easiroc::shouldWarnForAsicSlowControlConsistency(
             true, indeterminate_snapshot),
         "enabled BOR warns for indeterminate hardware state");

  const auto immutable_snapshot = match_snapshot;
  auto live_settings = settings;
  auto live_applied = applied;
  ++live_settings.asic[0].input_dac[0];
  ++live_applied.asic[1].input_dac[31];
  const auto post_change_snapshot =
      easiroc::snapshotAsicSlowControlConsistency(live_settings, live_applied,
                                                  false);
  expect(post_change_snapshot.status ==
             easiroc::HardwareConfigurationStatus::kMismatch,
         "changed live snapshots are independently re-evaluated");
  expect(immutable_snapshot.configuration_match &&
             immutable_snapshot.status ==
                 easiroc::HardwareConfigurationStatus::kMatch &&
             immutable_snapshot.last_applied_request_id == 42,
         "BOR consistency snapshot remains immutable after live values change");

  easiroc::ManualApplyStatus mailbox;
  const easiroc::ManualApplyRequestContext context{
      easiroc::ManualApplyRunState::kStopped, false, false};
  const auto transition = easiroc::beginManualApplyRequest(mailbox, 1);
  int hardware_calls = 0;
  const auto rejected = easiroc::executeManualApplyBackend(
      transition.pending, context, settings, applied, false, 999, {},
      [&](std::uint32_t, const std::vector<std::uint8_t>&) {
        ++hardware_calls;
      },
      [&](unsigned) { ++hardware_calls; });
  expect(transition.handled &&
             rejected.terminal.last_handled_request_id == 1 &&
             rejected.terminal.last_successful_request_id == 0,
         "rejected request is acknowledged but not successful");
  expect(hardware_calls == 0,
         "rejected request reached the hardware callbacks");
  expect(applied.valid && applied.request_id == 42 &&
             applied.apply_unix_time == 123456 &&
             sameAsics(applied.asic, applied_before_settings_change.asic),
         "backend result cannot mutate the caller's LastApplied snapshot");

  std::cout << "easiroc_last_applied_test: " << g_checks << " checks passed\n";
  return 0;
}
