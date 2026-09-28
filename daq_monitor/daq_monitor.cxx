#include "midas.h"
#include "alarm_policy.h"
#include "status_policy.h"

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>
#include <sys/statvfs.h>
#include <vector>

namespace {

constexpr char kClientName[] = "daq_monitor";
constexpr char kStatusRoot[] = "/DAQ/Status";
constexpr DWORD kUpdatePeriodMs = 1000;
constexpr DWORD kDiskUpdatePeriodMs = 10000;
constexpr std::size_t kStringCapacity = 512;
constexpr double kBytesPerGB = 1000.0 * 1000.0 * 1000.0;
constexpr std::size_t kTransitionErrorCapacity = 256;

constexpr char kVmeClientName[] = "fevme";
constexpr char kEasirocClientName[] = "feeasiroc";
constexpr char kLoggerClientName[] = "Logger";

volatile std::sig_atomic_t gStopRequested = 0;
HNDLE gDatabase = 0;

struct LoggerSource {
  std::string channel;
  std::string current_filename;
  std::string data_directory;
};

struct DiskCache {
  std::string path;
  double free_gb = -1.0;
  double total_gb = -1.0;
  DWORD last_update_ms = 0;
  bool initialized = false;
};

DiskCache gDiskCache;
daq_monitor::AlarmRuntimeState gVmeAlarmState;
daq_monitor::AlarmRuntimeState gEasirocAlarmState;
daq_monitor::AlarmRuntimeState gLoggerAlarmState;
daq_monitor::AlarmRuntimeState gDiskAlarmState;

void handle_signal(int) { gStopRequested = 1; }

daq_monitor::RunState policy_run_state(INT state);

bool is_owned_status_path(const char* path) {
  const std::size_t root_length = std::strlen(kStatusRoot);
  return std::strncmp(path, kStatusRoot, root_length) == 0 &&
         (path[root_length] == '\0' || path[root_length] == '/');
}

bool write_value(const char* path, const void* value, INT size, DWORD type) {
  if (!is_owned_status_path(path)) {
    cm_msg(MERROR, kClientName, "Refusing write outside %s: %s", kStatusRoot,
           path);
    return false;
  }
  const INT status =
      db_set_value(gDatabase, 0, path, value, size, 1, type);
  if (status != DB_SUCCESS) {
    cm_msg(MERROR, kClientName, "Cannot publish %s: status %d", path,
           status);
    return false;
  }
  return true;
}

bool write_string(const char* path, const std::string& value) {
  std::vector<char> buffer(kStringCapacity, '\0');
  const std::size_t length = std::min(value.size(), buffer.size() - 1);
  std::memcpy(buffer.data(), value.data(), length);
  return write_value(path, buffer.data(), static_cast<INT>(buffer.size()),
                     TID_STRING);
}

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

template <typename T>
bool read_value(const char* path, DWORD type, T* value) {
  T candidate{};
  INT size = sizeof(candidate);
  const INT status =
      db_get_value(gDatabase, 0, path, &candidate, &size, type, FALSE);
  if (status != DB_SUCCESS || size != static_cast<INT>(sizeof(candidate)))
    return false;
  *value = candidate;
  return true;
}

bool read_string(const std::string& path, std::string* value) {
  char buffer[kStringCapacity] = {};
  INT size = sizeof(buffer);
  const INT status = db_get_value(gDatabase, 0, path.c_str(), buffer, &size,
                                  TID_STRING, FALSE);
  if (status != DB_SUCCESS || size <= 0)
    return false;
  buffer[sizeof(buffer) - 1] = '\0';
  *value = buffer;
  return true;
}

daq_monitor::RunParticipation read_run_participation() {
  BOOL valid = FALSE;
  INT run_number = 0;
  BOOL vme = FALSE;
  BOOL easiroc = FALSE;
  if (!read_value("/DAQ/Status/Run/ParticipationValid", TID_BOOL, &valid) ||
      !read_value("/DAQ/Status/Run/ParticipationRunNumber", TID_INT32,
                  &run_number) ||
      !read_value("/DAQ/Status/Run/VMEParticipating", TID_BOOL, &vme) ||
      !read_value("/DAQ/Status/Run/EASIROCParticipating", TID_BOOL,
                  &easiroc))
    return {};
  return {valid != FALSE, run_number, vme != FALSE, easiroc != FALSE};
}

bool record_run_participation(INT run_number, bool vme, bool easiroc) {
  const BOOL invalid = FALSE;
  const BOOL valid = TRUE;
  const BOOL vme_participating = vme ? TRUE : FALSE;
  const BOOL easiroc_participating = easiroc ? TRUE : FALSE;

  // Invalidate first and publish Valid last so a partial write can never be
  // mistaken for the authoritative participant set of this run.
  bool ok = write_value("/DAQ/Status/Run/ParticipationValid", &invalid,
                        sizeof(invalid), TID_BOOL);
  ok = write_value("/DAQ/Status/Run/ParticipationRunNumber", &run_number,
                   sizeof(run_number), TID_INT32) && ok;
  ok = write_value("/DAQ/Status/Run/VMEParticipating", &vme_participating,
                   sizeof(vme_participating), TID_BOOL) && ok;
  ok = write_value("/DAQ/Status/Run/EASIROCParticipating",
                   &easiroc_participating,
                   sizeof(easiroc_participating), TID_BOOL) && ok;
  if (!ok)
    return false;
  return write_value("/DAQ/Status/Run/ParticipationValid", &valid,
                     sizeof(valid), TID_BOOL);
}

bool validate_global_alarm_class() {
  HNDLE key = 0;
  if (db_find_key(gDatabase, 0, "/Alarms/Classes/All", &key) != DB_SUCCESS)
    return true;

  BOOL stop_run = FALSE;
  if (!read_value("/Alarms/Classes/All/Stop run", TID_BOOL, &stop_run)) {
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
  if (!read_value(counter_path, TID_QWORD, &count))
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

bool update_alarms() {
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

  if (!read_value("/Alarms/Alarm system active", TID_BOOL,
                  &alarm_system_active))
    return false;

  const bool vme_ok =
      read_string("/DAQ/Status/Frontends/VME/Severity", &vme_severity) &&
      read_string("/DAQ/Status/Frontends/VME/Reason", &vme_reason);
  const bool easiroc_ok =
      read_string("/DAQ/Status/Frontends/EASIROC/Severity",
                  &easiroc_severity) &&
      read_string("/DAQ/Status/Frontends/EASIROC/Reason", &easiroc_reason);
  const bool logger_ok =
      read_string("/DAQ/Status/Logger/Severity", &logger_severity);
  const bool disk_ok =
      read_string("/DAQ/Status/Disk/Severity", &disk_severity) &&
      read_value("/DAQ/Status/Disk/FreeGB", TID_DOUBLE, &disk_free_gb);
  const bool run_state_ok =
      read_value("/DAQ/Status/Run/State", TID_INT32, &run_state);
  const bool vme_connection_ok = read_value(
      "/DAQ/Status/Frontends/VME/Connected", TID_BOOL, &vme_connected);
  const bool easiroc_connection_ok = read_value(
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
          policy_run_state(run_state), vme_connected != FALSE, vme.severity,
          vme.reason);

  daq_monitor::AlarmObservation easiroc{
      "EASIROC", easiroc_ok ? easiroc_severity : "",
      easiroc_ok ? easiroc_reason : "status unavailable", {}, now_unix};
  if (easiroc_ok)
    easiroc.detail = easiroc_alarm_detail(easiroc.reason);
  easiroc.alarm_suppressed =
      run_state_ok && easiroc_connection_ok &&
      daq_monitor::suppress_frontend_disconnect_alarm(
          policy_run_state(run_state), easiroc_connected != FALSE,
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
  ok = update_component_alarm("DAQ_VME", &gVmeAlarmState, vme,
                              alarm_system_active != FALSE) && ok;
  ok = update_component_alarm("DAQ_EASIROC", &gEasirocAlarmState,
                              easiroc, alarm_system_active != FALSE) && ok;
  ok = update_component_alarm("DAQ_LOGGER", &gLoggerAlarmState, logger,
                              alarm_system_active != FALSE) && ok;
  ok = update_component_alarm("DAQ_DISK", &gDiskAlarmState, disk,
                              alarm_system_active != FALSE) && ok;
  return ok;
}

struct ClientHealth {
  bool connected = false;
  bool status_fresh = false;
};

ClientHealth client_health(const char* client_name) {
  DWORD timeout_ms = 0;
  DWORD elapsed_ms = 0;
  const INT status = cm_get_watchdog_info(gDatabase, client_name, &timeout_ms,
                                          &elapsed_ms);
  if (status != DB_SUCCESS)
    return {};
  return {true, timeout_ms == 0 || elapsed_ms <= timeout_ms};
}

bool timestamp_is_fresh(std::uint64_t now_unix,
                        std::uint64_t last_update_unix) {
  return last_update_unix != 0 && now_unix >= last_update_unix &&
         now_unix - last_update_unix <=
             daq_monitor::kMonitorStatusFreshnessSec;
}

bool publish_can_start(const daq_monitor::CanStartEvaluation& evaluation) {
  const BOOL allowed = evaluation.allowed ? TRUE : FALSE;
  bool ok = true;
  if (!evaluation.allowed)
    ok = write_value("/DAQ/Status/Global/CanStart", &allowed,
                     sizeof(allowed), TID_BOOL) && ok;
  ok = write_string("/DAQ/Status/Global/CanStartReason",
                    evaluation.reason) && ok;
  if (evaluation.allowed)
    ok = write_value("/DAQ/Status/Global/CanStart", &allowed,
                     sizeof(allowed), TID_BOOL) && ok;
  return ok;
}

daq_monitor::RunState policy_run_state(INT state) {
  switch (state) {
    case STATE_RUNNING:
      return daq_monitor::RunState::kRunning;
    case STATE_STOPPED:
      return daq_monitor::RunState::kStopped;
    case STATE_PAUSED:
    default:
      return daq_monitor::RunState::kPausedOrTransition;
  }
}

std::uint64_t add_saturating(std::uint64_t left, std::uint64_t right) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left)
    return std::numeric_limits<std::uint64_t>::max();
  return left + right;
}

std::string logger_channel_path() {
  HNDLE channels_key = 0;
  if (db_find_key(gDatabase, 0, "/Logger/Channels", &channels_key) !=
      DB_SUCCESS)
    return {};

  std::string first_channel;
  for (INT index = 0;; ++index) {
    HNDLE channel_key = 0;
    const INT status =
        db_enum_key(gDatabase, channels_key, index, &channel_key);
    if (status == DB_NO_MORE_SUBKEYS)
      break;
    if (status != DB_SUCCESS)
      break;

    KEY key{};
    if (db_get_key(gDatabase, channel_key, &key) != DB_SUCCESS ||
        key.type != TID_KEY)
      continue;

    const std::string base = std::string("/Logger/Channels/") + key.name;
    if (first_channel.empty())
      first_channel = base;

    BOOL active = FALSE;
    std::string type;
    read_value((base + "/Settings/Active").c_str(), TID_BOOL, &active);
    read_string(base + "/Settings/Type", &type);
    if (active != FALSE && (type.empty() || equal_ustring(type.c_str(), "Disk")))
      return base;
  }
  return first_channel;
}

LoggerSource read_logger_source() {
  LoggerSource source;
  source.channel = logger_channel_path();
  if (!source.channel.empty())
    read_string(source.channel + "/Settings/Current filename",
                &source.current_filename);
  read_string("/Logger/Data dir", &source.data_directory);
  return source;
}

std::string parent_directory(const std::string& path) {
  const std::size_t separator = path.find_last_of('/');
  if (separator == std::string::npos)
    return ".";
  if (separator == 0)
    return "/";
  return path.substr(0, separator);
}

std::string disk_path_for(const LoggerSource& source) {
  const std::string data_directory = cm_expand_env(source.data_directory.c_str());
  const std::string current_filename =
      cm_expand_env(source.current_filename.c_str());

  if (current_filename.empty())
    return data_directory.empty() ? "." : data_directory;
  if (current_filename.front() == '/')
    return parent_directory(current_filename);

  std::string combined = data_directory;
  if (!combined.empty() && combined.back() != '/')
    combined += '/';
  combined += current_filename;
  return parent_directory(combined);
}

void update_disk_cache(const LoggerSource& source, DWORD now_ms) {
  const std::string path = disk_path_for(source);
  const DWORD elapsed = now_ms - gDiskCache.last_update_ms;
  if (gDiskCache.initialized && path == gDiskCache.path &&
      elapsed < kDiskUpdatePeriodMs)
    return;

  gDiskCache.path = path;
  const double free_bytes = ss_disk_free(path.c_str());
  gDiskCache.free_gb = free_bytes < 0.0 ? -1.0 : free_bytes / kBytesPerGB;
  struct statvfs filesystem {};
  if (statvfs(path.c_str(), &filesystem) == 0) {
    gDiskCache.total_gb =
        static_cast<double>(filesystem.f_blocks) * filesystem.f_frsize /
        kBytesPerGB;
  } else {
    gDiskCache.total_gb = -1.0;
  }
  gDiskCache.last_update_ms = now_ms;
  gDiskCache.initialized = true;
}

bool publish_status(bool synchronous_start_check = false) {
  INT run_number = 0;
  INT run_state = 0;
  INT transition_in_progress = 0;
  DWORD start_time = 0;
  DWORD stop_time = 0;
  std::uint64_t previous_update_unix = 0;
  bool collection_ok = true;
  collection_ok =
      read_value("/Runinfo/Run number", TID_INT32, &run_number) &&
      collection_ok;
  collection_ok = read_value("/Runinfo/State", TID_INT32, &run_state) &&
                  collection_ok;
  // MIDAS writes the transition number before invoking callbacks and clears
  // it only after updating Runinfo/State. If this read fails, remain fail-safe
  // and do not suppress any acquisition-state error.
  read_value("/Runinfo/Transition in progress", TID_INT32,
             &transition_in_progress);
  collection_ok = read_value("/Runinfo/Start time binary", TID_DWORD,
                             &start_time) && collection_ok;
  read_value("/Runinfo/Stop time binary", TID_DWORD, &stop_time);
  read_value("/DAQ/Status/Global/LastUpdateUnix", TID_QWORD,
             &previous_update_unix);

  const std::time_t now = std::time(nullptr);
  const std::uint64_t now_unix =
      now < 0 ? 0 : static_cast<std::uint64_t>(now);
  std::uint64_t duration_sec = 0;
  if (run_state == STATE_RUNNING && start_time != 0 &&
      now_unix >= static_cast<std::uint64_t>(start_time))
    duration_sec = now_unix - static_cast<std::uint64_t>(start_time);
  else if (run_state == STATE_STOPPED && start_time != 0 &&
           stop_time >= start_time)
    duration_sec = stop_time - start_time;

  const ClientHealth vme_health = client_health(kVmeClientName);
  const ClientHealth easiroc_health = client_health(kEasirocClientName);
  const ClientHealth logger_health = client_health(kLoggerClientName);
  const BOOL vme_connected = vme_health.connected ? TRUE : FALSE;
  const BOOL vme_status_fresh = vme_health.status_fresh ? TRUE : FALSE;
  const BOOL easiroc_connected = easiroc_health.connected ? TRUE : FALSE;
  const BOOL easiroc_status_fresh =
      easiroc_health.status_fresh ? TRUE : FALSE;
  const BOOL logger_connected = logger_health.status_fresh ? TRUE : FALSE;

  BOOL vme_configuration_ok = FALSE;
  INT vme_configuration_run_number = 0;
  std::uint64_t vme_configuration_checked_unix = 0;
  BOOL easiroc_configuration_ok = FALSE;
  INT easiroc_configuration_run_number = 0;
  std::uint64_t easiroc_configuration_checked_unix = 0;
  // Configuration is a BOR result, not an input to the sequence-400
  // pre-start policy. Missing or old values remain false/zero for monitoring;
  // each participating frontend reports BOR failure through its own
  // sequence-500 transition callback.
  read_value("/Equipment/VME/Variables/Frontend/ConfigurationOK", TID_BOOL,
             &vme_configuration_ok);
  read_value("/Equipment/VME/Variables/Frontend/ConfigurationRunNumber",
             TID_INT, &vme_configuration_run_number);
  read_value("/Equipment/VME/Variables/Frontend/ConfigurationCheckedUnix",
             TID_QWORD, &vme_configuration_checked_unix);
  read_value("/Equipment/EASIROC/Variables/Frontend/ConfigurationOK",
             TID_BOOL, &easiroc_configuration_ok);
  read_value(
      "/Equipment/EASIROC/Variables/Frontend/ConfigurationRunNumber",
      TID_INT, &easiroc_configuration_run_number);
  read_value(
      "/Equipment/EASIROC/Variables/Frontend/ConfigurationCheckedUnix",
      TID_QWORD, &easiroc_configuration_checked_unix);

  std::uint64_t event_slip_count = 0;
  std::uint64_t malformed_event_count = 0;
  std::uint64_t vme_timeout_count = 0;
  std::uint64_t counter_discontinuity_count = 0;
  std::uint64_t size_error_count = 0;
  std::uint64_t channel_mask_error_count = 0;
  BOOL v1720_enabled = TRUE;
  BOOL v1720_running = FALSE;
  read_value("/Equipment/VME/Variables/V1720E/EnabledForRun", TID_BOOL,
             &v1720_enabled);
  read_value("/Equipment/VME/Variables/V1720E/Running", TID_BOOL,
             &v1720_running);
  read_value("/Equipment/VME/Variables/RunCounters/EventSlipCount",
             TID_QWORD, &event_slip_count);
  read_value(
      "/Equipment/VME/Variables/RunCounters/V1720EMalformedEventCount",
      TID_QWORD, &malformed_event_count);
  read_value("/Equipment/VME/Variables/RunCounters/V1720EReadTimeoutCount",
             TID_QWORD, &vme_timeout_count);
  read_value(
      "/Equipment/VME/Variables/RunCounters/V1720ECounterDiscontinuityCount",
      TID_QWORD, &counter_discontinuity_count);
  read_value("/Equipment/VME/Variables/RunCounters/V1720ESizeErrorCount",
             TID_QWORD, &size_error_count);
  read_value(
      "/Equipment/VME/Variables/RunCounters/V1720EChannelMaskErrorCount",
      TID_QWORD, &channel_mask_error_count);
  const std::uint64_t vme_event_content_error_count =
      add_saturating(size_error_count, channel_mask_error_count);

  BOOL easiroc_running = FALSE;
  BOOL easiroc_enabled = TRUE;
  BOOL easiroc_fault = FALSE;
  std::uint64_t easiroc_event_counter = 0;
  std::uint64_t decode_error_count = 0;
  std::uint64_t easiroc_timeout_count = 0;
  std::uint64_t overflow_count = 0;
  std::uint64_t easiroc_event_content_error_count = 0;
  read_value("/Equipment/EASIROC/Variables/EnabledForRun", TID_BOOL,
             &easiroc_enabled);
  read_value("/Equipment/EASIROC/Variables/AcquisitionRunning", TID_BOOL,
             &easiroc_running);
  read_value("/Equipment/EASIROC/Variables/AcquisitionFault", TID_BOOL,
             &easiroc_fault);
  read_value("/Equipment/EASIROC/Variables/EventCounter", TID_QWORD,
             &easiroc_event_counter);
  read_value("/Equipment/EASIROC/Variables/Statistics/DecodeErrorCount",
             TID_QWORD, &decode_error_count);
  read_value("/Equipment/EASIROC/Variables/Statistics/ReceiveTimeoutCount",
             TID_QWORD, &easiroc_timeout_count);
  read_value("/Equipment/EASIROC/Variables/Statistics/ADCOverflowCount",
             TID_QWORD, &overflow_count);
  read_value(
      "/Equipment/EASIROC/Variables/Statistics/EventContentErrorCount",
      TID_QWORD, &easiroc_event_content_error_count);

  const LoggerSource logger = read_logger_source();
  update_disk_cache(logger, ss_millitime());

  daq_monitor::RawStatus raw_status;
  raw_status.run_state = policy_run_state(run_state);
  raw_status.stop_transition_in_progress =
      transition_in_progress == TR_STOP;
  raw_status.monitor_status_fresh =
      collection_ok &&
      (synchronous_start_check ||
       timestamp_is_fresh(now_unix, previous_update_unix));
  raw_status.disk_free_gb = gDiskCache.free_gb;
  raw_status.logger_connected = logger_connected != FALSE;
  const daq_monitor::ActiveParticipation participation =
      daq_monitor::resolve_run_participation(
          raw_status.run_state, run_number, read_run_participation());
  // Preserve the largest observed MIDAS event count if a participating
  // frontend disconnects before EOR. The record is keyed by run number.
  if (run_state == STATE_RUNNING || run_state == STATE_PAUSED) {
    INT count_run = 0;
    double previous_vme = 0, previous_easiroc = 0;
    read_value("/DAQ/Status/Runlog/CountRunNumber", TID_INT32, &count_run);
    if (count_run == run_number) {
      read_value("/DAQ/Status/Runlog/ObservedVMEEvents", TID_DOUBLE,
                 &previous_vme);
      read_value("/DAQ/Status/Runlog/ObservedEASIROCEvents", TID_DOUBLE,
                 &previous_easiroc);
    }
    double current_vme = 0, current_easiroc = 0;
    read_value("/Equipment/VME/Statistics/Events sent", TID_DOUBLE,
               &current_vme);
    read_value("/Equipment/NIM-EASIROC Physics/Statistics/Events sent",
               TID_DOUBLE, &current_easiroc);
    const double observed_vme = participation.vme
        ? std::max(previous_vme, current_vme) : 0;
    const double observed_easiroc = participation.easiroc
        ? std::max(previous_easiroc, current_easiroc) : 0;
    write_value("/DAQ/Status/Runlog/ObservedVMEEvents", &observed_vme,
                sizeof(observed_vme), TID_DOUBLE);
    write_value("/DAQ/Status/Runlog/ObservedEASIROCEvents", &observed_easiroc,
                sizeof(observed_easiroc), TID_DOUBLE);
    write_value("/DAQ/Status/Runlog/CountRunNumber", &run_number,
                sizeof(run_number), TID_INT32);
  }
  raw_status.vme.participating = participation.vme;
  raw_status.vme.connected = vme_connected != FALSE;
  raw_status.vme.status_fresh = vme_status_fresh != FALSE;
  raw_status.vme.acquisition_expected = v1720_enabled != FALSE;
  raw_status.vme.acquisition_running = v1720_running != FALSE;
  raw_status.vme.event_slip_count = event_slip_count;
  raw_status.vme.malformed_event_count = malformed_event_count;
  raw_status.vme.timeout_count = vme_timeout_count;
  raw_status.vme.event_content_error_count = vme_event_content_error_count;
  raw_status.vme.size_error_count = size_error_count;
  raw_status.vme.channel_mask_error_count = channel_mask_error_count;
  raw_status.vme.counter_discontinuity_count = counter_discontinuity_count;
  raw_status.easiroc.participating = participation.easiroc;
  raw_status.easiroc.connected = easiroc_connected != FALSE;
  raw_status.easiroc.status_fresh = easiroc_status_fresh != FALSE;
  raw_status.easiroc.acquisition_expected = easiroc_enabled != FALSE;
  raw_status.easiroc.acquisition_running = easiroc_running != FALSE;
  raw_status.easiroc.acquisition_fault = easiroc_fault != FALSE;
  raw_status.easiroc.decode_error_count = decode_error_count;
  raw_status.easiroc.timeout_count = easiroc_timeout_count;
  raw_status.easiroc.overflow_count = overflow_count;
  raw_status.easiroc.event_content_error_count =
      easiroc_event_content_error_count;
  raw_status.vme_configuration = {
      vme_configuration_ok != FALSE, vme_configuration_run_number,
      vme_configuration_checked_unix};
  raw_status.easiroc_configuration = {
      easiroc_configuration_ok != FALSE, easiroc_configuration_run_number,
      easiroc_configuration_checked_unix};
  const daq_monitor::StatusEvaluation evaluation =
      daq_monitor::evaluate_status(raw_status);

  // Keep the worst observed run status so a transient disconnect remains
  // visible in the EOR record even if the frontend reconnects before STOP.
  if (run_state == STATE_RUNNING || run_state == STATE_PAUSED) {
    INT status_run = 0;
    std::string previous;
    read_value("/DAQ/Status/Runlog/StatusRunNumber", TID_INT32,
               &status_run);
    if (status_run == run_number) {
      read_string("/DAQ/Status/Runlog/DAQStatus", &previous);
    }
    const std::string current =
        daq_monitor::severity_name(evaluation.global_severity);
    const auto rank = [](const std::string& value) {
      return value == "ERROR" ? 2 : value == "WARNING" ? 1 : 0;
    };
    if (status_run != run_number || rank(current) > rank(previous)) {
      write_string("/DAQ/Status/Runlog/DAQStatus", current);
      write_string("/DAQ/Status/Runlog/DAQSummary",
                   evaluation.global_summary);
    }
    write_value("/DAQ/Status/Runlog/StatusRunNumber", &run_number,
                sizeof(run_number), TID_INT32);
  }

  bool ok = true;
#define PUBLISH(path, value, type) \
  ok = write_value(path, &(value), sizeof(value), type) && ok
  ok = write_string("/DAQ/Status/Global/Severity",
                    daq_monitor::severity_name(evaluation.global_severity)) &&
       ok;
  ok = write_string("/DAQ/Status/Global/Summary", evaluation.global_summary) &&
       ok;
  PUBLISH("/DAQ/Status/Run/RunNumber", run_number, TID_INT32);
  PUBLISH("/DAQ/Status/Run/State", run_state, TID_INT32);
  PUBLISH("/DAQ/Status/Run/DurationSec", duration_sec, TID_QWORD);
  ok = write_string("/DAQ/Status/Disk/Path", gDiskCache.path) && ok;
  PUBLISH("/DAQ/Status/Disk/FreeGB", gDiskCache.free_gb, TID_DOUBLE);
  PUBLISH("/DAQ/Status/Disk/TotalGB", gDiskCache.total_gb, TID_DOUBLE);
  ok = write_string("/DAQ/Status/Disk/Severity",
                    daq_monitor::severity_name(evaluation.disk.severity)) &&
       ok;
  PUBLISH("/DAQ/Status/Logger/Connected", logger_connected, TID_BOOL);
  ok = write_string("/DAQ/Status/Logger/CurrentFilename",
                    logger.current_filename) && ok;
  ok = write_string("/DAQ/Status/Logger/Severity",
                    daq_monitor::severity_name(evaluation.logger.severity)) &&
       ok;

  PUBLISH("/DAQ/Status/Frontends/VME/Connected", vme_connected, TID_BOOL);
  const BOOL vme_participating = participation.vme ? TRUE : FALSE;
  PUBLISH("/DAQ/Status/Frontends/VME/Participating", vme_participating,
          TID_BOOL);
  PUBLISH("/DAQ/Status/Frontends/VME/StatusFresh", vme_status_fresh,
          TID_BOOL);
  PUBLISH("/DAQ/Status/Frontends/VME/ConfigurationOK",
          vme_configuration_ok, TID_BOOL);
  PUBLISH("/DAQ/Status/Frontends/VME/ConfigurationRunNumber",
          vme_configuration_run_number, TID_INT);
  PUBLISH("/DAQ/Status/Frontends/VME/ConfigurationCheckedUnix",
          vme_configuration_checked_unix, TID_QWORD);
  ok = write_string("/DAQ/Status/Frontends/VME/Severity",
                    daq_monitor::severity_name(evaluation.vme.severity)) &&
       ok;
  ok = write_string("/DAQ/Status/Frontends/VME/Reason",
                    evaluation.vme.reason) && ok;
  PUBLISH("/DAQ/Status/Frontends/VME/EventSlipCount", event_slip_count,
          TID_QWORD);
  PUBLISH("/DAQ/Status/Frontends/VME/MalformedEventCount",
          malformed_event_count, TID_QWORD);
  PUBLISH("/DAQ/Status/Frontends/VME/TimeoutCount", vme_timeout_count,
          TID_QWORD);
  PUBLISH("/DAQ/Status/Frontends/VME/EventContentErrorCount",
          vme_event_content_error_count, TID_QWORD);
  PUBLISH("/DAQ/Status/Frontends/VME/SizeErrorCount", size_error_count,
          TID_QWORD);
  PUBLISH("/DAQ/Status/Frontends/VME/ChannelMaskErrorCount",
          channel_mask_error_count, TID_QWORD);
  PUBLISH("/DAQ/Status/Frontends/VME/CounterDiscontinuityCount",
          counter_discontinuity_count, TID_QWORD);

  PUBLISH("/DAQ/Status/Frontends/EASIROC/Connected", easiroc_connected,
          TID_BOOL);
  const BOOL easiroc_participating =
      participation.easiroc ? TRUE : FALSE;
  PUBLISH("/DAQ/Status/Frontends/EASIROC/Participating",
          easiroc_participating, TID_BOOL);
  PUBLISH("/DAQ/Status/Frontends/EASIROC/StatusFresh",
          easiroc_status_fresh, TID_BOOL);
  PUBLISH("/DAQ/Status/Frontends/EASIROC/ConfigurationOK",
          easiroc_configuration_ok, TID_BOOL);
  PUBLISH("/DAQ/Status/Frontends/EASIROC/ConfigurationRunNumber",
          easiroc_configuration_run_number, TID_INT);
  PUBLISH("/DAQ/Status/Frontends/EASIROC/ConfigurationCheckedUnix",
          easiroc_configuration_checked_unix, TID_QWORD);
  ok = write_string("/DAQ/Status/Frontends/EASIROC/Severity",
                    daq_monitor::severity_name(evaluation.easiroc.severity)) &&
       ok;
  ok = write_string("/DAQ/Status/Frontends/EASIROC/Reason",
                    evaluation.easiroc.reason) && ok;
  PUBLISH("/DAQ/Status/Frontends/EASIROC/AcquisitionRunning",
          easiroc_running, TID_BOOL);
  PUBLISH("/DAQ/Status/Frontends/EASIROC/AcquisitionFault", easiroc_fault,
          TID_BOOL);
  PUBLISH("/DAQ/Status/Frontends/EASIROC/EventCounter",
          easiroc_event_counter, TID_QWORD);
  PUBLISH("/DAQ/Status/Frontends/EASIROC/DecodeErrorCount",
          decode_error_count, TID_QWORD);
  PUBLISH("/DAQ/Status/Frontends/EASIROC/TimeoutCount",
          easiroc_timeout_count, TID_QWORD);
  PUBLISH("/DAQ/Status/Frontends/EASIROC/OverflowCount", overflow_count,
          TID_QWORD);
  PUBLISH("/DAQ/Status/Frontends/EASIROC/EventContentErrorCount",
          easiroc_event_content_error_count, TID_QWORD);
  PUBLISH("/DAQ/Status/Global/LastUpdateUnix", now_unix, TID_QWORD);
#undef PUBLISH
  daq_monitor::CanStartEvaluation can_start = evaluation.can_start;
  if (!ok) {
    raw_status.monitor_status_fresh = false;
    can_start = daq_monitor::evaluate_can_start(raw_status);
  }
  return publish_can_start(can_start) && ok;
}

INT validate_start_transition(INT run_number, char* error) {
  daq_monitor::CanStartEvaluation evaluation;
  BOOL can_start = FALSE;
  BOOL vme_connected = FALSE;
  BOOL easiroc_connected = FALSE;

  // A successful synchronous collection makes freshness explicit without
  // sleeping or polling while this transition callback is running.
  if (!publish_status(true) ||
      !read_value("/DAQ/Status/Global/CanStart", TID_BOOL, &can_start) ||
      !read_string("/DAQ/Status/Global/CanStartReason", &evaluation.reason) ||
      !read_value("/DAQ/Status/Frontends/VME/Connected", TID_BOOL,
                  &vme_connected) ||
      !read_value("/DAQ/Status/Frontends/EASIROC/Connected", TID_BOOL,
                  &easiroc_connected)) {
    evaluation = {false, "Monitor status unavailable"};
  } else {
    evaluation.allowed = can_start != FALSE;
  }

  if (evaluation.allowed &&
      !record_run_participation(run_number, vme_connected != FALSE,
                                easiroc_connected != FALSE)) {
    evaluation = {false, "Cannot record frontend participation"};
  }

  if (evaluation.allowed) {
    cm_msg(MINFO, kClientName,
           "Run %d participants: VME=%s EASIROC=%s", run_number,
           vme_connected != FALSE ? "yes" : "no",
           easiroc_connected != FALSE ? "yes" : "no");
    if (error != nullptr)
      error[0] = '\0';
    return CM_SUCCESS;
  }

  if (error != nullptr)
    std::snprintf(error, kTransitionErrorCapacity, "%s",
                  evaluation.reason.c_str());
  cm_msg(MERROR, kClientName, "Rejecting START for run %d: %s", run_number,
         evaluation.reason.c_str());
  return CM_TRANSITION_CANCELED;
}

// Logger reads EOR links at sequence 800. Capture the finished run first.
INT capture_runlog_eor(INT run_number, char*) {
  const daq_monitor::RunParticipation participation = read_run_participation();
  if (!participation.valid || participation.run_number != run_number) {
    cm_msg(MERROR, kClientName, "No participation record for EOR run %d", run_number);
    return CM_SUCCESS;  // Never prevent STOP from completing.
  }
  DWORD start = 0, stop = 0;
  read_value("/Runinfo/Start time binary", TID_DWORD, &start);
  read_value("/Runinfo/Stop time binary", TID_DWORD, &stop);
  const std::uint64_t elapsed = stop >= start ? stop - start : 0;
  double vme_sent = 0, easiroc_sent = 0;
  read_value("/Equipment/VME/Statistics/Events sent", TID_DOUBLE, &vme_sent);
  read_value("/Equipment/NIM-EASIROC Physics/Statistics/Events sent",
             TID_DOUBLE, &easiroc_sent);
  INT count_run = 0;
  read_value("/DAQ/Status/Runlog/CountRunNumber", TID_INT32, &count_run);
  if (count_run == run_number) {
    double observed = 0;
    if (read_value("/DAQ/Status/Runlog/ObservedVMEEvents", TID_DOUBLE,
                   &observed)) vme_sent = std::max(vme_sent, observed);
    if (read_value("/DAQ/Status/Runlog/ObservedEASIROCEvents", TID_DOUBLE,
                   &observed)) easiroc_sent = std::max(easiroc_sent, observed);
  }
  const std::int64_t vme_events =
      daq_monitor::runlog_event_count(participation.vme, vme_sent, 0);
  const std::int64_t easiroc_events =
      daq_monitor::runlog_event_count(participation.easiroc, easiroc_sent, 0);
  std::uint64_t slips = 0;
  if (participation.vme)
    read_value("/Equipment/VME/Variables/RunCounters/EventSlipCount",
               TID_QWORD, &slips);
  publish_status();
  bool ok = true;
  ok = write_value("/DAQ/Status/Runlog/DurationSec", &elapsed,
                   sizeof(elapsed), TID_QWORD) && ok;
  ok = write_value("/DAQ/Status/Runlog/VMEEvents", &vme_events,
                   sizeof(vme_events), TID_INT64) && ok;
  ok = write_value("/DAQ/Status/Runlog/EASIROCEvents", &easiroc_events,
                   sizeof(easiroc_events), TID_INT64) && ok;
  ok = write_value("/DAQ/Status/Runlog/EventSlipCount", &slips,
                   sizeof(slips), TID_QWORD) && ok;
  if (!ok)
    cm_msg(MERROR, kClientName, "Cannot capture EOR run %d for JSON runlog", run_number);
  return CM_SUCCESS;
}

void print_usage(const char* program) {
  std::printf("Usage: %s [-h host] [-e experiment]\n", program);
}

}  // namespace

int main(int argc, char** argv) {
  char host_name[HOST_NAME_LENGTH] = {};
  char experiment_name[NAME_LENGTH] = {};
  cm_get_environment(host_name, sizeof(host_name), experiment_name,
                     sizeof(experiment_name));

  for (int index = 1; index < argc; ++index) {
    if ((std::strcmp(argv[index], "-h") == 0 ||
         std::strcmp(argv[index], "-e") == 0) &&
        index + 1 < argc) {
      char* destination = std::strcmp(argv[index], "-h") == 0
                              ? host_name
                              : experiment_name;
      const std::size_t capacity = std::strcmp(argv[index], "-h") == 0
                                       ? sizeof(host_name)
                                       : sizeof(experiment_name);
      std::snprintf(destination, capacity, "%s", argv[++index]);
    } else {
      print_usage(argv[0]);
      return 1;
    }
  }

  const INT connect_status = cm_connect_experiment(
      host_name, experiment_name, kClientName, nullptr);
  if (connect_status != CM_SUCCESS) {
    std::fprintf(stderr, "Cannot connect %s to MIDAS: status %d\n",
                 kClientName, connect_status);
    return 1;
  }

  const std::string actual_client_name = cm_get_client_name();
  if (actual_client_name != kClientName) {
    std::fprintf(stderr,
                 "MIDAS registered client as %s instead of %s; another "
                 "monitor may already be running\n",
                 actual_client_name.c_str(), kClientName);
    cm_disconnect_experiment();
    return 1;
  }

  cm_get_experiment_database(&gDatabase, nullptr);
  if (!initialize_alarm_classes() || !validate_global_alarm_class()) {
    std::fprintf(stderr,
                 "Cannot initialize DAQ alarms with automatic Stop disabled\n");
    cm_disconnect_experiment();
    return 1;
  }
  const INT transition_status = cm_register_transition(
      TR_START, validate_start_transition,
      daq_monitor::kStartTransitionSequence);
  if (transition_status != CM_SUCCESS) {
    std::fprintf(stderr,
                 "Cannot register START validation at sequence %d: status "
                 "%d\n",
                 daq_monitor::kStartTransitionSequence, transition_status);
    cm_disconnect_experiment();
    return 1;
  }
  const INT stop_status = cm_register_transition(TR_STOP, capture_runlog_eor, 700);
  if (stop_status != CM_SUCCESS) {
    std::fprintf(stderr, "Cannot register EOR runlog capture: status %d\n", stop_status);
    cm_disconnect_experiment();
    return 1;
  }
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  cm_msg(MINFO, kClientName,
         "Started DAQ status collection and component alarm synchronization");

  INT yield_status = CM_SUCCESS;
  while (!gStopRequested) {
    if (publish_status() && !update_alarms())
      cm_msg(MERROR, kClientName, "DAQ alarm synchronization failed");
    yield_status = cm_yield(kUpdatePeriodMs);
    if (yield_status == RPC_SHUTDOWN || yield_status == SS_ABORT)
      break;
  }

  cm_disconnect_experiment();
  return 0;
}
