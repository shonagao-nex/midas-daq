#include "runlog_index_retry.h"

#include <algorithm>
#include <sys/wait.h>

namespace daq_monitor {

bool RunlogIndexRetry::should_start(int run, Clock::time_point now) const {
  return run > last_successful_run_ && active_child_ == 0 &&
         (run != failed_run_ || now >= retry_after_);
}

void RunlogIndexRetry::started(int run, pid_t child) {
  active_run_ = run;
  active_child_ = child;
}

void RunlogIndexRetry::failed(int run, Clock::time_point now) {
  failed_run_ = run;
  retry_after_ = now + kRetryDelay;
}

bool RunlogIndexRetry::finished(pid_t child, int status, Clock::time_point now) {
  if (child != active_child_ || child <= 0) return false;
  const bool success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
  if (success) {
    last_successful_run_ = std::max(last_successful_run_, active_run_);
    failed_run_ = 0;
  } else {
    failed(active_run_, now);
  }
  active_child_ = 0;
  active_run_ = 0;
  return success;
}

void RunlogIndexRetry::lost_child(Clock::time_point now) {
  if (active_child_ <= 0) return;
  failed(active_run_, now);
  active_child_ = 0;
  active_run_ = 0;
}

}  // namespace daq_monitor
