#ifndef EASIROC_FRONTEND_EASIROC_MANUAL_APPLY_H
#define EASIROC_FRONTEND_EASIROC_MANUAL_APPLY_H

#include "easiroc_asic_apply.h"
#include "easiroc_last_applied.h"

#include <cstdint>
#include <functional>
#include <string>

namespace easiroc {

enum class ManualApplyState {
  kIdle,
  kPending,
  kApplying,
  kSucceeded,
  kFailed,
  kRejected,
  kIndeterminate,
};

enum class ManualApplyRunState {
  kStopped,
  kRunning,
  kPaused,
  kUnknown,
};

struct ManualApplyStatus {
  std::uint32_t active_request_id = 0;
  std::uint32_t last_handled_request_id = 0;
  std::uint32_t last_successful_request_id = 0;
  ManualApplyState state = ManualApplyState::kIdle;
  bool apply_in_progress = false;
  bool last_attempt_succeeded = false;
  std::string last_apply_error;
  std::uint64_t last_apply_unix_time = 0;
};

struct ManualApplyRequestContext {
  ManualApplyRunState run_state = ManualApplyRunState::kUnknown;
  bool frontend_enabled = false;
  bool another_apply_in_progress = false;
};

struct ManualApplyTransition {
  bool handled = false;
  ManualApplyStatus pending;
  ManualApplyStatus terminal;
};

using ManualApplyStatusPublisher =
    std::function<void(const ManualApplyStatus&)>;

struct ManualApplyBackendResult {
  ManualApplyStatus terminal;
  AppliedAsicSlowControlSettings last_applied;
  bool last_applied_changed = false;
  bool hardware_state_indeterminate = false;
  AsicApplyResult apply_result;
};

const char* manualApplyStateName(ManualApplyState state);

// Claims one monotonic request and produces the state that must be published
// before safety checks or hardware preparation begin.
ManualApplyTransition beginManualApplyRequest(
    const ManualApplyStatus& current, std::uint32_t request_id);

ManualApplyStatus rejectManualApplyRequest(
    const ManualApplyStatus& pending, std::string error,
    std::uint64_t unix_time);

// Applies one immutable Settings snapshot. The only hardware operations are
// the injected ASIC-only seven-transaction writer and delay callbacks.
ManualApplyBackendResult executeManualApplyBackend(
    const ManualApplyStatus& pending,
    const ManualApplyRequestContext& context,
    const AsicSlowControlSettings& settings_snapshot,
    const AppliedAsicSlowControlSettings& previous_last_applied,
    bool previous_hardware_state_indeterminate, std::uint64_t unix_time,
    const ManualApplyStatusPublisher& publish_status,
    const SlowControlWrite& write, const SlowControlDelay& delay);

// A request already present when the frontend starts is acknowledged without
// execution so restarting the frontend can never replay a stale command.
ManualApplyStatus acknowledgeStaleManualApplyRequest(
    const ManualApplyStatus& current, std::uint32_t request_id,
    std::uint64_t unix_time);

}  // namespace easiroc

#endif
