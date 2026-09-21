#include "easiroc_manual_apply.h"

#include <array>
#include <exception>

namespace easiroc {
namespace {

std::string rejectionReason(const ManualApplyRequestContext& context) {
  if (context.another_apply_in_progress)
    return "Another ASIC slow-control apply is already in progress";
  switch (context.run_state) {
    case ManualApplyRunState::kRunning:
      return "Manual ASIC slow-control apply requires Run state STOPPED; "
             "current state is RUNNING";
    case ManualApplyRunState::kPaused:
      return "Manual ASIC slow-control apply requires Run state STOPPED; "
             "current state is PAUSED";
    case ManualApplyRunState::kUnknown:
      return "Manual ASIC slow-control apply requires Run state STOPPED; "
             "current state is unknown";
    case ManualApplyRunState::kStopped:
      break;
  }
  if (!context.frontend_enabled)
    return "Manual ASIC slow-control apply requires EASIROC Enabled=TRUE";
  return {};
}

ManualApplyStatus terminalStatus(const ManualApplyStatus& pending,
                                 ManualApplyState state,
                                 std::uint64_t unix_time,
                                 std::string error) {
  ManualApplyStatus result = pending;
  result.last_handled_request_id = pending.active_request_id;
  result.state = state;
  result.last_attempt_succeeded = state == ManualApplyState::kSucceeded;
  result.last_apply_error = std::move(error);
  result.last_apply_unix_time = unix_time;
  result.active_request_id = 0;
  result.apply_in_progress = false;
  if (result.last_attempt_succeeded)
    result.last_successful_request_id = result.last_handled_request_id;
  return result;
}

std::array<std::uint16_t, kInputDacChannelCount> inputDacValues(
    const AsicDiscriminatorSettings& settings) {
  std::array<std::uint16_t, kInputDacChannelCount> result{};
  for (std::size_t channel = 0; channel < result.size(); ++channel)
    result[channel] = static_cast<std::uint16_t>(settings.input_dac[channel]);
  return result;
}

}  // namespace

const char* manualApplyStateName(ManualApplyState state) {
  switch (state) {
    case ManualApplyState::kIdle:
      return "Idle";
    case ManualApplyState::kPending:
      return "Pending";
    case ManualApplyState::kApplying:
      return "Applying";
    case ManualApplyState::kSucceeded:
      return "Succeeded";
    case ManualApplyState::kFailed:
      return "Failed";
    case ManualApplyState::kRejected:
      return "Rejected";
    case ManualApplyState::kIndeterminate:
      return "Indeterminate";
  }
  return "Indeterminate";
}

ManualApplyTransition beginManualApplyRequest(
    const ManualApplyStatus& current, std::uint32_t request_id) {
  ManualApplyTransition transition;
  transition.pending = current;
  transition.terminal = current;
  if (request_id <= current.last_handled_request_id) return transition;

  transition.handled = true;
  transition.pending.active_request_id = request_id;
  transition.pending.state = ManualApplyState::kPending;
  transition.pending.apply_in_progress = true;
  transition.pending.last_attempt_succeeded = false;
  transition.pending.last_apply_error.clear();

  transition.terminal = transition.pending;
  return transition;
}

ManualApplyStatus rejectManualApplyRequest(
    const ManualApplyStatus& pending, std::string error,
    std::uint64_t unix_time) {
  return terminalStatus(pending, ManualApplyState::kRejected, unix_time,
                        std::move(error));
}

ManualApplyBackendResult executeManualApplyBackend(
    const ManualApplyStatus& pending,
    const ManualApplyRequestContext& context,
    const AsicSlowControlSettings& settings_snapshot,
    const AppliedAsicSlowControlSettings& previous_last_applied,
    bool previous_hardware_state_indeterminate, std::uint64_t unix_time,
    const ManualApplyStatusPublisher& publish_status,
    const SlowControlWrite& write, const SlowControlDelay& delay) {
  ManualApplyBackendResult result;
  const AsicSlowControlSettings frozen_settings = settings_snapshot;
  result.last_applied = previous_last_applied;
  result.hardware_state_indeterminate =
      previous_hardware_state_indeterminate;

  if (const std::string rejection = rejectionReason(context);
      !rejection.empty()) {
    result.terminal =
        rejectManualApplyRequest(pending, rejection, unix_time);
    return result;
  }
  if (const auto validation_error =
          validateAsicSlowControlSettings(frozen_settings)) {
    result.terminal =
        rejectManualApplyRequest(pending, *validation_error, unix_time);
    return result;
  }

  ManualApplyStatus applying = pending;
  applying.state = ManualApplyState::kApplying;
  if (publish_status) publish_status(applying);

  try {
    AsicSlowControlImages images;
    for (std::size_t asic = 0; asic < images.size(); ++asic) {
      const auto& settings = frozen_settings.asic[asic];
      images[asic] = SlowControlPolicy::encodeLegacySiteAsicOverlay(
          static_cast<std::uint16_t>(settings.dac_code),
          static_cast<std::uint16_t>(settings.dac_slope),
          inputDacValues(settings));
    }
    static_assert(SlowControlEncoder::Image{}.size() == 57,
                  "ASIC slow-control image must be 57 bytes");
    result.apply_result = executeAsicApplyPlan(
        SlowControlPolicy::buildAsicApplyPlan(images), write, delay);
  } catch (const std::exception& error) {
    result.terminal = terminalStatus(
        applying, ManualApplyState::kFailed, unix_time,
        std::string("ASIC slow-control preparation failed: ") + error.what());
    return result;
  } catch (...) {
    result.terminal = terminalStatus(
        applying, ManualApplyState::kFailed, unix_time,
        "ASIC slow-control preparation failed: unknown exception");
    return result;
  }

  if (!result.apply_result.sequence_succeeded) {
    result.hardware_state_indeterminate = true;
    result.terminal = terminalStatus(
        applying, ManualApplyState::kFailed, unix_time,
        result.apply_result.error);
    return result;
  }

  result.last_applied.valid = true;
  result.last_applied.request_id = pending.active_request_id;
  result.last_applied.apply_unix_time = unix_time;
  result.last_applied.asic = frozen_settings.asic;
  result.last_applied_changed = true;
  result.hardware_state_indeterminate = false;
  result.terminal =
      terminalStatus(applying, ManualApplyState::kSucceeded, unix_time, "");
  return result;
}

ManualApplyStatus acknowledgeStaleManualApplyRequest(
    const ManualApplyStatus& current, std::uint32_t request_id,
    std::uint64_t unix_time) {
  if (request_id <= current.last_handled_request_id) return current;
  ManualApplyStatus result = current;
  result.active_request_id = 0;
  result.last_handled_request_id = request_id;
  result.state = ManualApplyState::kIndeterminate;
  result.apply_in_progress = false;
  result.last_attempt_succeeded = false;
  result.last_apply_error =
      "Stale manual ASIC slow-control request found at frontend startup; "
      "request was not executed";
  result.last_apply_unix_time = unix_time;
  return result;
}

}  // namespace easiroc
