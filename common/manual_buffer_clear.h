#ifndef MIDAS_ONLINE_COMMON_MANUAL_BUFFER_CLEAR_H
#define MIDAS_ONLINE_COMMON_MANUAL_BUFFER_CLEAR_H

#include <cstdint>
#include <string>

namespace daq {

enum class BufferClearState {
  kIdle,
  kPending,
  kClearing,
  kSucceeded,
  kFailed,
  kRejected,
  kIndeterminate,
};

enum class BufferClearRunState { kStopped, kRunning, kPaused, kUnknown };

struct BufferClearStatus {
  std::uint32_t active_request_id = 0;
  std::uint32_t last_handled_request_id = 0;
  std::uint32_t last_successful_request_id = 0;
  BufferClearState state = BufferClearState::kIdle;
  bool in_progress = false;
  bool last_attempt_succeeded = false;
  std::string last_error;
  std::uint64_t last_clear_unix_time = 0;
};

struct BufferClearTransition {
  bool handled = false;
  BufferClearStatus pending;
};

const char* bufferClearStateName(BufferClearState state);
std::string bufferClearRunStateRejection(BufferClearRunState state,
                                         const char* target);

BufferClearTransition beginBufferClearRequest(
    const BufferClearStatus& current, std::uint32_t request_id);

BufferClearStatus markBufferClearExecuting(const BufferClearStatus& pending);

BufferClearStatus finishBufferClearRequest(const BufferClearStatus& executing,
                                           bool succeeded,
                                           std::string error,
                                           std::uint64_t unix_time);

BufferClearStatus rejectBufferClearRequest(const BufferClearStatus& pending,
                                           std::string error,
                                           std::uint64_t unix_time);

// A command left in ODB while the frontend was absent is acknowledged but is
// never executed. This is the restart/replay safety boundary.
BufferClearStatus acknowledgeStaleBufferClearRequest(
    const BufferClearStatus& current, std::uint32_t request_id,
    std::uint64_t unix_time);

}  // namespace daq

#endif
