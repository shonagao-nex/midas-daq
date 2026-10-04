#include "midas.h"
#include "mrpc.h"
#include "alarm_policy.h"
#include "monitor_alarms.h"
#include "monitor_odb_utils.h"
#include "runlog_edit_rpc.h"
#include "status_policy.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <limits>
#include <optional>
#include <spawn.h>
#include <string>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace {

using daq_monitor::read_string;
using daq_monitor::read_value;

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

//************************************//
// Hold monitor disk cache and component alarm states
//************************************//
struct MonitorState {
  DiskCache disk;
  daq_monitor::MonitorAlarmState alarms;
};
MonitorState gMonitorState;

void handle_signal(int) { gStopRequested = 1; }

bool is_owned_status_path(const char* path) {
  const std::size_t root_length = std::strlen(kStatusRoot);
  return std::strncmp(path, kStatusRoot, root_length) == 0 &&
         (path[root_length] == '\0' || path[root_length] == '/');
}

//************************************//
// Publish monitor-owned status to ODB
//************************************//
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

daq_monitor::RunParticipation read_run_participation() {
  BOOL valid = FALSE;
  INT run_number = 0;
  BOOL vme = FALSE;
  BOOL easiroc = FALSE;
  if (!read_value(gDatabase, "/DAQ/Status/Run/ParticipationValid", TID_BOOL, &valid) ||
      !read_value(gDatabase, "/DAQ/Status/Run/ParticipationRunNumber", TID_INT32,
                  &run_number) ||
      !read_value(gDatabase, "/DAQ/Status/Run/VMEParticipating", TID_BOOL, &vme) ||
      !read_value(gDatabase, "/DAQ/Status/Run/EASIROCParticipating", TID_BOOL,
                  &easiroc))
    return {};
  return {valid != FALSE, run_number, vme != FALSE, easiroc != FALSE};
}

//************************************//
// Record frontend participation for the current run
//************************************//
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
    read_value(gDatabase, (base + "/Settings/Active").c_str(), TID_BOOL, &active);
    read_string(gDatabase, base + "/Settings/Type", &type);
    if (active != FALSE && (type.empty() || equal_ustring(type.c_str(), "Disk")))
      return base;
  }
  return first_channel;
}

LoggerSource read_logger_source() {
  LoggerSource source;
  source.channel = logger_channel_path();
  if (!source.channel.empty())
    read_string(gDatabase, source.channel + "/Settings/Current filename",
                &source.current_filename);
  read_string(gDatabase, "/Logger/Data dir", &source.data_directory);
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

//************************************//
// Refresh cached logger disk information
//************************************//
void update_disk_cache(const LoggerSource& source, DWORD now_ms) {
  const std::string path = disk_path_for(source);
  const DWORD elapsed = now_ms - gMonitorState.disk.last_update_ms;
  if (gMonitorState.disk.initialized && path == gMonitorState.disk.path &&
      elapsed < kDiskUpdatePeriodMs)
    return;

  gMonitorState.disk.path = path;
  const double free_bytes = ss_disk_free(path.c_str());
  gMonitorState.disk.free_gb = free_bytes < 0.0 ? -1.0 : free_bytes / kBytesPerGB;
  struct statvfs filesystem {};
  if (statvfs(path.c_str(), &filesystem) == 0) {
    gMonitorState.disk.total_gb =
        static_cast<double>(filesystem.f_blocks) * filesystem.f_frsize /
        kBytesPerGB;
  } else {
    gMonitorState.disk.total_gb = -1.0;
  }
  gMonitorState.disk.last_update_ms = now_ms;
  gMonitorState.disk.initialized = true;
}

struct StatusInputs {
  INT run_number;
  INT run_state;
  INT transition_in_progress;
  DWORD start_time;
  DWORD stop_time;
  std::uint64_t previous_update_unix;
  bool collection_ok;
  std::uint64_t now_unix;
  std::uint64_t duration_sec;
  BOOL vme_connected;
  BOOL vme_status_fresh;
  BOOL easiroc_connected;
  BOOL easiroc_status_fresh;
  BOOL logger_connected;
  BOOL vme_frontend_enabled;
  BOOL easiroc_frontend_enabled;
  BOOL vme_configuration_ok;
  INT vme_configuration_run_number;
  std::uint64_t vme_configuration_checked_unix;
  BOOL easiroc_configuration_ok;
  INT easiroc_configuration_run_number;
  std::uint64_t easiroc_configuration_checked_unix;
  std::uint64_t event_slip_count;
  std::uint64_t malformed_event_count;
  std::uint64_t vme_timeout_count;
  std::uint64_t counter_discontinuity_count;
  std::uint64_t size_error_count;
  std::uint64_t channel_mask_error_count;
  BOOL v1720_enabled;
  BOOL v1720_running;
  std::uint64_t vme_event_content_error_count;
  BOOL easiroc_running;
  BOOL easiroc_enabled;
  BOOL easiroc_fault;
  std::uint64_t easiroc_event_counter;
  std::uint64_t decode_error_count;
  std::uint64_t easiroc_timeout_count;
  std::uint64_t overflow_count;
  std::uint64_t easiroc_event_content_error_count;
  LoggerSource logger;
  daq_monitor::RunParticipation run_participation;
};

struct StatusDecision {
  daq_monitor::RawStatus raw_status;
  daq_monitor::ActiveParticipation participation;
  daq_monitor::StatusEvaluation evaluation;
};

//************************************//
// Collect run state, timestamps, and monitor freshness inputs
//************************************//
void collect_runinfo_inputs(StatusInputs* inputs) {
  auto& run_number = inputs->run_number;
  auto& run_state = inputs->run_state;
  auto& transition_in_progress = inputs->transition_in_progress;
  auto& start_time = inputs->start_time;
  auto& stop_time = inputs->stop_time;
  auto& previous_update_unix = inputs->previous_update_unix;
  auto& collection_ok = inputs->collection_ok;
  auto& now_unix = inputs->now_unix;
  auto& duration_sec = inputs->duration_sec;
  collection_ok =
      read_value(gDatabase, "/Runinfo/Run number", TID_INT32, &run_number) &&
      collection_ok;
  collection_ok = read_value(gDatabase, "/Runinfo/State", TID_INT32, &run_state) &&
                  collection_ok;
  // MIDAS writes the transition number before invoking callbacks and clears
  // it only after updating Runinfo/State. If this read fails, remain fail-safe
  // and do not suppress any acquisition-state error.
  read_value(gDatabase, "/Runinfo/Transition in progress", TID_INT32,
             &transition_in_progress);
  collection_ok = read_value(gDatabase, "/Runinfo/Start time binary", TID_DWORD,
                             &start_time) && collection_ok;
  read_value(gDatabase, "/Runinfo/Stop time binary", TID_DWORD, &stop_time);
  read_value(gDatabase, "/DAQ/Status/Global/LastUpdateUnix", TID_QWORD,
             &previous_update_unix);

  const std::time_t now = std::time(nullptr);
  now_unix = now < 0 ? 0 : static_cast<std::uint64_t>(now);
  duration_sec = 0;
  if (run_state == STATE_RUNNING && start_time != 0 &&
      now_unix >= static_cast<std::uint64_t>(start_time))
    duration_sec = now_unix - static_cast<std::uint64_t>(start_time);
  else if (run_state == STATE_STOPPED && start_time != 0 &&
           stop_time >= start_time)
    duration_sec = stop_time - start_time;
}

//************************************//
// Collect frontend and logger client watchdog health
//************************************//
void collect_client_health_inputs(StatusInputs* inputs) {
  auto& vme_connected = inputs->vme_connected;
  auto& vme_status_fresh = inputs->vme_status_fresh;
  auto& easiroc_connected = inputs->easiroc_connected;
  auto& easiroc_status_fresh = inputs->easiroc_status_fresh;
  auto& logger_connected = inputs->logger_connected;
  const ClientHealth vme_health = client_health(kVmeClientName);
  const ClientHealth easiroc_health = client_health(kEasirocClientName);
  const ClientHealth logger_health = client_health(kLoggerClientName);
  vme_connected = vme_health.connected ? TRUE : FALSE;
  vme_status_fresh = vme_health.status_fresh ? TRUE : FALSE;
  easiroc_connected = easiroc_health.connected ? TRUE : FALSE;
  easiroc_status_fresh = easiroc_health.status_fresh ? TRUE : FALSE;
  logger_connected = logger_health.status_fresh ? TRUE : FALSE;
}

//************************************//
// Collect both frontends' BOR configuration records
//************************************//
void collect_configuration_inputs(StatusInputs* inputs) {
  auto& vme_configuration_ok = inputs->vme_configuration_ok;
  auto& vme_configuration_run_number = inputs->vme_configuration_run_number;
  auto& vme_configuration_checked_unix = inputs->vme_configuration_checked_unix;
  auto& easiroc_configuration_ok = inputs->easiroc_configuration_ok;
  auto& easiroc_configuration_run_number =
      inputs->easiroc_configuration_run_number;
  auto& easiroc_configuration_checked_unix =
      inputs->easiroc_configuration_checked_unix;
  // Configuration is a BOR result, not an input to the sequence-400
  // pre-start policy. Missing or old values remain false/zero for monitoring;
  // each participating frontend reports BOR failure through its own
  // sequence-500 transition callback.
  read_value(gDatabase, "/Equipment/VME/Variables/Frontend/ConfigurationOK", TID_BOOL,
             &vme_configuration_ok);
  read_value(gDatabase, "/Equipment/VME/Variables/Frontend/ConfigurationRunNumber",
             TID_INT, &vme_configuration_run_number);
  read_value(gDatabase, "/Equipment/VME/Variables/Frontend/ConfigurationCheckedUnix",
             TID_QWORD, &vme_configuration_checked_unix);
  read_value(gDatabase, "/Equipment/EASIROC/Variables/Frontend/ConfigurationOK",
             TID_BOOL, &easiroc_configuration_ok);
  read_value(gDatabase,
      "/Equipment/EASIROC/Variables/Frontend/ConfigurationRunNumber",
      TID_INT, &easiroc_configuration_run_number);
  read_value(gDatabase,
      "/Equipment/EASIROC/Variables/Frontend/ConfigurationCheckedUnix",
      TID_QWORD, &easiroc_configuration_checked_unix);
}

//************************************//
// Collect VME acquisition state and run counters
//************************************//
void collect_vme_inputs(StatusInputs* inputs) {
  auto& event_slip_count = inputs->event_slip_count;
  auto& malformed_event_count = inputs->malformed_event_count;
  auto& vme_timeout_count = inputs->vme_timeout_count;
  auto& counter_discontinuity_count = inputs->counter_discontinuity_count;
  auto& size_error_count = inputs->size_error_count;
  auto& channel_mask_error_count = inputs->channel_mask_error_count;
  auto& v1720_enabled = inputs->v1720_enabled;
  auto& v1720_running = inputs->v1720_running;
  auto& vme_event_content_error_count = inputs->vme_event_content_error_count;
  v1720_enabled = TRUE;
  read_value(gDatabase, "/Equipment/VME/Variables/V1720E/EnabledForRun", TID_BOOL,
             &v1720_enabled);
  read_value(gDatabase, "/Equipment/VME/Variables/V1720E/Running", TID_BOOL,
             &v1720_running);
  read_value(gDatabase, "/Equipment/VME/Variables/RunCounters/EventSlipCount",
             TID_QWORD, &event_slip_count);
  read_value(gDatabase,
      "/Equipment/VME/Variables/RunCounters/V1720EMalformedEventCount",
      TID_QWORD, &malformed_event_count);
  read_value(gDatabase, "/Equipment/VME/Variables/RunCounters/V1720EReadTimeoutCount",
             TID_QWORD, &vme_timeout_count);
  read_value(gDatabase,
      "/Equipment/VME/Variables/RunCounters/V1720ECounterDiscontinuityCount",
      TID_QWORD, &counter_discontinuity_count);
  read_value(gDatabase, "/Equipment/VME/Variables/RunCounters/V1720ESizeErrorCount",
             TID_QWORD, &size_error_count);
  read_value(gDatabase,
      "/Equipment/VME/Variables/RunCounters/V1720EChannelMaskErrorCount",
      TID_QWORD, &channel_mask_error_count);
  vme_event_content_error_count =
      add_saturating(size_error_count, channel_mask_error_count);
}

//************************************//
// Collect EASIROC acquisition state and run counters
//************************************//
void collect_easiroc_inputs(StatusInputs* inputs) {
  auto& easiroc_running = inputs->easiroc_running;
  auto& easiroc_enabled = inputs->easiroc_enabled;
  auto& easiroc_fault = inputs->easiroc_fault;
  auto& easiroc_event_counter = inputs->easiroc_event_counter;
  auto& decode_error_count = inputs->decode_error_count;
  auto& easiroc_timeout_count = inputs->easiroc_timeout_count;
  auto& overflow_count = inputs->overflow_count;
  auto& easiroc_event_content_error_count =
      inputs->easiroc_event_content_error_count;
  easiroc_enabled = TRUE;
  read_value(gDatabase, "/Equipment/EASIROC/Variables/EnabledForRun", TID_BOOL,
             &easiroc_enabled);
  read_value(gDatabase, "/Equipment/EASIROC/Variables/AcquisitionRunning", TID_BOOL,
             &easiroc_running);
  read_value(gDatabase, "/Equipment/EASIROC/Variables/AcquisitionFault", TID_BOOL,
             &easiroc_fault);
  read_value(gDatabase, "/Equipment/EASIROC/Variables/EventCounter", TID_QWORD,
             &easiroc_event_counter);
  read_value(gDatabase, "/Equipment/EASIROC/Variables/Statistics/DecodeErrorCount",
             TID_QWORD, &decode_error_count);
  read_value(gDatabase, "/Equipment/EASIROC/Variables/Statistics/ReceiveTimeoutCount",
             TID_QWORD, &easiroc_timeout_count);
  read_value(gDatabase, "/Equipment/EASIROC/Variables/Statistics/ADCOverflowCount",
             TID_QWORD, &overflow_count);
  read_value(gDatabase,
      "/Equipment/EASIROC/Variables/Statistics/EventContentErrorCount",
      TID_QWORD, &easiroc_event_content_error_count);
}

//************************************//
// Collect the logger source and refresh the cached disk measurements
//************************************//
void collect_logger_disk_inputs(StatusInputs* inputs) {
  inputs->logger = read_logger_source();
  update_disk_cache(inputs->logger, ss_millitime());
}

//************************************//
// Read monitor status inputs from ODB and system services
//************************************//
StatusInputs collect_status_inputs() {
  StatusInputs inputs{};
  inputs.collection_ok = true;
  collect_runinfo_inputs(&inputs);
  collect_client_health_inputs(&inputs);
  inputs.vme_frontend_enabled = TRUE;
  inputs.easiroc_frontend_enabled = TRUE;
  read_value(gDatabase, "/Equipment/VME/Settings/FrontendEnabled",
             TID_BOOL, &inputs.vme_frontend_enabled);
  read_value(gDatabase, "/Equipment/EASIROC/Settings/FrontendEnabled",
             TID_BOOL, &inputs.easiroc_frontend_enabled);
  collect_configuration_inputs(&inputs);
  collect_vme_inputs(&inputs);
  collect_easiroc_inputs(&inputs);
  collect_logger_disk_inputs(&inputs);
  inputs.run_participation = read_run_participation();
  return inputs;
}

//************************************//
// Evaluate DAQ status and start policy from collected inputs
//************************************//
StatusDecision evaluate_status_inputs(const StatusInputs& inputs,
                                      bool synchronous_start_check) {
  const auto& run_state = inputs.run_state;
  const auto& transition_in_progress = inputs.transition_in_progress;
  const auto& collection_ok = inputs.collection_ok;
  const auto& now_unix = inputs.now_unix;
  const auto& previous_update_unix = inputs.previous_update_unix;
  const auto& run_number = inputs.run_number;
  const auto& vme_connected = inputs.vme_connected;
  const auto& vme_status_fresh = inputs.vme_status_fresh;
  const auto& easiroc_connected = inputs.easiroc_connected;
  const auto& easiroc_status_fresh = inputs.easiroc_status_fresh;
  const auto& logger_connected = inputs.logger_connected;
  const auto& vme_configuration_ok = inputs.vme_configuration_ok;
  const auto& vme_configuration_run_number = inputs.vme_configuration_run_number;
  const auto& vme_configuration_checked_unix = inputs.vme_configuration_checked_unix;
  const auto& easiroc_configuration_ok = inputs.easiroc_configuration_ok;
  const auto& easiroc_configuration_run_number = inputs.easiroc_configuration_run_number;
  const auto& easiroc_configuration_checked_unix = inputs.easiroc_configuration_checked_unix;
  const auto& event_slip_count = inputs.event_slip_count;
  const auto& malformed_event_count = inputs.malformed_event_count;
  const auto& vme_timeout_count = inputs.vme_timeout_count;
  const auto& counter_discontinuity_count = inputs.counter_discontinuity_count;
  const auto& size_error_count = inputs.size_error_count;
  const auto& channel_mask_error_count = inputs.channel_mask_error_count;
  const auto& v1720_enabled = inputs.v1720_enabled;
  const auto& v1720_running = inputs.v1720_running;
  const auto& vme_event_content_error_count = inputs.vme_event_content_error_count;
  const auto& easiroc_running = inputs.easiroc_running;
  const auto& easiroc_enabled = inputs.easiroc_enabled;
  const auto& easiroc_fault = inputs.easiroc_fault;
  const auto& decode_error_count = inputs.decode_error_count;
  const auto& easiroc_timeout_count = inputs.easiroc_timeout_count;
  const auto& overflow_count = inputs.overflow_count;
  const auto& easiroc_event_content_error_count = inputs.easiroc_event_content_error_count;
  daq_monitor::RawStatus raw_status;
  raw_status.run_state = daq_monitor::policy_run_state(run_state);
  raw_status.stop_transition_in_progress =
      transition_in_progress == TR_STOP;
  raw_status.monitor_status_fresh =
      collection_ok &&
      (synchronous_start_check ||
       timestamp_is_fresh(now_unix, previous_update_unix));
  raw_status.disk_free_gb = gMonitorState.disk.free_gb;
  raw_status.logger_connected = logger_connected != FALSE;
  raw_status.vme_requested = inputs.vme_frontend_enabled != FALSE;
  raw_status.easiroc_requested = inputs.easiroc_frontend_enabled != FALSE;
  const daq_monitor::ActiveParticipation participation =
      daq_monitor::resolve_run_participation(
          raw_status.run_state, run_number, inputs.run_participation);
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

  return {raw_status, participation, evaluation};
}

//************************************//
// Preserve the largest observed event counts for the runlog
//************************************//
void publish_observed_event_counts(const StatusInputs& inputs,
                                   const StatusDecision& decision) {
  const auto& run_state = inputs.run_state;
  const auto& run_number = inputs.run_number;
  const auto& participation = decision.participation;
  // Preserve the largest observed MIDAS event count if a participating
  // frontend disconnects before EOR. The record is keyed by run number.
  if (run_state == STATE_RUNNING || run_state == STATE_PAUSED) {
    INT count_run = 0;
    double previous_vme = 0, previous_easiroc = 0;
    read_value(gDatabase, "/DAQ/Status/Runlog/CountRunNumber", TID_INT32, &count_run);
    if (count_run == run_number) {
      read_value(gDatabase, "/DAQ/Status/Runlog/ObservedVMEEvents", TID_DOUBLE,
                 &previous_vme);
      read_value(gDatabase, "/DAQ/Status/Runlog/ObservedEASIROCEvents", TID_DOUBLE,
                 &previous_easiroc);
    }
    double current_vme = 0, current_easiroc = 0;
    read_value(gDatabase, "/Equipment/VME/Statistics/Events sent", TID_DOUBLE,
               &current_vme);
    read_value(gDatabase, "/Equipment/NIM-EASIROC Physics/Statistics/Events sent",
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
}

//************************************//
// Preserve the worst DAQ status observed during the run
//************************************//
bool publish_runlog_worst_status(const StatusInputs& inputs,
                                 const StatusDecision& decision) {
  const auto& run_state = inputs.run_state;
  const auto& run_number = inputs.run_number;
  const auto& evaluation = decision.evaluation;
  bool ok = true;
  // Keep the worst observed run status so a transient disconnect remains
  // visible in the EOR record even if the frontend reconnects before STOP.
  if (run_state == STATE_RUNNING || run_state == STATE_PAUSED) {
    INT status_run = 0;
    std::string previous;
    read_value(gDatabase, "/DAQ/Status/Runlog/StatusRunNumber", TID_INT32,
               &status_run);
    if (status_run == run_number) {
      read_string(gDatabase, "/DAQ/Status/Runlog/DAQStatus", &previous);
    }
    const std::string current =
        daq_monitor::severity_name(evaluation.global_severity);
    const auto rank = [](const std::string& value) {
      return value == "ERROR" ? 2 : value == "WARNING" ? 1 : 0;
    };
    if (status_run != run_number || rank(current) > rank(previous)) {
      ok = write_string("/DAQ/Status/Runlog/DAQStatus", current) && ok;
      ok = write_string("/DAQ/Status/Runlog/DAQSummary",
                        evaluation.global_summary) && ok;
    }
    ok = write_value("/DAQ/Status/Runlog/StatusRunNumber", &run_number,
                     sizeof(run_number), TID_INT32) && ok;
  }
  return ok;
}

//************************************//
// Publish overall DAQ severity and run state
//************************************//
bool publish_overall_run_status(const StatusInputs& inputs,
                                const StatusDecision& decision) {
  const auto& run_state = inputs.run_state;
  const auto& run_number = inputs.run_number;
  const auto& duration_sec = inputs.duration_sec;
  const auto& evaluation = decision.evaluation;
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
#undef PUBLISH
  return ok;
}

//************************************//
// Publish disk and logger status
//************************************//
bool publish_disk_logger_status(const StatusInputs& inputs,
                                const StatusDecision& decision) {
  const auto& logger = inputs.logger;
  const auto& logger_connected = inputs.logger_connected;
  const auto& evaluation = decision.evaluation;
  bool ok = true;
#define PUBLISH(path, value, type) \
  ok = write_value(path, &(value), sizeof(value), type) && ok
  ok = write_string("/DAQ/Status/Disk/Path", gMonitorState.disk.path) && ok;
  PUBLISH("/DAQ/Status/Disk/FreeGB", gMonitorState.disk.free_gb, TID_DOUBLE);
  PUBLISH("/DAQ/Status/Disk/TotalGB", gMonitorState.disk.total_gb, TID_DOUBLE);
  ok = write_string("/DAQ/Status/Disk/Severity",
                    daq_monitor::severity_name(evaluation.disk.severity)) &&
       ok;
  PUBLISH("/DAQ/Status/Logger/Connected", logger_connected, TID_BOOL);
  ok = write_string("/DAQ/Status/Logger/CurrentFilename",
                    logger.current_filename) && ok;
  ok = write_string("/DAQ/Status/Logger/Severity",
                    daq_monitor::severity_name(evaluation.logger.severity)) &&
       ok;
#undef PUBLISH
  return ok;
}

//************************************//
// Publish VME frontend status and counters
//************************************//
bool publish_vme_status(const StatusInputs& inputs,
                        const StatusDecision& decision) {
  const auto& vme_connected = inputs.vme_connected;
  const auto& vme_status_fresh = inputs.vme_status_fresh;
  const auto& vme_configuration_ok = inputs.vme_configuration_ok;
  const auto& vme_configuration_run_number =
      inputs.vme_configuration_run_number;
  const auto& vme_configuration_checked_unix =
      inputs.vme_configuration_checked_unix;
  const auto& event_slip_count = inputs.event_slip_count;
  const auto& malformed_event_count = inputs.malformed_event_count;
  const auto& vme_timeout_count = inputs.vme_timeout_count;
  const auto& vme_event_content_error_count =
      inputs.vme_event_content_error_count;
  const auto& size_error_count = inputs.size_error_count;
  const auto& channel_mask_error_count = inputs.channel_mask_error_count;
  const auto& counter_discontinuity_count =
      inputs.counter_discontinuity_count;
  const auto& participation = decision.participation;
  const auto& evaluation = decision.evaluation;
  bool ok = true;
#define PUBLISH(path, value, type) \
  ok = write_value(path, &(value), sizeof(value), type) && ok
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
#undef PUBLISH
  return ok;
}

//************************************//
// Publish EASIROC frontend status and counters
//************************************//
bool publish_easiroc_status(const StatusInputs& inputs,
                            const StatusDecision& decision) {
  const auto& easiroc_connected = inputs.easiroc_connected;
  const auto& easiroc_status_fresh = inputs.easiroc_status_fresh;
  const auto& easiroc_configuration_ok = inputs.easiroc_configuration_ok;
  const auto& easiroc_configuration_run_number =
      inputs.easiroc_configuration_run_number;
  const auto& easiroc_configuration_checked_unix =
      inputs.easiroc_configuration_checked_unix;
  const auto& easiroc_running = inputs.easiroc_running;
  const auto& easiroc_fault = inputs.easiroc_fault;
  const auto& easiroc_event_counter = inputs.easiroc_event_counter;
  const auto& decode_error_count = inputs.decode_error_count;
  const auto& easiroc_timeout_count = inputs.easiroc_timeout_count;
  const auto& overflow_count = inputs.overflow_count;
  const auto& easiroc_event_content_error_count =
      inputs.easiroc_event_content_error_count;
  const auto& participation = decision.participation;
  const auto& evaluation = decision.evaluation;
  bool ok = true;
#define PUBLISH(path, value, type) \
  ok = write_value(path, &(value), sizeof(value), type) && ok
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
#undef PUBLISH
  return ok;
}

//************************************//
// Publish the timestamp of the latest monitor status update
//************************************//
bool publish_status_last_update(const StatusInputs& inputs) {
  const auto& now_unix = inputs.now_unix;
  return write_value("/DAQ/Status/Global/LastUpdateUnix", &now_unix,
                     sizeof(now_unix), TID_QWORD);
}

//************************************//
// Publish evaluated status and start permission to ODB
//************************************//
bool publish_status_outputs(const StatusInputs& inputs,
                            const StatusDecision& decision) {
  bool ok = publish_runlog_worst_status(inputs, decision);
  ok = publish_overall_run_status(inputs, decision) && ok;
  ok = publish_disk_logger_status(inputs, decision) && ok;
  ok = publish_vme_status(inputs, decision) && ok;
  ok = publish_easiroc_status(inputs, decision) && ok;
  ok = publish_status_last_update(inputs) && ok;
  daq_monitor::CanStartEvaluation can_start = decision.evaluation.can_start;
  if (!ok) {
    auto raw_status = decision.raw_status;
    raw_status.monitor_status_fresh = false;
    can_start = daq_monitor::evaluate_can_start(raw_status);
  }
  return publish_can_start(can_start) && ok;
}

//************************************//
// Collect, evaluate, and publish the current DAQ status
//************************************//
bool publish_status(bool synchronous_start_check = false) {
  const StatusInputs inputs = collect_status_inputs();
  const StatusDecision decision =
      evaluate_status_inputs(inputs, synchronous_start_check);
  publish_observed_event_counts(inputs, decision);
  return publish_status_outputs(inputs, decision);
}

//************************************//
// Reject START when DAQ prerequisites are not met
//************************************//
INT validate_start_transition(INT run_number, char* error) {
  daq_monitor::CanStartEvaluation evaluation;
  BOOL can_start = FALSE;
  BOOL vme_connected = FALSE;
  BOOL easiroc_connected = FALSE;
  BOOL vme_enabled = FALSE;
  BOOL easiroc_enabled = FALSE;

  // A successful synchronous collection makes freshness explicit without
  // sleeping or polling while this transition callback is running.
  if (!publish_status(true) ||
      !read_value(gDatabase, "/DAQ/Status/Global/CanStart", TID_BOOL, &can_start) ||
      !read_string(gDatabase, "/DAQ/Status/Global/CanStartReason", &evaluation.reason) ||
      !read_value(gDatabase, "/DAQ/Status/Frontends/VME/Connected", TID_BOOL,
                  &vme_connected) ||
      !read_value(gDatabase, "/DAQ/Status/Frontends/EASIROC/Connected", TID_BOOL,
                  &easiroc_connected) ||
      !read_value(gDatabase, "/Equipment/VME/Settings/FrontendEnabled",
                  TID_BOOL, &vme_enabled) ||
      !read_value(gDatabase, "/Equipment/EASIROC/Settings/FrontendEnabled",
                  TID_BOOL, &easiroc_enabled)) {
    evaluation = {false, "Monitor status unavailable"};
  } else {
    evaluation.allowed = can_start != FALSE;
  }

  if (evaluation.allowed &&
      !record_run_participation(run_number,
                                vme_connected && vme_enabled,
                                easiroc_connected && easiroc_enabled)) {
    evaluation = {false, "Cannot record frontend participation"};
  }

  if (evaluation.allowed) {
    cm_msg(MINFO, kClientName,
           "Run %d participants: VME=%s EASIROC=%s", run_number,
           vme_connected && vme_enabled ? "yes" : "no",
           easiroc_connected && easiroc_enabled ? "yes" : "no");
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

//************************************//
// Capture completed run values for the JSON Runlog
//************************************//
INT capture_runlog_eor(INT run_number, char*) {
  const daq_monitor::RunParticipation participation = read_run_participation();
  if (!participation.valid || participation.run_number != run_number) {
    cm_msg(MERROR, kClientName, "No participation record for EOR run %d", run_number);
    return CM_SUCCESS;  // Never prevent STOP from completing.
  }
  DWORD start = 0, stop = 0;
  bool times_ok = read_value(gDatabase, "/Runinfo/Start time binary", TID_DWORD, &start);
  times_ok = read_value(gDatabase, "/Runinfo/Stop time binary", TID_DWORD, &stop) &&
             times_ok;
  const std::uint64_t elapsed = stop >= start ? stop - start : 0;
  double vme_sent = 0, easiroc_sent = 0;
  read_value(gDatabase, "/Equipment/VME/Statistics/Events sent", TID_DOUBLE, &vme_sent);
  read_value(gDatabase, "/Equipment/NIM-EASIROC Physics/Statistics/Events sent",
             TID_DOUBLE, &easiroc_sent);
  INT count_run = 0;
  read_value(gDatabase, "/DAQ/Status/Runlog/CountRunNumber", TID_INT32, &count_run);
  if (count_run == run_number) {
    double observed = 0;
    if (read_value(gDatabase, "/DAQ/Status/Runlog/ObservedVMEEvents", TID_DOUBLE,
                   &observed)) vme_sent = std::max(vme_sent, observed);
    if (read_value(gDatabase, "/DAQ/Status/Runlog/ObservedEASIROCEvents", TID_DOUBLE,
                   &observed)) easiroc_sent = std::max(easiroc_sent, observed);
  }
  const std::int64_t vme_events =
      daq_monitor::runlog_event_count(participation.vme, vme_sent, 0);
  const std::int64_t easiroc_events =
      daq_monitor::runlog_event_count(participation.easiroc, easiroc_sent, 0);
  // No HUL frontend or run participation record exists yet. -1 means absent.
  // Once HUL is integrated, capture its participation and Events sent here.
  const std::int64_t hul_events = -1;
  std::uint64_t slips = 0;
  if (participation.vme)
    read_value(gDatabase, "/Equipment/VME/Variables/RunCounters/EventSlipCount",
               TID_QWORD, &slips);
  bool ok = publish_status() && times_ok;
  INT status_run = 0;
  std::string daq_status, daq_summary;
  ok = read_value(gDatabase, "/DAQ/Status/Runlog/StatusRunNumber", TID_INT32,
                  &status_run) && status_run == run_number &&
       read_string(gDatabase, "/DAQ/Status/Runlog/DAQStatus", &daq_status) &&
       read_string(gDatabase, "/DAQ/Status/Runlog/DAQSummary", &daq_summary) && ok;
  ok = write_value("/DAQ/Status/Runlog/DurationSec", &elapsed,
                   sizeof(elapsed), TID_QWORD) && ok;
  ok = write_value("/DAQ/Status/Runlog/VMEEvents", &vme_events,
                   sizeof(vme_events), TID_INT64) && ok;
  ok = write_value("/DAQ/Status/Runlog/EASIROCEvents", &easiroc_events,
                   sizeof(easiroc_events), TID_INT64) && ok;
  ok = write_value("/DAQ/Status/Runlog/HULEvents", &hul_events,
                   sizeof(hul_events), TID_INT64) && ok;
  ok = write_value("/DAQ/Status/Runlog/EventSlipCount", &slips,
                   sizeof(slips), TID_QWORD) && ok;
  // The marker identifies the run whose ODB EOR sources are complete;
  // Logger writes their JSON snapshot later in this STOP transition.
  if (ok)
    ok = write_value("/DAQ/Status/Runlog/EORCompleteRunNumber", &run_number,
                     sizeof(run_number), TID_INT32);
  if (!ok)
    cm_msg(MERROR, kClientName, "Cannot capture EOR run %d for JSON runlog", run_number);
  return CM_SUCCESS;
}

//************************************//
// Submit a completed run to the built-in ELOG
//************************************//
void maybe_spawn_run_elog(INT* last_spawned_run) {
  INT state = 0, transition = 0, eor_run = 0, last_attempt = 0, last_run = 0;
  if (!read_value(gDatabase, "/Runinfo/State", TID_INT32, &state) ||
      !read_value(gDatabase, "/Runinfo/Transition in progress", TID_INT32, &transition) ||
      !read_value(gDatabase, "/DAQ/Status/Runlog/EORCompleteRunNumber", TID_INT32,
                  &eor_run) ||
      !read_value(gDatabase, "/Experiment/Run Elog/Last Attempt Run", TID_INT32,
                  &last_attempt) ||
      !read_value(gDatabase, "/Experiment/Run Elog/Last Run", TID_INT32, &last_run) ||
      state != STATE_STOPPED || transition != 0 || eor_run <= 0 ||
      eor_run <= std::max({*last_spawned_run, last_attempt, last_run}))
    return;

  char run_text[32] = {};
  std::snprintf(run_text, sizeof(run_text), "%d", eor_run);
  std::error_code path_error;
  const auto executable = std::filesystem::read_symlink("/proc/self/exe", path_error);
  const auto script = executable.parent_path().parent_path().parent_path() /
                      "scripts/run_elog.py";
  if (path_error || !std::filesystem::is_regular_file(script)) {
    cm_msg(MERROR, kClientName, "Run ELOG script is unavailable: %s",
           script.c_str());
    *last_spawned_run = eor_run;
    return;
  }
  const std::string script_name = script.string();
  char* const arguments[] = {
      const_cast<char*>("/usr/bin/python3"),
      const_cast<char*>(script_name.c_str()),
      const_cast<char*>("--run"), run_text, nullptr};
  pid_t child = 0;
  const int result = posix_spawn(&child, arguments[0], nullptr, nullptr,
                                 arguments, environ);
  *last_spawned_run = eor_run;
  if (result != 0) {
    const std::string reason = std::string("Cannot launch run_elog.py: ") +
                               std::strerror(result);
    db_set_value(gDatabase, 0, "/Experiment/Run Elog/Last Status",
                 "ERROR", 6, 1, TID_STRING);
    db_set_value(gDatabase, 0, "/Experiment/Run Elog/Last Error",
                 reason.c_str(), static_cast<INT>(reason.size() + 1), 1,
                 TID_STRING);
    cm_msg(MERROR, kClientName, "Run %d ELOG post failed: %s", eor_run,
           reason.c_str());
  }
}

//************************************//
// Refresh the Runlog index after a completed run
//************************************//
void maybe_spawn_runlog_index(INT* last_spawned_run, pid_t* active_child,
                             INT* active_run) {
  INT state = 0, transition = 0, eor_run = 0;
  if (!read_value(gDatabase, "/Runinfo/State", TID_INT32, &state) ||
      !read_value(gDatabase, "/Runinfo/Transition in progress", TID_INT32, &transition) ||
      !read_value(gDatabase, "/DAQ/Status/Runlog/EORCompleteRunNumber", TID_INT32,
                  &eor_run) ||
      state != STATE_STOPPED || transition != 0 || eor_run <= 0 ||
      eor_run <= *last_spawned_run || *active_child > 0)
    return;

  std::string directory, subdir;
  if (!read_string(gDatabase, "/Logger/Message dir", &directory) || directory.empty()) {
    if (!read_string(gDatabase, "/Logger/Data dir", &directory) || directory.empty()) {
      cm_msg(MERROR, kClientName, "Cannot locate JSON Runlog directory for index");
      *last_spawned_run = eor_run;
      return;
    }
  }
  if (!read_string(gDatabase, "/Logger/Runlog/JSON/Subdir", &subdir)) {
    cm_msg(MERROR, kClientName, "Cannot read JSON Runlog subdirectory for index");
    *last_spawned_run = eor_run;
    return;
  }
  const std::string path =
      (std::filesystem::path(directory) / subdir).lexically_normal().string();
  std::error_code path_error;
  const auto executable =
      std::filesystem::read_symlink("/proc/self/exe", path_error);
  if (path_error) {
    cm_msg(MERROR, kClientName, "Cannot locate Runlog index script: %s",
           path_error.message().c_str());
    *last_spawned_run = eor_run;
    return;
  }
  const std::string script =
      (executable.parent_path().parent_path().parent_path() /
       "scripts/update_runlog_index.py").string();
  if (!std::filesystem::is_regular_file(script, path_error)) {
    cm_msg(MERROR, kClientName, "Runlog index script is unavailable: %s",
           script.c_str());
    *last_spawned_run = eor_run;
    return;
  }
  char* const arguments[] = {
      const_cast<char*>("/usr/bin/python3"),
      const_cast<char*>(script.c_str()),
      const_cast<char*>(path.c_str()), nullptr};
  pid_t child = 0;
  const int result = posix_spawn(&child, arguments[0], nullptr, nullptr,
                                 arguments, environ);
  *last_spawned_run = eor_run;
  if (result != 0)
    cm_msg(MERROR, kClientName, "Cannot launch Runlog index refresh for run %d: %s",
           eor_run, std::strerror(result));
  else {
    *active_child = child;
    *active_run = eor_run;
  }
}

std::optional<std::filesystem::path> edit_runlog_directory() {
  std::string directory, subdir;
  if (!read_string(gDatabase, "/Logger/Message dir", &directory) || directory.empty()) {
    if (!read_string(gDatabase, "/Logger/Data dir", &directory) || directory.empty())
      return std::nullopt;
  }
  if (!read_string(gDatabase, "/Logger/Runlog/JSON/Subdir", &subdir) || subdir.empty() ||
      std::filesystem::path(subdir).is_absolute())
    return std::nullopt;
  std::error_code error;
  const auto path = std::filesystem::weakly_canonical(
      std::filesystem::path(directory) / subdir, error);
  const char* home = std::getenv("HOME");
  if (error || !home || !*home ||
      path != std::filesystem::path(home) / "midas/midas/runlogs")
    return std::nullopt;
  return path;
}

//************************************//
// Check whether a Runlog may still be written
//************************************//
bool runlog_edit_is_active(std::int64_t target_run) {
  INT current_run = 0, state = 0, transition = 0;
  const bool valid =
      read_value(gDatabase, "/Runinfo/Run number", TID_INT32, &current_run) &&
      read_value(gDatabase, "/Runinfo/State", TID_INT32, &state) &&
      read_value(gDatabase, "/Runinfo/Transition in progress", TID_INT32, &transition);
  return daq_monitor::runlog_edit_target_active(
      target_run, current_run, state, transition, valid);
}

//************************************//
// Handle the limited Runlog metadata edit RPC
//************************************//
INT edit_runlog_rpc_callback(INT, void* parameters[]) {
  if (!parameters || !parameters[2]) return RPC_INVALID_ID;
  auto* reply = static_cast<std::string*>(parameters[2]);
  const char* command = static_cast<const char*>(parameters[0]);
  const char* arguments = static_cast<const char*>(parameters[1]);
  const auto directory = edit_runlog_directory();
  if (!directory) {
    *reply = daq_monitor::runlog_edit_rpc_response(
        {daq_monitor::RunlogEditCode::kIoError, "Runlog directory unavailable"}, 0);
    return RPC_SUCCESS;
  }
  const daq_monitor::RunlogEditor editor(*directory, runlog_edit_is_active);
  *reply = daq_monitor::handle_runlog_edit_rpc(
      command ? command : "", arguments ? arguments : "", editor);
  return RPC_SUCCESS;
}

void print_usage(const char* program) {
  std::printf("Usage: %s [-h host] [-e experiment]\n", program);
}

}  // namespace

//************************************//
// Run the DAQ status monitor
//************************************//
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
  if (!daq_monitor::initialize_monitor_alarm_classes(gDatabase) ||
      !daq_monitor::validate_monitor_global_alarm_class()) {
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
  const INT edit_rpc_status =
      cm_register_function(RPC_JRPC_CXX, edit_runlog_rpc_callback);
  if (edit_rpc_status != CM_SUCCESS) {
    std::fprintf(stderr, "Cannot register Runlog edit RPC: status %d\n",
                 edit_rpc_status);
    cm_disconnect_experiment();
    return 1;
  }
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  cm_msg(MINFO, kClientName,
         "Started DAQ status collection and component alarm synchronization");

  INT yield_status = CM_SUCCESS;
  INT last_spawned_elog_run = 0;
  INT last_spawned_index_run = 0;
  INT active_index_run = 0;
  pid_t active_index_child = 0;
  while (!gStopRequested) {
    if (publish_status() &&
        !daq_monitor::update_monitor_alarms(&gMonitorState.alarms))
      cm_msg(MERROR, kClientName, "DAQ alarm synchronization failed");
    int child_status = 0;
    pid_t reaped = 0;
    while ((reaped = waitpid(-1, &child_status, WNOHANG)) > 0) {
      if (reaped == active_index_child) {
        if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
          cm_msg(MERROR, kClientName,
                 "Runlog index refresh for run %d failed (child status %d)",
                 active_index_run, child_status);
        active_index_child = 0;
      }
    }
    maybe_spawn_run_elog(&last_spawned_elog_run);
    maybe_spawn_runlog_index(&last_spawned_index_run, &active_index_child,
                             &active_index_run);
    yield_status = cm_yield(kUpdatePeriodMs);
    if (yield_status == RPC_SHUTDOWN || yield_status == SS_ABORT)
      break;
  }

  cm_disconnect_experiment();
  return 0;
}
