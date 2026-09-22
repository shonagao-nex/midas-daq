#include "manual_buffer_clear.h"

#include <utility>

namespace daq {
namespace {

BufferClearStatus terminalStatus(const BufferClearStatus& current,
                                 BufferClearState state, bool succeeded,
                                 std::string error,
                                 std::uint64_t unix_time) {
  BufferClearStatus result = current;
  result.last_handled_request_id = current.active_request_id;
  result.state = state;
  result.in_progress = false;
  result.last_attempt_succeeded = succeeded;
  result.last_error = std::move(error);
  result.last_clear_unix_time = unix_time;
  result.active_request_id = 0;
  if (succeeded)
    result.last_successful_request_id = result.last_handled_request_id;
  return result;
}

}  // namespace

const char* bufferClearStateName(BufferClearState state) {
  switch (state) {
    case BufferClearState::kIdle:
      return "Idle";
    case BufferClearState::kPending:
      return "Pending";
    case BufferClearState::kClearing:
      return "Clearing";
    case BufferClearState::kSucceeded:
      return "Succeeded";
    case BufferClearState::kFailed:
      return "Failed";
    case BufferClearState::kRejected:
      return "Rejected";
    case BufferClearState::kIndeterminate:
      return "Indeterminate";
  }
  return "Indeterminate";
}

std::string bufferClearRunStateRejection(BufferClearRunState state,
                                         const char* target) {
  if (state == BufferClearRunState::kStopped) return {};
  const char* name = "unknown";
  if (state == BufferClearRunState::kRunning) name = "RUNNING";
  if (state == BufferClearRunState::kPaused) name = "PAUSED";
  return std::string("Manual ") + target +
         " buffer clear requires STOPPED; current state is " + name;
}

BufferClearTransition beginBufferClearRequest(
    const BufferClearStatus& current, std::uint32_t request_id) {
  BufferClearTransition transition;
  transition.pending = current;
  if (request_id <= current.last_handled_request_id) return transition;
  transition.handled = true;
  transition.pending.active_request_id = request_id;
  transition.pending.state = BufferClearState::kPending;
  transition.pending.in_progress = true;
  transition.pending.last_attempt_succeeded = false;
  transition.pending.last_error.clear();
  return transition;
}

BufferClearStatus markBufferClearExecuting(const BufferClearStatus& pending) {
  BufferClearStatus result = pending;
  result.state = BufferClearState::kClearing;
  return result;
}

BufferClearStatus finishBufferClearRequest(const BufferClearStatus& executing,
                                           bool succeeded,
                                           std::string error,
                                           std::uint64_t unix_time) {
  return terminalStatus(executing,
                        succeeded ? BufferClearState::kSucceeded
                                  : BufferClearState::kFailed,
                        succeeded, std::move(error), unix_time);
}

BufferClearStatus rejectBufferClearRequest(const BufferClearStatus& pending,
                                           std::string error,
                                           std::uint64_t unix_time) {
  return terminalStatus(pending, BufferClearState::kRejected, false,
                        std::move(error), unix_time);
}

BufferClearStatus acknowledgeStaleBufferClearRequest(
    const BufferClearStatus& current, std::uint32_t request_id,
    std::uint64_t unix_time) {
  if (request_id <= current.last_handled_request_id) return current;
  BufferClearStatus result = current;
  result.active_request_id = 0;
  result.last_handled_request_id = request_id;
  result.state = BufferClearState::kIndeterminate;
  result.in_progress = false;
  result.last_attempt_succeeded = false;
  result.last_error =
      "Stale manual buffer-clear request found at frontend startup; request "
      "was acknowledged without execution";
  result.last_clear_unix_time = unix_time;
  return result;
}

}  // namespace daq
