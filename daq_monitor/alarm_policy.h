#ifndef DAQ_MONITOR_ALARM_POLICY_H
#define DAQ_MONITOR_ALARM_POLICY_H

#include <cstdint>
#include <string>

#include "status_policy.h"

namespace daq_monitor {

enum class AlarmLevel {
  kUnknown,
  kInactive,
  kWarning,
  kError,
};

struct AlarmObservation {
  std::string component;
  std::string severity;
  std::string reason;
  std::string detail;
  std::uint64_t observed_unix = 0;
  // A component can remain WARNING in status while being intentionally
  // excluded from alarm notification.
  bool alarm_suppressed = false;
};

struct AlarmRuntimeState {
  bool initialized = false;
  AlarmLevel level = AlarmLevel::kUnknown;
};

struct AlarmDecision {
  bool reset = false;
  bool trigger = false;
  AlarmLevel trigger_level = AlarmLevel::kUnknown;
  std::string message;
  AlarmRuntimeState next_state;
};

AlarmLevel alarm_level_from_severity(const std::string& severity);
const char* alarm_class_for_level(AlarmLevel level);
bool suppress_frontend_disconnect_alarm(RunState run_state, bool connected,
                                        const std::string& severity,
                                        const std::string& reason);
std::string format_alarm_message(const AlarmObservation& observation);
AlarmDecision decide_alarm_transition(
    const AlarmRuntimeState& current,
    const AlarmObservation& observation);

}  // namespace daq_monitor

#endif  // DAQ_MONITOR_ALARM_POLICY_H
