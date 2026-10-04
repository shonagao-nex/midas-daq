#include "runlog_index_retry.h"

#include <chrono>
#include <cstdio>
#include <csignal>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

}  // namespace

int main() {
  using daq_monitor::RunlogIndexRetry;
  const auto now = RunlogIndexRetry::Clock::time_point{};
  const auto delay = RunlogIndexRetry::kRetryDelay;

  // A successful child is the only event that completes a run.
  RunlogIndexRetry success;
  check(success.should_start(42, now), "completed run should start");
  success.started(42, 101);
  check(!success.should_start(42, now), "active child must not be duplicated");
  check(!success.should_start(43, now), "next run waits for active child");
  check(success.finished(101, 0, now), "zero exit status should succeed");
  check(success.last_successful_run() == 42, "successful run was not recorded");
  check(!success.should_start(42, now + delay), "successful run was repeated");
  check(success.should_start(43, now), "next run was blocked");

  // Preparation or posix_spawn failure retries after the cooldown.
  RunlogIndexRetry launch_failure;
  launch_failure.failed(50, now);
  check(!launch_failure.should_start(50, now + delay - std::chrono::milliseconds(1)),
        "launch failure retried too soon");
  check(launch_failure.should_start(50, now + delay), "launch failure never retried");
  check(launch_failure.should_start(51, now), "newer run inherited old retry delay");

  // A nonzero or signal exit retries; an unrelated child changes no state.
  RunlogIndexRetry abnormal;
  abnormal.started(60, 201);
  check(!abnormal.finished(202, 0, now), "unrelated child was accepted");
  check(abnormal.active_child() == 201, "unrelated child cleared active state");
  check(!abnormal.finished(201, 1 << 8, now), "nonzero exit was accepted");
  check(abnormal.last_successful_run() == 0, "failed run was marked successful");
  check(!abnormal.should_start(60, now), "failed run retried immediately");
  check(abnormal.should_start(60, now + delay), "failed run never retried");
  abnormal.started(60, 203);
  check(!abnormal.finished(203, SIGTERM, now + delay), "signal exit was accepted");

  // The next index rebuild covers earlier runs without mixing child results.
  RunlogIndexRetry next_run;
  next_run.started(70, 301);
  check(!next_run.should_start(71, now), "new run overlapped an active rebuild");
  check(!next_run.finished(301, 1 << 8, now), "old run failure was accepted");
  check(next_run.should_start(71, now), "new run inherited old cooldown");
  next_run.started(71, 302);
  check(next_run.finished(302, 0, now), "new run did not succeed");
  check(next_run.last_successful_run() == 71, "new run result was mixed with old run");
  check(!next_run.should_start(70, now + delay), "old run repeated after newer success");

  RunlogIndexRetry lost;
  lost.started(80, 401);
  lost.lost_child(now);
  check(!lost.should_start(80, now), "lost child retried immediately");
  check(lost.should_start(80, now + delay), "lost child never retried");

  std::printf("runlog_index_retry_test: %d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
