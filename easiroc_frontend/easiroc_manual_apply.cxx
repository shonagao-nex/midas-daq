#include "easiroc_manual_apply.h"

#include <array>
#include <exception>
#include <stdexcept>
#include <set>
#include <sstream>
#include <cctype>

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
  if (!context.transition_clear)
    return "Run transition active or unknown; Apply rejected";
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

std::vector<Transaction> preparePlan(const AsicSlowControlSettings& settings) {
  if (const auto error = validateAsicSlowControlSettings(settings))
    throw std::invalid_argument(*error);
  if (settings.apply_at_bor) throw std::invalid_argument(std::string(kApplyAtBorDeprecatedError));
  AsicSlowControlImages images;
  for (unsigned i = 0; i < 2; ++i) {
    const auto& a = settings.asic[i];
    images[i] = SlowControlPolicy::encodeLegacySiteAsicOverlay(
        a.dac_code, a.dac_slope, inputDacValues(a), a.hg_feedback_capacitance,
        a.lg_feedback_capacitance, a.hg_shaping_time, a.lg_shaping_time,
        a.channel_enabled);
  }
  if (!settings.extras) return SlowControlPolicy::buildAsicApplyPlan(images);
  const auto& e = *settings.extras;
  std::array<ProbeSelection, 2> probe;
  for (unsigned i = 0; i < 2; ++i)
    probe[i] = {static_cast<ProbeOutput>(e.probe_signal[i]),
                e.probe_enabled[i] ? e.probe_channel[i] + int(32 * i) : -1};
  SelectableLogicConfig logic;
  logic.pattern = static_cast<SelectableLogicPattern>(e.logic_pattern);
  logic.one_channel = e.one_channel;
  logic.hit_number_threshold = e.hit_num_threshold;
  for (unsigned i = 0; i < 64; ++i)
    if (e.and_channels[i]) logic.and_channels.push_back(i);
  return SlowControlPolicy::buildSlowControlApplyPlan(images, probe, logic,
      e.trigger_width_ns == 0 ? std::nullopt :
      std::optional<std::uint16_t>(e.trigger_width_ns));
}

}  // namespace

bool slowControlPermitsDaqStart(bool hardware_state_indeterminate,
                               bool apply_in_progress) {
  return !hardware_state_indeterminate && !apply_in_progress;
}

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
  transition.pending.rollback_attempted = false;
  transition.pending.rollback_succeeded = false;
  transition.pending.rollback_error.clear();

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
    const auto plan = preparePlan(frozen_settings);
    result.apply_result = executeSlowControlPlan(plan, write, delay);
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
    // At most one retransmission of the previous successful complete snapshot.
    // A legacy ASIC-only record cannot restore a failed extended plan.
    if (previous_last_applied.valid &&
        previous_last_applied.extras.has_value() == frozen_settings.extras.has_value()) {
      try {
        AsicSlowControlSettings rollback;
        rollback.asic = previous_last_applied.asic;
        rollback.extras = previous_last_applied.extras;
        const auto rollback_plan = preparePlan(rollback);
        result.terminal.rollback_attempted = true;
        const auto restored = executeSlowControlPlan(rollback_plan, write, delay);
        result.terminal.rollback_succeeded = restored.sequence_succeeded;
        result.terminal.rollback_error = restored.error;
        result.hardware_state_indeterminate = !restored.sequence_succeeded;
      } catch (const std::exception& error) {
        result.terminal.rollback_error = error.what();
      }
    } else {
      result.terminal.rollback_error = "No compatible valid LastApplied snapshot";
    }
    result.terminal.last_apply_error += result.terminal.rollback_succeeded ?
        "; Rollback succeeded (transmission record, no readback)" :
        "; Rollback unavailable or failed; hardware state indeterminate: " + result.terminal.rollback_error;
    return result;
  }

  result.last_applied.valid = true;
  result.last_applied.request_id = pending.active_request_id;
  result.last_applied.apply_unix_time = unix_time;
  result.last_applied.asic = frozen_settings.asic;
  result.last_applied.extras = frozen_settings.extras;
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

bool isSlowControlDeviceId(const std::string& id) {
  if (id.empty() || id.size() > 31) return false;
  std::string lower;
  for (unsigned char c : id) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    lower += static_cast<char>(std::tolower(c));
  }
  return lower != "legacy";
}

std::optional<std::string> validateSlowControlDevices(
    const std::vector<SlowControlDevice>& devices) {
  std::set<std::string> ids, ips;
  for (const auto& device : devices) {
    if (!isSlowControlDeviceId(device.id)) return "Invalid/reserved device ID";
    std::string normalized_id;
    for (unsigned char c : device.id) normalized_id += static_cast<char>(std::tolower(c));
    if (!ids.insert(normalized_id).second) return "Duplicate device ID";
    std::istringstream input(device.ip_address);
    std::string part, canonical;
    for (int i = 0; i < 4; ++i) {
      if (!std::getline(input, part, '.') || part.empty() || part.size() > 3)
        return "Invalid IPv4 address";
      for (unsigned char c : part)
        if (!std::isdigit(c)) return "Invalid IPv4 address";
      const int value = std::stoi(part);
      if (value > 255) return "Invalid IPv4 address";
      canonical += (i ? "." : "") + std::to_string(value);
    }
    if (canonical != device.ip_address) return "IPv4 address must be canonical";
    if (!ips.insert(canonical).second) return "Duplicate IP address: " + canonical;
  }
  return std::nullopt;
}

SlowControlDevice proposeLegacySlowControlMigration(
    const std::string& id, const std::string& ip,
    const AsicSlowControlSettings& settings,
    const AppliedAsicSlowControlSettings& last_applied) {
  SlowControlDevice result;
  result.id = id;
  result.ip_address = ip;
  result.settings = settings;
  result.settings.extras = SlowControlExtras{};
  // Preserve the record's scope: never claim new fields were transmitted.
  result.last_applied = last_applied;
  return result;
}

}  // namespace easiroc
