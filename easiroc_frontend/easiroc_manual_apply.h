#ifndef EASIROC_FRONTEND_EASIROC_MANUAL_APPLY_H
#define EASIROC_FRONTEND_EASIROC_MANUAL_APPLY_H

#include "easiroc_asic_apply.h"
#include "easiroc_last_applied.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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
  bool rollback_attempted = false;
  bool rollback_succeeded = false;
  std::string rollback_error;
};

struct ManualApplyRequestContext {
  ManualApplyRunState run_state = ManualApplyRunState::kUnknown;
  bool frontend_enabled = false;
  bool another_apply_in_progress = false;
  bool transition_clear = false;  // unknown is unsafe

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

// Failed/unavailable rollback inhibits START until a successful re-apply.
bool slowControlPermitsDaqStart(bool hardware_state_indeterminate,
                               bool apply_in_progress);

// Claims one monotonic request and produces the state that must be published
// before safety checks or hardware preparation begin.
ManualApplyTransition beginManualApplyRequest(
    const ManualApplyStatus& current, std::uint32_t request_id);

ManualApplyStatus rejectManualApplyRequest(
    const ManualApplyStatus& pending, std::string error,
    std::uint64_t unix_time);

// Applies one immutable Settings snapshot. The only hardware operations are
// injected writer and delay callbacks (legacy ASIC-only or complete SC plan).
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

struct SlowControlDevice {
  std::string id;
  std::string ip_address;
  AsicSlowControlSettings settings;
  AppliedAsicSlowControlSettings last_applied;
  ManualApplyStatus status;
  bool hardware_state_indeterminate = false;
};

// IDs are ODB-safe names, IPs are canonical dotted decimal (no aliases).
bool isSlowControlDeviceId(const std::string& id);
std::optional<std::string> validateSlowControlDevices(
    const std::vector<SlowControlDevice>& devices);

// Pure migration proposal: does not create/modify any ODB keys. The original
// legacy DAQ subtree remains the acquisition owner after migration.
SlowControlDevice proposeLegacySlowControlMigration(
    const std::string& id, const std::string& ip,
    const AsicSlowControlSettings& settings,
    const AppliedAsicSlowControlSettings& last_applied);

}  // namespace easiroc

#endif
