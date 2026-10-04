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
  // Default to monitored (fail-safe); only an exact current-run participant
  // record may set this false.
  bool participating = true;
  bool connected = false;
  bool status_fresh = false;
  bool acquisition_expected = false;
  bool acquisition_running = false;
  std::uint64_t event_slip_count = 0;
  std::uint64_t malformed_event_count = 0;
  std::uint64_t timeout_count = 0;
  std::uint64_t event_content_error_count = 0;
  std::uint64_t size_error_count = 0;
  std::uint64_t channel_mask_error_count = 0;
  std::uint64_t counter_discontinuity_count = 0;
};

struct EasirocRawStatus {
  bool participating = true;
  bool connected = false;
  bool status_fresh = false;
  bool acquisition_expected = true;
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

struct RunParticipation {
  bool valid = false;
  std::int32_t run_number = 0;
  bool vme = false;
  bool easiroc = false;
};

struct ActiveParticipation {
  bool known = false;
  bool vme = true;
  bool easiroc = true;
};

struct RawStatus {
  RunState run_state = RunState::kStopped;
  bool stop_transition_in_progress = false;
  bool monitor_status_fresh = false;
  double disk_free_gb = -1.0;
  bool logger_connected = false;
  bool vme_requested = true;
  bool easiroc_requested = true;
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
constexpr int kStartTransitionSequence = 400;

const char* severity_name(Severity severity);
ComponentStatus evaluate_disk(double free_gb);
ComponentStatus evaluate_logger(bool connected);
ComponentStatus evaluate_vme(RunState run_state, const VmeRawStatus& raw,
                             bool stop_transition_in_progress = false);
ComponentStatus evaluate_easiroc(RunState run_state,
                                 const EasirocRawStatus& raw,
                                 bool stop_transition_in_progress = false);
CanStartEvaluation evaluate_can_start(const RawStatus& raw);
ActiveParticipation resolve_run_participation(
    RunState run_state, std::int32_t current_run_number,
    const RunParticipation& recorded);
std::int64_t runlog_event_count(bool participating, double final_events_sent);
StatusEvaluation evaluate_status(const RawStatus& raw);

}  // namespace daq_monitor

#endif  // DAQ_MONITOR_STATUS_POLICY_H
