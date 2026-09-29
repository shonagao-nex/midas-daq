#ifndef DAQ_MONITOR_RUNLOG_EDIT_H
#define DAQ_MONITOR_RUNLOG_EDIT_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace daq_monitor {

// Field names accepted from a future RPC request: Experiment, Type, Comment.
// A missing expected value means that the JSON field was absent when read.
struct RunlogFieldEdit {
  std::string field;
  std::optional<std::string> expected;
  std::string value;
};

struct RunlogEditRequest {
  std::int64_t run_number = 0;
  std::vector<RunlogFieldEdit> changes;
};

enum class RunlogEditCode {
  kOk,
  kInvalidRequest,
  kInvalidValue,
  kActiveRun,
  kNotFound,
  kUnsafeFile,
  kInvalidJson,
  kIncomplete,
  kRunMismatch,
  kConflict,
  kIoError,
  kDurabilityUnknown
};

struct RunlogEditResult {
  RunlogEditCode code;
  std::string message;
  bool ok() const { return code == RunlogEditCode::kOk; }
};

// The directory and activity predicate must come from trusted server-side
// configuration, never from the browser request. The predicate returns true
// for a run that is still acquiring or in its START/STOP transition.
class RunlogEditor {
 public:
  RunlogEditor(std::filesystem::path trusted_runlog_directory,
               std::function<bool(std::int64_t)> is_run_active);

  RunlogEditResult edit(const RunlogEditRequest& request) const;

 private:
  std::filesystem::path directory_;
  std::function<bool(std::int64_t)> is_run_active_;
};

}  // namespace daq_monitor

#endif
