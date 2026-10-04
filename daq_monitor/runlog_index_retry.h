#ifndef DAQ_MONITOR_RUNLOG_INDEX_RETRY_H
#define DAQ_MONITOR_RUNLOG_INDEX_RETRY_H

#include <chrono>
#include <sys/types.h>

namespace daq_monitor {

class RunlogIndexRetry {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr auto kRetryDelay = std::chrono::seconds(5);

  // Decide whether the latest completed run needs an index refresh now.
  bool should_start(int run, Clock::time_point now) const;

  // Track a successfully spawned child until its exit status is collected.
  void started(int run, pid_t child);

  // Delay another attempt after a launch or preparation failure.
  void failed(int run, Clock::time_point now);

  // Mark a run complete only when its child exits successfully.
  bool finished(pid_t child, int status, Clock::time_point now);

  // Recover if the tracked child can no longer be reaped.
  void lost_child(Clock::time_point now);

  pid_t active_child() const { return active_child_; }
  int active_run() const { return active_run_; }
  int last_successful_run() const { return last_successful_run_; }

 private:
  int last_successful_run_ = 0;
  int failed_run_ = 0;
  int active_run_ = 0;
  pid_t active_child_ = 0;
  Clock::time_point retry_after_{};
};

}  // namespace daq_monitor

#endif
