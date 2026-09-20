#include "alarm_policy.h"

#include <ctime>

namespace daq_monitor {
namespace {

constexpr std::size_t kMidasAlarmMessageCapacity = 80;

std::string observation_time(std::uint64_t observed_unix) {
  const std::time_t value = static_cast<std::time_t>(observed_unix);
  std::tm utc{};
  char buffer[32] = {};
  if (observed_unix == 0 || gmtime_r(&value, &utc) == nullptr ||
      std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0)
    return "time unavailable";
  return buffer;
}

}  // namespace

AlarmLevel alarm_level_from_severity(const std::string& severity) {
  if (severity == "OK") return AlarmLevel::kInactive;
  if (severity == "WARNING") return AlarmLevel::kWarning;
  if (severity == "ERROR") return AlarmLevel::kError;
  return AlarmLevel::kUnknown;
}

const char* alarm_class_for_level(AlarmLevel level) {
  switch (level) {
    case AlarmLevel::kWarning:
      return "DAQ Warning";
    case AlarmLevel::kError:
      return "DAQ Error";
    case AlarmLevel::kUnknown:
    case AlarmLevel::kInactive:
      return "";
  }
  return "";
}

bool suppress_frontend_disconnect_alarm(RunState run_state, bool connected,
                                        const std::string& severity,
                                        const std::string& reason) {
  return run_state == RunState::kStopped && !connected &&
         severity == "WARNING" &&
         reason.find("disconnected while stopped") !=
             std::string::npos;
}

std::string format_alarm_message(const AlarmObservation& observation) {
  std::string message = "[" + observation_time(observation.observed_unix) +
                        "] " + observation.component + " " +
                        observation.severity + ": " + observation.reason;
  if (!observation.detail.empty())
    message += " (" + observation.detail + ")";

  // The installed MIDAS ALARM record reserves 80 bytes for Alarm Message.
  if (message.size() >= kMidasAlarmMessageCapacity &&
      !observation.detail.empty()) {
    // Keep count details syntactically complete. First reclaim the optional
    // separator after the timestamp; if that is not enough, shorten only the
    // human-readable reason, never the " (count=...)" suffix.
    const std::size_t timestamp_separator = message.find("] ");
    if (timestamp_separator != std::string::npos)
      message.erase(timestamp_separator + 1, 1);
    const std::string suffix = " (" + observation.detail + ")";
    if (message.size() >= kMidasAlarmMessageCapacity) {
      const std::size_t suffix_start = message.rfind(suffix);
      const std::size_t max_prefix =
          kMidasAlarmMessageCapacity - 1 - suffix.size();
      if (suffix_start != std::string::npos && suffix_start > max_prefix)
        message = message.substr(0, max_prefix) + suffix;
    }
  }
  if (message.size() >= kMidasAlarmMessageCapacity)
    message.resize(kMidasAlarmMessageCapacity - 1);
  return message;
}

AlarmDecision decide_alarm_transition(
    const AlarmRuntimeState& current,
    const AlarmObservation& observation) {
  AlarmDecision decision;
  decision.next_state = current;
  const AlarmLevel target = observation.alarm_suppressed
                                ? AlarmLevel::kInactive
                                : alarm_level_from_severity(observation.severity);

  // An unreadable or unrecognized status must not clear an existing alarm.
  if (target == AlarmLevel::kUnknown)
    return decision;

  decision.next_state = {true, target};
  if (!current.initialized) {
    // Clear a possibly stale MIDAS alarm left by an earlier monitor process.
    if (target == AlarmLevel::kInactive) {
      decision.reset = true;
    } else {
      decision.reset = true;
      decision.trigger = true;
    }
  } else if (target == current.level) {
    return decision;
  } else if (target == AlarmLevel::kInactive) {
    decision.reset = true;
  } else {
    // Reset before a WARNING/ERROR class change so the escalation produces
    // one fresh notification using the new class.
    decision.reset = current.level != AlarmLevel::kInactive;
    decision.trigger = true;
  }

  if (decision.trigger) {
    decision.trigger_level = target;
    decision.message = format_alarm_message(observation);
  }
  return decision;
}

}  // namespace daq_monitor
