#include "manual_buffer_clear.h"

#include <cstdlib>
#include <iostream>

namespace {
void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}
}  // namespace

int main() {
  daq::BufferClearStatus status;
  auto request = daq::beginBufferClearRequest(status, 7);
  expect(request.handled, "new request was not claimed");
  expect(request.pending.in_progress, "claimed request is not pending");
  status = daq::markBufferClearExecuting(request.pending);
  status = daq::finishBufferClearRequest(status, true, "", 100);
  expect(status.last_handled_request_id == 7,
         "successful request was not acknowledged");
  expect(status.last_successful_request_id == 7,
         "successful request id was not recorded");
  expect(!daq::beginBufferClearRequest(status, 7).handled,
         "duplicate request was replayed");

  auto stale = daq::acknowledgeStaleBufferClearRequest(status, 8, 101);
  expect(stale.last_handled_request_id == 8,
         "stale request was not acknowledged");
  expect(stale.state == daq::BufferClearState::kIndeterminate,
         "stale request state is not Indeterminate");
  expect(!stale.last_attempt_succeeded,
         "stale request was incorrectly reported as successful");

  request = daq::beginBufferClearRequest(stale, 9);
  status = daq::rejectBufferClearRequest(
      request.pending, "Run state is RUNNING", 102);
  expect(status.state == daq::BufferClearState::kRejected,
         "RUNNING rejection did not produce Rejected state");
  expect(status.last_handled_request_id == 9,
         "rejected request was not acknowledged");
  expect(status.last_successful_request_id == 7,
         "rejection changed last successful request id");
  expect(!daq::bufferClearRunStateRejection(
              daq::BufferClearRunState::kRunning, "VME").empty(),
         "RUNNING was not rejected by clear policy");
  expect(daq::bufferClearRunStateRejection(
             daq::BufferClearRunState::kStopped, "VME").empty(),
         "STOPPED was rejected by clear policy");

  std::cout << "manual_buffer_clear_test: 14 checks passed\n";
}
