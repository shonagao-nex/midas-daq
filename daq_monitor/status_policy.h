#ifndef DAQ_MONITOR_STATUS_POLICY_H
#define DAQ_MONITOR_STATUS_POLICY_H

#include <cstdint>
#include <string>

namespace daq_monitor {

enum class Severity { kOk, kWarning, kError };

enum class RunState {
  kRunning,
  kStopped,
  kPausedOrTransition,
};

struct ComponentStatus {
  Severity severity = Severity::kOk;
  std::string reason;
};

struct VmeRawStatus {
  bool connected = false;
  bool status_fresh = false;
  std::uint64_t event_slip_count = 0;
  std::uint64_t malformed_event_count = 0;
  std::uint64_t timeout_count = 0;
  std::uint64_t event_content_error_count = 0;
  std::uint64_t size_error_count = 0;
  std::uint64_t channel_mask_error_count = 0;
  std::uint64_t counter_discontinuity_count = 0;
};

struct EasirocRawStatus {
  bool connected = false;
  bool status_fresh = false;
  bool acquisition_running = false;
  bool acquisition_fault = false;
  std::uint64_t decode_error_count = 0;
  std::uint64_t timeout_count = 0;
  std::uint64_t overflow_count = 0;
  std::uint64_t event_content_error_count = 0;
};

struct ConfigurationRawStatus {
  bool ok = false;
  std::int32_t run_number = 0;
  std::uint64_t checked_unix = 0;
};

struct CanStartEvaluation {
  bool allowed = false;
  std::string reason;
};

struct ConfigurationEvaluation {
  bool ready = false;
  std::string reason;
};

struct StartEvaluation {
  bool allowed = false;
  std::string reason;
};

struct RawStatus {
  RunState run_state = RunState::kStopped;
  bool monitor_status_fresh = false;
  double disk_free_gb = -1.0;
  bool logger_connected = false;
  VmeRawStatus vme;
  EasirocRawStatus easiroc;
  ConfigurationRawStatus vme_configuration;
  ConfigurationRawStatus easiroc_configuration;
};

struct StatusEvaluation {
  ComponentStatus disk;
  ComponentStatus logger;
  ComponentStatus vme;
  ComponentStatus easiroc;
  Severity global_severity = Severity::kOk;
  std::string global_summary;
  CanStartEvaluation can_start;
};

constexpr double kDiskWarningThresholdGB = 50.0;
constexpr double kDiskErrorThresholdGB = 10.0;
constexpr std::uint64_t kMonitorStatusFreshnessSec = 3;

const char* severity_name(Severity severity);
ComponentStatus evaluate_disk(double free_gb);
ComponentStatus evaluate_logger(bool connected);
ComponentStatus evaluate_vme(RunState run_state, const VmeRawStatus& raw);
ComponentStatus evaluate_easiroc(RunState run_state,
                                 const EasirocRawStatus& raw);
CanStartEvaluation evaluate_can_start(const RawStatus& raw);
ConfigurationEvaluation evaluate_bor_configuration(
    std::int32_t target_run_number,
    const ConfigurationRawStatus& vme,
    const ConfigurationRawStatus& easiroc);
StartEvaluation evaluate_start(
    std::int32_t target_run_number,
    const CanStartEvaluation& pre_start,
    const ConfigurationRawStatus& vme,
    const ConfigurationRawStatus& easiroc);
StatusEvaluation evaluate_status(const RawStatus& raw);

}  // namespace daq_monitor

#endif  // DAQ_MONITOR_STATUS_POLICY_H
