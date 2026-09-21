#include "easiroc_manual_apply.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int tests_run = 0;

void check(bool condition, const std::string& message) {
  ++tests_run;
  if (!condition) throw std::runtime_error(message);
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

bool sameApplied(const easiroc::AppliedAsicSlowControlSettings& lhs,
                 const easiroc::AppliedAsicSlowControlSettings& rhs) {
  return lhs.valid == rhs.valid && lhs.request_id == rhs.request_id &&
         lhs.apply_unix_time == rhs.apply_unix_time &&
         sameAsic(lhs.asic[0], rhs.asic[0]) &&
         sameAsic(lhs.asic[1], rhs.asic[1]);
}

std::array<std::uint16_t, easiroc::kInputDacChannelCount> inputValues(
    const easiroc::AsicDiscriminatorSettings& settings) {
  std::array<std::uint16_t, easiroc::kInputDacChannelCount> result{};
  for (std::size_t channel = 0; channel < result.size(); ++channel)
    result[channel] = static_cast<std::uint16_t>(settings.input_dac[channel]);
  return result;
}

easiroc::ManualApplyBackendResult execute(
    const easiroc::ManualApplyStatus& pending,
    const easiroc::ManualApplyRequestContext& context,
    const easiroc::AsicSlowControlSettings& settings,
    const easiroc::AppliedAsicSlowControlSettings& previous,
    bool previous_indeterminate, std::uint64_t unix_time, int* writes,
    int* delays) {
  return easiroc::executeManualApplyBackend(
      pending, context, settings, previous, previous_indeterminate, unix_time,
      {},
      [&](std::uint32_t, const std::vector<std::uint8_t>&) { ++*writes; },
      [&](unsigned) { ++*delays; });
}

}  // namespace

int main() {
  try {
    easiroc::ManualApplyStatus initial;
    check(initial.active_request_id == 0 &&
              initial.last_handled_request_id == 0 &&
              initial.last_successful_request_id == 0 &&
              initial.state == easiroc::ManualApplyState::kIdle &&
              !initial.apply_in_progress &&
              !initial.last_attempt_succeeded,
          "manual apply initial state is not safe/idle");

    const auto request1 = easiroc::beginManualApplyRequest(initial, 1);
    check(request1.handled && request1.pending.active_request_id == 1 &&
              request1.pending.apply_in_progress &&
              request1.pending.state == easiroc::ManualApplyState::kPending,
          "request 1 did not enter Pending");

    auto settings = easiroc::AsicSlowControlSettings{};
    settings.asic[0].dac_code = 601;
    settings.asic[0].input_dac[0] = 351;
    settings.asic[0].input_dac[17] = 349;
    settings.asic[0].hg_feedback_capacitance = 200;
    settings.asic[0].hg_shaping_time = 75;
    settings.asic[0].lg_shaping_time = 125;
    settings.asic[0].channel_enabled[0] = false;
    settings.asic[0].channel_enabled[16] = false;
    settings.asic[1].dac_code = 602;
    settings.asic[1].input_dac[16] = 352;
    settings.asic[1].input_dac[31] = 348;
    settings.asic[1].lg_feedback_capacitance = 1500;
    settings.asic[1].hg_shaping_time = 25;
    settings.asic[1].lg_shaping_time = 175;
    settings.asic[1].channel_enabled[31] = false;
    const auto frozen_settings = settings;
    const auto expected_asic1 =
        easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
            frozen_settings.asic[0].dac_code,
            frozen_settings.asic[0].dac_slope,
            inputValues(frozen_settings.asic[0]),
            frozen_settings.asic[0].hg_feedback_capacitance,
            frozen_settings.asic[0].lg_feedback_capacitance,
            frozen_settings.asic[0].hg_shaping_time,
            frozen_settings.asic[0].lg_shaping_time,
            frozen_settings.asic[0].channel_enabled);
    const auto expected_asic2 =
        easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
            frozen_settings.asic[1].dac_code,
            frozen_settings.asic[1].dac_slope,
            inputValues(frozen_settings.asic[1]),
            frozen_settings.asic[1].hg_feedback_capacitance,
            frozen_settings.asic[1].lg_feedback_capacitance,
            frozen_settings.asic[1].hg_shaping_time,
            frozen_settings.asic[1].lg_shaping_time,
            frozen_settings.asic[1].channel_enabled);

    std::vector<std::uint32_t> addresses;
    std::vector<std::vector<std::uint8_t>> payloads;
    std::vector<easiroc::ManualApplyState> published_states;
    const easiroc::ManualApplyRequestContext stopped_enabled{
        easiroc::ManualApplyRunState::kStopped, true, false};
    const auto succeeded = easiroc::executeManualApplyBackend(
        request1.pending, stopped_enabled, settings, {}, false, 1001,
        [&](const easiroc::ManualApplyStatus& status) {
          published_states.push_back(status.state);
          settings.asic[0].input_dac[0] = 400;
          settings.asic[0].hg_shaping_time = 150;
          settings.asic[0].channel_enabled[0] = true;
          settings.asic[1].dac_code = 700;
          settings.asic[1].lg_shaping_time = 25;
          settings.asic[1].channel_enabled[31] = true;
        },
        [&](std::uint32_t address, const std::vector<std::uint8_t>& data) {
          addresses.push_back(address);
          payloads.push_back(data);
        },
        [&](unsigned milliseconds) {
          check(milliseconds == 100, "manual apply delay is not 100 ms");
        });
    check(published_states ==
              std::vector<easiroc::ManualApplyState>{
                  easiroc::ManualApplyState::kApplying},
          "Applying was not published exactly once before execution");
    check(succeeded.apply_result.sequence_succeeded &&
              succeeded.apply_result.completed_transactions == 7 &&
              succeeded.terminal.state ==
                  easiroc::ManualApplyState::kSucceeded &&
              succeeded.terminal.last_handled_request_id == 1 &&
              succeeded.terminal.last_successful_request_id == 1 &&
              succeeded.terminal.last_attempt_succeeded &&
              succeeded.terminal.last_apply_error.empty() &&
              !succeeded.terminal.apply_in_progress &&
              succeeded.terminal.active_request_id == 0 &&
              succeeded.last_applied_changed &&
              !succeeded.hardware_state_indeterminate,
          "successful request terminal state differs");
    check(succeeded.last_applied.valid &&
              succeeded.last_applied.request_id == 1 &&
              succeeded.last_applied.apply_unix_time == 1001 &&
              sameAsic(succeeded.last_applied.asic[0],
                       frozen_settings.asic[0]) &&
              sameAsic(succeeded.last_applied.asic[1],
                       frozen_settings.asic[1]),
          "LastApplied was not populated from the request snapshot");
    check(easiroc::compareAsicSlowControlSettings(
              frozen_settings, succeeded.last_applied,
              succeeded.hardware_state_indeterminate)
              .status == easiroc::HardwareConfigurationStatus::kMatch,
          "successful apply does not produce ConfigurationStatus Match");
    check(addresses.size() == 6 && payloads.size() == 6 &&
              addresses[1] == 0x00000003 && payloads[1].size() == 57 &&
              addresses[2] == 0x0000003d && payloads[2].size() == 57,
          "manual backend did not execute the ASIC-only plan");
    check(std::equal(expected_asic1.begin(), expected_asic1.end(),
                     payloads[1].begin()) &&
              std::equal(expected_asic2.begin(), expected_asic2.end(),
                         payloads[2].begin()),
          "threshold/InputDAC/feedback/shaping/mask values did not propagate "
          "to both ASIC images");

    const auto duplicate =
        easiroc::beginManualApplyRequest(succeeded.terminal, 1);
    check(!duplicate.handled,
          "the same request ID was accepted more than once");

    const easiroc::AppliedAsicSlowControlSettings previous =
        succeeded.last_applied;
    const auto request2 =
        easiroc::beginManualApplyRequest(succeeded.terminal, 2);
    int failed_writes = 0;
    int failed_delays = 0;
    const auto failed = easiroc::executeManualApplyBackend(
        request2.pending, stopped_enabled, frozen_settings, previous, false,
        1002, {},
        [&](std::uint32_t, const std::vector<std::uint8_t>&) {
          if (++failed_writes == 3)
            throw std::runtime_error("mock RBCP failure");
        },
        [&](unsigned) { ++failed_delays; });
    check(failed_writes == 3 && failed_delays == 0 &&
              failed.apply_result.completed_transactions == 2,
          "executor did not stop at the failed write");
    check(failed.terminal.state == easiroc::ManualApplyState::kFailed &&
              failed.terminal.last_handled_request_id == 2 &&
              failed.terminal.last_successful_request_id == 1 &&
              !failed.terminal.last_attempt_succeeded &&
              failed.terminal.last_apply_error.find("mock RBCP failure") !=
                  std::string::npos &&
              failed.terminal.last_apply_error.find(
                  "hardware state may be indeterminate") !=
                  std::string::npos &&
              failed.hardware_state_indeterminate &&
              !failed.last_applied_changed &&
              sameApplied(failed.last_applied, previous),
          "partial failure did not preserve LastApplied or mark Indeterminate");
    check(easiroc::compareAsicSlowControlSettings(
              frozen_settings, failed.last_applied,
              failed.hardware_state_indeterminate)
              .status ==
              easiroc::HardwareConfigurationStatus::kIndeterminate,
          "partial failure does not produce ConfigurationStatus Indeterminate");

    struct RejectionCase {
      const char* name;
      easiroc::ManualApplyRequestContext context;
      easiroc::AsicSlowControlSettings settings;
      const char* expected_error;
    };
    auto invalid_dac = frozen_settings;
    invalid_dac.asic[0].dac_code = 1024;
    auto invalid_slope = frozen_settings;
    invalid_slope.asic[0].dac_slope = 2;
    auto invalid_input = frozen_settings;
    invalid_input.asic[1].input_dac[31] = 512;
    auto invalid_shaping = frozen_settings;
    invalid_shaping.asic[0].hg_shaping_time = 60;
    const std::array rejection_cases{
        RejectionCase{"RUNNING",
                      {easiroc::ManualApplyRunState::kRunning, true, false},
                      frozen_settings, "RUNNING"},
        RejectionCase{"PAUSED",
                      {easiroc::ManualApplyRunState::kPaused, true, false},
                      frozen_settings, "PAUSED"},
        RejectionCase{"disabled",
                      {easiroc::ManualApplyRunState::kStopped, false, false},
                      frozen_settings, "Enabled=TRUE"},
        RejectionCase{"invalid DAC", stopped_enabled, invalid_dac,
                      "range 0..1023"},
        RejectionCase{"invalid slope", stopped_enabled, invalid_slope,
                      "coarse"},
        RejectionCase{"invalid InputDAC", stopped_enabled, invalid_input,
                      "InputDAC[31]"},
        RejectionCase{"invalid shaping", stopped_enabled, invalid_shaping,
                      "HGShapingTime=60"},
        RejectionCase{"busy",
                      {easiroc::ManualApplyRunState::kStopped, true, true},
                      frozen_settings, "already in progress"},
    };
    std::uint32_t rejection_id = 10;
    for (const auto& rejection : rejection_cases) {
      const auto begun = easiroc::beginManualApplyRequest(
          succeeded.terminal, rejection_id++);
      int writes = 0;
      int delays = 0;
      const auto rejected = execute(
          begun.pending, rejection.context, rejection.settings, previous,
          false, 2000 + rejection_id, &writes, &delays);
      check(rejected.terminal.state ==
                    easiroc::ManualApplyState::kRejected &&
                rejected.terminal.last_handled_request_id == rejection_id - 1 &&
                rejected.terminal.last_successful_request_id == 1 &&
                rejected.terminal.last_apply_error.find(
                    rejection.expected_error) != std::string::npos &&
                writes == 0 && delays == 0 &&
                !rejected.hardware_state_indeterminate &&
                !rejected.last_applied_changed &&
                sameApplied(rejected.last_applied, previous),
            std::string(rejection.name) +
                " rejection touched hardware or persistent state");
    }

    int stale_hardware_calls = 0;
    const auto stale = easiroc::acknowledgeStaleManualApplyRequest(
        easiroc::ManualApplyStatus{}, 50, 3001);
    check(stale.state == easiroc::ManualApplyState::kIndeterminate &&
              stale.last_handled_request_id == 50 &&
              stale.last_successful_request_id == 0 &&
              stale.active_request_id == 0 && !stale.apply_in_progress &&
              stale_hardware_calls == 0,
          "startup stale request was not acknowledged without hardware");

    std::cout << "easiroc_manual_apply_test: " << tests_run
              << " checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << "easiroc_manual_apply_test: FAILED: " << error.what()
              << '\n';
    return 1;
  }
  return 0;
}
