#include "monitor_alarms.h"
#include "monitor_odb_utils.h"
#include "status_policy.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using daq_monitor::read_string;
using daq_monitor::read_value;

constexpr char kClientName[] = "daq_monitor";
HNDLE gDatabase = 0;

bool ensure_alarm_value(const std::string& path, const void* value, INT size,
                        DWORD type, bool overwrite = false) {
  HNDLE key = 0;
  const INT find_status =
      db_find_key(gDatabase, 0, path.c_str(), &key);
  if (find_status == DB_SUCCESS && !overwrite)
    return true;
  if (find_status != DB_SUCCESS && find_status != DB_NO_KEY) {
    cm_msg(MERROR, kClientName, "Cannot inspect alarm setting %s: status %d",
           path.c_str(), find_status);
    return false;
  }
  const INT status = db_set_value(gDatabase, 0, path.c_str(), value, size, 1,
                                  type);
  if (status != DB_SUCCESS) {
    cm_msg(MERROR, kClientName, "Cannot write alarm setting %s: status %d",
           path.c_str(), status);
    return false;
  }
  return true;
}

bool ensure_alarm_string(const std::string& path, const std::string& value,
                         std::size_t capacity, bool overwrite = false) {
  std::vector<char> buffer(capacity, '\0');
  const std::size_t length = std::min(value.size(), buffer.size() - 1);
  std::memcpy(buffer.data(), value.data(), length);
  return ensure_alarm_value(path, buffer.data(),
                            static_cast<INT>(buffer.size()), TID_STRING,
                            overwrite);
}

bool initialize_alarm_class(const char* name, const char* background_color) {
  const std::string root = std::string("/Alarms/Classes/") + name;
  const INT create_status = db_create_key(gDatabase, 0, root.c_str(), TID_KEY);
  if (create_status != DB_SUCCESS && create_status != DB_KEY_EXIST) {
    cm_msg(MERROR, kClientName, "Cannot create alarm class %s: status %d",
           name, create_status);
    return false;
  }

  const BOOL enabled = TRUE;
  const BOOL disabled = FALSE;
  const INT no_throttle = 0;
  const DWORD never = 0;
  bool ok = true;
  ok = ensure_alarm_value(root + "/Write system message", &enabled,
                          sizeof(enabled), TID_BOOL) && ok;
  ok = ensure_alarm_value(root + "/Write Elog message", &disabled,
                          sizeof(disabled), TID_BOOL) && ok;
  ok = ensure_alarm_value(root + "/System message interval", &no_throttle,
                          sizeof(no_throttle), TID_INT) && ok;
  ok = ensure_alarm_value(root + "/System message last", &never,
                          sizeof(never), TID_DWORD) && ok;
  ok = ensure_alarm_string(root + "/Execute command", "", 256) && ok;
  ok = ensure_alarm_value(root + "/Execute interval", &no_throttle,
                          sizeof(no_throttle), TID_INT) && ok;
  ok = ensure_alarm_value(root + "/Execute last", &never, sizeof(never),
                          TID_DWORD) && ok;
  // This is deliberately enforced even if the class already existed.
  ok = ensure_alarm_value(root + "/Stop run", &disabled, sizeof(disabled),
                          TID_BOOL, true) && ok;
  ok = ensure_alarm_string(root + "/Display BGColor", background_color, 32) &&
       ok;
  ok = ensure_alarm_string(root + "/Display FGColor", "black", 32) && ok;
  ok = ensure_alarm_value(root + "/Alarm sound", &enabled, sizeof(enabled),
                          TID_BOOL) && ok;
  return ok;
}

bool initialize_alarm_classes() {
  return initialize_alarm_class("DAQ Warning", "yellow") &&
         initialize_alarm_class("DAQ Error", "red");
}

bool validate_global_alarm_class() {
  HNDLE key = 0;
  if (db_find_key(gDatabase, 0, "/Alarms/Classes/All", &key) != DB_SUCCESS)
    return true;

  BOOL stop_run = FALSE;
  if (!read_value(gDatabase, "/Alarms/Classes/All/Stop run", TID_BOOL, &stop_run)) {
    cm_msg(MERROR, kClientName,
           "Cannot verify /Alarms/Classes/All/Stop run");
    return false;
  }
  if (stop_run != FALSE) {
    cm_msg(MERROR, kClientName,
           "Refusing alarm synchronization because the MIDAS All alarm "
           "class has Stop run enabled");
    return false;
  }
  return true;
}

std::string count_detail(const std::string& reason,
                         const char* counter_path,
                         const char* reason_fragment) {
  if (reason.find(reason_fragment) == std::string::npos)
    return {};
  std::uint64_t count = 0;
  if (!read_value(gDatabase, counter_path, TID_QWORD, &count))
    return {};
  return "count=" + std::to_string(count);
}

std::string vme_alarm_detail(const std::string& reason) {
  std::string detail = count_detail(
      reason, "/DAQ/Status/Frontends/VME/EventSlipCount", "event slip");
  if (!detail.empty()) return detail;
  detail = count_detail(reason,
                        "/DAQ/Status/Frontends/VME/MalformedEventCount",
                        "malformed event");
  if (!detail.empty()) return detail;
  detail = count_detail(reason, "/DAQ/Status/Frontends/VME/TimeoutCount",
                        "timeout");
  if (!detail.empty()) return detail;
  detail = count_detail(
      reason, "/DAQ/Status/Frontends/VME/EventContentErrorCount",
      "content error");
  if (!detail.empty()) return detail;
  return count_detail(
      reason, "/DAQ/Status/Frontends/VME/CounterDiscontinuityCount",
      "counter discontinuity");
}

std::string easiroc_alarm_detail(const std::string& reason) {
  std::string detail = count_detail(
      reason, "/DAQ/Status/Frontends/EASIROC/DecodeErrorCount",
      "decode error");
  if (!detail.empty()) return detail;
  detail = count_detail(reason,
                        "/DAQ/Status/Frontends/EASIROC/TimeoutCount",
                        "timeout");
  if (!detail.empty()) return detail;
  detail = count_detail(reason,
                        "/DAQ/Status/Frontends/EASIROC/OverflowCount",
                        "over-threshold flag");
  if (!detail.empty()) return detail;
  return count_detail(
      reason, "/DAQ/Status/Frontends/EASIROC/EventContentErrorCount",
      "content error");
}

bool alarm_exists(const char* alarm_name) {
  HNDLE key = 0;
  const std::string path =
      std::string("/Alarms/Alarms/") + alarm_name;
  return db_find_key(gDatabase, 0, path.c_str(), &key) == DB_SUCCESS;
}

bool set_alarm_class(const char* alarm_name, const char* alarm_class) {
  const std::string path = std::string("/Alarms/Alarms/") + alarm_name +
                           "/Alarm Class";
  return ensure_alarm_string(path, alarm_class, 32, true);
}

bool apply_alarm_decision(const char* alarm_name,
                          const daq_monitor::AlarmDecision& decision) {
  const bool exists = alarm_exists(alarm_name);
  const char* alarm_class = decision.trigger
                                ? daq_monitor::alarm_class_for_level(
                                      decision.trigger_level)
                                : "";
  if (decision.trigger && exists &&
      !set_alarm_class(alarm_name, alarm_class))
    return false;

  if (decision.reset) {
    const INT status = al_reset_alarm(alarm_name);
    if (status != AL_SUCCESS && status != AL_RESET &&
        !(status == AL_INVALID_NAME && !exists)) {
      cm_msg(MERROR, kClientName, "Cannot reset alarm %s: status %d",
             alarm_name, status);
      return false;
    }
  }

  if (!decision.trigger)
    return true;

  const INT status = al_trigger_alarm(
      alarm_name, decision.message.c_str(), alarm_class,
      decision.message.c_str(), AT_INTERNAL);
  if (status != AL_SUCCESS) {
    cm_msg(MERROR, kClientName, "Cannot trigger alarm %s: status %d",
           alarm_name, status);
    return false;
  }
  return true;
}

bool update_component_alarm(
    const char* alarm_name,
    daq_monitor::AlarmRuntimeState* runtime_state,
    const daq_monitor::AlarmObservation& observation,
    bool alarm_system_active) {
  const daq_monitor::AlarmDecision decision =
      daq_monitor::decide_alarm_transition(*runtime_state, observation,
                                           alarm_system_active);
  if (!apply_alarm_decision(alarm_name, decision))
    return false;
  *runtime_state = decision.next_state;
  return true;
}

//************************************//
// Synchronize DAQ component alarms with current status
//************************************//
bool update_alarms(daq_monitor::MonitorAlarmState* state) {
  const std::time_t now = std::time(nullptr);
  const std::uint64_t now_unix =
      now < 0 ? 0 : static_cast<std::uint64_t>(now);

  std::string vme_severity;
  std::string vme_reason;
  std::string easiroc_severity;
  std::string easiroc_reason;
  std::string logger_severity;
  std::string disk_severity;
  double disk_free_gb = -1.0;
  INT run_state = 0;
  BOOL vme_connected = FALSE;
  BOOL easiroc_connected = FALSE;
  BOOL alarm_system_active = FALSE;

  if (!read_value(gDatabase, "/Alarms/Alarm system active", TID_BOOL,
                  &alarm_system_active))
    return false;

  const bool vme_ok =
      read_string(gDatabase, "/DAQ/Status/Frontends/VME/Severity", &vme_severity) &&
      read_string(gDatabase, "/DAQ/Status/Frontends/VME/Reason", &vme_reason);
  const bool easiroc_ok =
      read_string(gDatabase, "/DAQ/Status/Frontends/EASIROC/Severity",
                  &easiroc_severity) &&
      read_string(gDatabase, "/DAQ/Status/Frontends/EASIROC/Reason", &easiroc_reason);
  const bool logger_ok =
      read_string(gDatabase, "/DAQ/Status/Logger/Severity", &logger_severity);
  const bool disk_ok =
      read_string(gDatabase, "/DAQ/Status/Disk/Severity", &disk_severity) &&
      read_value(gDatabase, "/DAQ/Status/Disk/FreeGB", TID_DOUBLE, &disk_free_gb);
  const bool run_state_ok =
      read_value(gDatabase, "/DAQ/Status/Run/State", TID_INT32, &run_state);
  const bool vme_connection_ok = read_value(gDatabase,
      "/DAQ/Status/Frontends/VME/Connected", TID_BOOL, &vme_connected);
  const bool easiroc_connection_ok = read_value(gDatabase,
      "/DAQ/Status/Frontends/EASIROC/Connected", TID_BOOL,
      &easiroc_connected);

  daq_monitor::AlarmObservation vme{
      "VME", vme_ok ? vme_severity : "",
      vme_ok ? vme_reason : "status unavailable", {}, now_unix};
  if (vme_ok)
    vme.detail = vme_alarm_detail(vme.reason);
  vme.alarm_suppressed =
      run_state_ok && vme_connection_ok &&
      daq_monitor::suppress_frontend_disconnect_alarm(
          daq_monitor::policy_run_state(run_state), vme_connected != FALSE, vme.severity,
          vme.reason);

  daq_monitor::AlarmObservation easiroc{
      "EASIROC", easiroc_ok ? easiroc_severity : "",
      easiroc_ok ? easiroc_reason : "status unavailable", {}, now_unix};
  if (easiroc_ok)
    easiroc.detail = easiroc_alarm_detail(easiroc.reason);
  easiroc.alarm_suppressed =
      run_state_ok && easiroc_connection_ok &&
      daq_monitor::suppress_frontend_disconnect_alarm(
          daq_monitor::policy_run_state(run_state), easiroc_connected != FALSE,
          easiroc.severity, easiroc.reason);

  const daq_monitor::AlarmObservation logger{
      "Logger", logger_ok ? logger_severity : "",
      logger_ok ? "Logger disconnected" : "status unavailable", {},
      now_unix};

  char disk_detail[64] = {};
  if (disk_ok)
    std::snprintf(disk_detail, sizeof(disk_detail), "free=%.1f GB",
                  disk_free_gb);
  const daq_monitor::AlarmObservation disk{
      "Disk", disk_ok ? disk_severity : "",
      disk_ok ? "free space" : "status unavailable",
      disk_ok ? disk_detail : "", now_unix};

  bool ok = true;
  ok = update_component_alarm("DAQ_VME", &state->vme_alarm, vme,
                              alarm_system_active != FALSE) && ok;
  ok = update_component_alarm("DAQ_EASIROC", &state->easiroc_alarm,
                              easiroc, alarm_system_active != FALSE) && ok;
  ok = update_component_alarm("DAQ_LOGGER", &state->logger_alarm, logger,
                              alarm_system_active != FALSE) && ok;
  ok = update_component_alarm("DAQ_DISK", &state->disk_alarm, disk,
                              alarm_system_active != FALSE) && ok;
  return ok;
}


}  // namespace

namespace daq_monitor {

//************************************//
// Initialize component alarm classes in ODB
//************************************//
bool initialize_monitor_alarm_classes(HNDLE database) {
  gDatabase = database;
  return initialize_alarm_classes();
}

//************************************//
// Verify the global alarm class cannot stop a run
//************************************//
bool validate_monitor_global_alarm_class() {
  return validate_global_alarm_class();
}

//************************************//
// Synchronize component alarms with current status
//************************************//
bool update_monitor_alarms(MonitorAlarmState* state) {
  return update_alarms(state);
}

}  // namespace daq_monitor
