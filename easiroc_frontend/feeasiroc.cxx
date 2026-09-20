#include "midas.h"
#include "mfe.h"

#include "easiroc_daq_control.h"
#include "easiroc_readout.h"
#include "easiroc_slow_control.h"
#include "easiroc_status.h"
#include "easiroc_stream.h"
#include "rbcp.h"
#include "tcp_probe.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr char kSettingsPath[] = "/Equipment/EASIROC/Settings";
constexpr char kInfoPath[] = "/Equipment/EASIROC/Info";
constexpr char kReadbackPath[] = "/Equipment/EASIROC/Readback";
constexpr char kVariablesPath[] = "/Equipment/EASIROC/Variables";
constexpr char kRunSnapshotPath[] = "/Equipment/EASIROC/RunSnapshot";
constexpr DWORD kRunSnapshotSchemaVersion = 1;
constexpr int kReceiveTimeoutMs = 100;
constexpr int kDrainQuietMs = 100;
constexpr int kDrainMaximumMs = 1000;
constexpr std::size_t kReceiveChunkBytes = 4096;
constexpr bool kWriteLowGainBank = false;
constexpr char kDefaultIpAddress[] = "192.168.10.26";
constexpr char kHardwareModel[] = "NIM-EASIROC";
constexpr DWORD kAsicCount = 2;
constexpr DWORD kChannelsPerAsic = 32;
static_assert(kAsicCount * kChannelsPerAsic == easiroc::kAdcChannelCount,
              "EASIROC hardware channel information is inconsistent");

struct FrontendSettings {
  bool enabled = true;
  std::string ip_address = kDefaultIpAddress;
  easiroc::DaqEnables enables;
};

struct RuntimeStatistics {
  std::uint64_t received_bytes = 0;
  std::uint64_t receive_chunks = 0;
  std::uint64_t tcp_error_count = 0;
  std::uint64_t receive_timeout_count = 0;
  std::uint64_t decode_error_count = 0;
  std::uint64_t event_content_error_count = 0;
  std::uint64_t adc_overflow_count = 0;
  std::uint64_t last_drain_bytes = 0;
  std::uint64_t total_drain_bytes = 0;
};

struct RuntimeState {
  bool enabled_for_run = false;
  bool rbcp_communication_ok = false;
  bool tcp_reachable = false;
  bool tcp_connected = false;
  bool acquisition_running = false;
  bool acquisition_fault = false;
  std::string last_error;
  std::uint64_t event_counter = 0;
  RuntimeStatistics statistics;
};

struct FirmwareObservation {
  bool valid = false;
  easiroc::FirmwareVersion firmware;
  std::string observed_ip_address;
  std::uint64_t observed_at_unix_time = 0;
  std::string observed_at_iso8601;
  std::string source;
};

struct EasirocRunSnapshot {
  DWORD schema_version = kRunSnapshotSchemaVersion;
  std::string snapshot_id;
  INT run_number = 0;
  std::uint64_t bor_unix_time = 0;
  std::string bor_time_iso8601;
  std::string frontend_name = "feeasiroc";
  bool frontend_bor_complete = false;
  bool enabled_for_run = false;
  FrontendSettings requested;
  FirmwareObservation firmware;
};

// This state is passive: construction and destruction perform no hardware
// access. All communication is explicit in BOR, polling, and cleanup paths.
struct FrontendState {
  FrontendSettings settings;
  bool run_active = false;
  easiroc::DaqControl daq_control;

  std::unique_ptr<RbcpClient> rbcp;
  std::unique_ptr<TcpConnection> tcp;
  std::unique_ptr<easiroc::EventStreamParser> parser;
  std::deque<easiroc::DecodedEvent> pending_events;
  bool daq_start_attempted = false;
  RuntimeState runtime;
  FirmwareObservation firmware_observation;
  EasirocRunSnapshot run_snapshot;

  // Connection points for the existing Slow Control policy. They are not
  // encoded or applied by this frontend.
  easiroc::EasirocSlowControlConfig slow_control_1;
  easiroc::EasirocSlowControlConfig slow_control_2;
};

FrontendState g_state;

struct DiagnosticResult {
  std::string host;
  bool rbcp_communication_ok = false;
  bool tcp_reachable = false;
  std::optional<easiroc::FirmwareVersion> firmware;
  std::uint64_t firmware_observed_at_unix_time = 0;
  std::string firmware_observed_at_iso8601;
  std::string error;
};

struct DiagnosticWorker {
  std::thread thread;
  std::atomic<bool> running{false};
  std::mutex result_mutex;
  std::optional<DiagnosticResult> result;
};

DiagnosticWorker g_diagnostic;

std::string odb_path(const char* base, const char* name) {
  return std::string(base) + "/" + name;
}

bool set_odb_value(const std::string& path, const void* value, INT size,
                   INT count, DWORD type) {
  const INT status =
      db_set_value(hDB, 0, path.c_str(), value, size, count, type);
  if (status != DB_SUCCESS)
    cm_msg(MERROR, "update_odb", "Cannot write %s (status %d)", path.c_str(),
           status);
  return status == DB_SUCCESS;
}

bool ensure_odb_value(const std::string& path, const void* default_value,
                      INT size, INT count, DWORD type) {
  HNDLE key = 0;
  const INT status = db_find_key(hDB, 0, path.c_str(), &key);
  if (status == DB_SUCCESS) return true;
  if (status != DB_NO_KEY) {
    cm_msg(MERROR, "initialize_odb", "Cannot inspect %s (status %d)",
           path.c_str(), status);
    return false;
  }
  return set_odb_value(path, default_value, size, count, type);
}

bool set_odb_string(const std::string& path, const std::string& value,
                    std::size_t capacity) {
  std::string bounded = value.substr(0, capacity - 1);
  std::vector<char> buffer(capacity, '\0');
  std::copy(bounded.begin(), bounded.end(), buffer.begin());
  return set_odb_value(path, buffer.data(), buffer.size(), 1, TID_STRING);
}

bool publish_configuration_status(bool configuration_ok, INT run_number) {
  const BOOL value = configuration_ok ? TRUE : FALSE;
  const std::time_t now = std::time(nullptr);
  const std::uint64_t checked_unix =
      now < 0 ? 0 : static_cast<std::uint64_t>(now);
  const std::string frontend = odb_path(kVariablesPath, "Frontend");
  bool ok = true;

  // Clear the gate first on negative updates. On positive updates, publish
  // the identifying metadata before opening the gate.
  if (!configuration_ok)
    ok = set_odb_value(odb_path(frontend.c_str(), "ConfigurationOK"),
                       &value, sizeof(value), 1, TID_BOOL) && ok;
  ok = set_odb_value(
           odb_path(frontend.c_str(), "ConfigurationCheckedUnix"),
           &checked_unix, sizeof(checked_unix), 1, TID_QWORD) && ok;
  ok = set_odb_value(
           odb_path(frontend.c_str(), "ConfigurationRunNumber"),
           &run_number, sizeof(run_number), 1, TID_INT) && ok;
  if (configuration_ok)
    ok = set_odb_value(odb_path(frontend.c_str(), "ConfigurationOK"),
                       &value, sizeof(value), 1, TID_BOOL) && ok;
  return ok;
}

void mark_configuration_failed(INT run_number) {
  if (!publish_configuration_status(false, run_number))
    cm_msg(MERROR, "begin_of_run",
           "Cannot publish failed EASIROC configuration status for run %d",
           run_number);
}

std::string formatIso8601Utc(std::time_t value) {
  std::tm utc{};
  char buffer[32] = {};
  if (gmtime_r(&value, &utc) == nullptr ||
      std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0)
    return {};
  return buffer;
}

void reset_run_snapshot(INT run_number) {
  g_state.run_snapshot = {};
  auto& snapshot = g_state.run_snapshot;
  snapshot.schema_version = kRunSnapshotSchemaVersion;
  snapshot.run_number = run_number;
  const std::time_t now = std::time(nullptr);
  snapshot.bor_unix_time =
      now < 0 ? 0 : static_cast<std::uint64_t>(now);
  snapshot.bor_time_iso8601 = formatIso8601Utc(now);
  snapshot.frontend_name = "feeasiroc";
  snapshot.snapshot_id =
      "run-" + std::to_string(run_number) + "_" +
      std::to_string(snapshot.bor_unix_time) + "_feeasiroc";
}

bool publish_run_snapshot() {
  const auto& snapshot = g_state.run_snapshot;
  const std::string metadata = odb_path(kRunSnapshotPath, "Metadata");
  const std::string requested = odb_path(kRunSnapshotPath, "Requested");
  const std::string firmware = odb_path(kRunSnapshotPath, "Readback/Firmware");
  const BOOL enabled_for_run = snapshot.enabled_for_run ? TRUE : FALSE;
  const BOOL requested_enabled = snapshot.requested.enabled ? TRUE : FALSE;
  const BOOL adc = snapshot.requested.enables.adc ? TRUE : FALSE;
  const BOOL tdc = snapshot.requested.enables.tdc ? TRUE : FALSE;
  const BOOL scaler = snapshot.requested.enables.scaler ? TRUE : FALSE;
  const BOOL firmware_valid = snapshot.firmware.valid ? TRUE : FALSE;
  bool ok = true;
  ok = set_odb_value(odb_path(metadata.c_str(), "SchemaVersion"),
                     &snapshot.schema_version, sizeof(snapshot.schema_version),
                     1, TID_DWORD) && ok;
  ok = set_odb_string(odb_path(metadata.c_str(), "SnapshotId"),
                      snapshot.snapshot_id, 128) && ok;
  ok = set_odb_value(odb_path(metadata.c_str(), "RunNumber"),
                     &snapshot.run_number, sizeof(snapshot.run_number), 1,
                     TID_INT) && ok;
  ok = set_odb_value(odb_path(metadata.c_str(), "BORUnixTime"),
                     &snapshot.bor_unix_time,
                     sizeof(snapshot.bor_unix_time), 1, TID_QWORD) && ok;
  ok = set_odb_string(odb_path(metadata.c_str(), "BORTimeISO8601"),
                      snapshot.bor_time_iso8601, 32) && ok;
  ok = set_odb_string(odb_path(metadata.c_str(), "FrontendName"),
                      snapshot.frontend_name, 32) && ok;
  ok = set_odb_value(odb_path(metadata.c_str(), "EnabledForRun"),
                     &enabled_for_run, sizeof(enabled_for_run), 1, TID_BOOL) &&
       ok;

  ok = set_odb_value(odb_path(requested.c_str(), "Enabled"),
                     &requested_enabled, sizeof(requested_enabled), 1,
                     TID_BOOL) && ok;
  ok = set_odb_string(odb_path(requested.c_str(), "IPAddress"),
                      snapshot.requested.ip_address, 64) && ok;
  ok = set_odb_value(odb_path(requested.c_str(), "ADCEnabled"), &adc,
                     sizeof(adc), 1, TID_BOOL) && ok;
  ok = set_odb_value(odb_path(requested.c_str(), "TDCEnabled"), &tdc,
                     sizeof(tdc), 1, TID_BOOL) && ok;
  ok = set_odb_value(odb_path(requested.c_str(), "ScalerEnabled"), &scaler,
                     sizeof(scaler), 1, TID_BOOL) && ok;

  ok = set_odb_value(odb_path(firmware.c_str(), "Valid"), &firmware_valid,
                     sizeof(firmware_valid), 1, TID_BOOL) && ok;
  const std::string version = snapshot.firmware.valid
                                  ? snapshot.firmware.firmware.versionString()
                                  : std::string();
  const std::string synthesis_date =
      snapshot.firmware.valid
          ? snapshot.firmware.firmware.synthesisDateString()
          : std::string();
  ok = set_odb_string(odb_path(firmware.c_str(), "Version"), version, 64) && ok;
  ok = set_odb_string(odb_path(firmware.c_str(), "SynthesisDate"),
                      synthesis_date, 64) && ok;
  ok = set_odb_value(odb_path(firmware.c_str(), "Raw"),
                     snapshot.firmware.firmware.raw.data(),
                     snapshot.firmware.firmware.raw.size(),
                     snapshot.firmware.firmware.raw.size(), TID_BYTE) && ok;
  ok = set_odb_value(odb_path(firmware.c_str(), "ObservedAtUnixTime"),
                     &snapshot.firmware.observed_at_unix_time,
                     sizeof(snapshot.firmware.observed_at_unix_time), 1,
                     TID_QWORD) && ok;
  ok = set_odb_string(odb_path(firmware.c_str(), "ObservedAtISO8601"),
                      snapshot.firmware.observed_at_iso8601, 32) && ok;
  ok = set_odb_string(odb_path(firmware.c_str(), "Source"),
                      snapshot.firmware.source, 32) && ok;
  /* Publish the completion marker last. If any preceding write failed,
   * leave the fixed subtree explicitly incomplete. */
  const BOOL published_complete =
      (snapshot.frontend_bor_complete && ok) ? TRUE : FALSE;
  const bool complete_ok = set_odb_value(
      odb_path(metadata.c_str(), "FrontendBORComplete"), &published_complete,
      sizeof(published_complete), 1, TID_BOOL);
  ok = complete_ok && ok;
  return ok;
}

bool publish_runtime_variables() {
  const BOOL enabled_for_run =
      g_state.runtime.enabled_for_run ? TRUE : FALSE;
  const BOOL rbcp_ok = g_state.runtime.rbcp_communication_ok ? TRUE : FALSE;
  const BOOL tcp_reachable = g_state.runtime.tcp_reachable ? TRUE : FALSE;
  const BOOL tcp_connected = g_state.runtime.tcp_connected ? TRUE : FALSE;
  const BOOL running = g_state.runtime.acquisition_running ? TRUE : FALSE;
  const BOOL fault = g_state.runtime.acquisition_fault ? TRUE : FALSE;
  const std::uint64_t pending = g_state.pending_events.size();
  const std::uint64_t buffered =
      g_state.parser ? g_state.parser->bufferedBytes() : 0;
  bool ok = true;
#define PUBLISH_BOOL(name, value) \
  ok = set_odb_value(odb_path(kVariablesPath, name), &(value), \
                     sizeof(value), 1, TID_BOOL) && ok
#define PUBLISH_QWORD(base, name, value) \
  ok = set_odb_value(odb_path(base, name), &(value), sizeof(value), 1, \
                     TID_QWORD) && ok
  PUBLISH_BOOL("EnabledForRun", enabled_for_run);
  PUBLISH_BOOL("RBCPCommunicationOK", rbcp_ok);
  PUBLISH_BOOL("TCPReachable", tcp_reachable);
  PUBLISH_BOOL("TCPConnected", tcp_connected);
  PUBLISH_BOOL("AcquisitionRunning", running);
  PUBLISH_BOOL("AcquisitionFault", fault);
  ok = set_odb_string(odb_path(kVariablesPath, "LastError"),
                      g_state.runtime.last_error, 256) && ok;
  PUBLISH_QWORD(kVariablesPath, "EventCounter", g_state.runtime.event_counter);
  const std::string readout = odb_path(kVariablesPath, "Readout");
  PUBLISH_QWORD(readout.c_str(), "PendingEventCount", pending);
  PUBLISH_QWORD(readout.c_str(), "ParserBufferedBytes", buffered);
  const std::string statistics = odb_path(kVariablesPath, "Statistics");
  PUBLISH_QWORD(statistics.c_str(), "ReceivedBytes",
                g_state.runtime.statistics.received_bytes);
  PUBLISH_QWORD(statistics.c_str(), "ReceiveChunks",
                g_state.runtime.statistics.receive_chunks);
  PUBLISH_QWORD(statistics.c_str(), "TCPErrorCount",
                g_state.runtime.statistics.tcp_error_count);
  PUBLISH_QWORD(statistics.c_str(), "ReceiveTimeoutCount",
                g_state.runtime.statistics.receive_timeout_count);
  PUBLISH_QWORD(statistics.c_str(), "DecodeErrorCount",
                g_state.runtime.statistics.decode_error_count);
  PUBLISH_QWORD(statistics.c_str(), "EventContentErrorCount",
                g_state.runtime.statistics.event_content_error_count);
  PUBLISH_QWORD(statistics.c_str(), "ADCOverflowCount",
                g_state.runtime.statistics.adc_overflow_count);
  PUBLISH_QWORD(statistics.c_str(), "LastDrainBytes",
                g_state.runtime.statistics.last_drain_bytes);
  PUBLISH_QWORD(statistics.c_str(), "TotalDrainBytes",
                g_state.runtime.statistics.total_drain_bytes);
#undef PUBLISH_QWORD
#undef PUBLISH_BOOL
  return ok;
}

bool initialize_odb() {
  char default_ip[64] = {};
  std::snprintf(default_ip, sizeof(default_ip), "%s", kDefaultIpAddress);
  const BOOL yes = TRUE;
  const BOOL no = FALSE;
  if (!ensure_odb_value(odb_path(kSettingsPath, "Enabled"),
                        &yes, sizeof(yes), 1, TID_BOOL) ||
      !ensure_odb_value(odb_path(kSettingsPath, "Network/IPAddress"),
                        default_ip, sizeof(default_ip), 1, TID_STRING) ||
      !ensure_odb_value(odb_path(kSettingsPath, "Acquisition/ADCEnabled"),
                        &yes, sizeof(yes), 1, TID_BOOL) ||
      !ensure_odb_value(odb_path(kSettingsPath, "Acquisition/TDCEnabled"),
                        &yes, sizeof(yes), 1, TID_BOOL) ||
      !ensure_odb_value(odb_path(kSettingsPath, "Acquisition/ScalerEnabled"),
                        &no, sizeof(no), 1, TID_BOOL))
    return false;

  const DWORD channel_count = easiroc::kAdcChannelCount;
  const DWORD tcp_port = easiroc::kTcpDataPort;
  const DWORD rbcp_port = kRbcpPort;
  if (!set_odb_string(odb_path(kInfoPath, "Hardware/Model"), kHardwareModel,
                      32) ||
      !set_odb_value(odb_path(kInfoPath, "Hardware/ASICCount"), &kAsicCount,
                     sizeof(kAsicCount), 1, TID_DWORD) ||
      !set_odb_value(odb_path(kInfoPath, "Hardware/ChannelsPerASIC"),
                     &kChannelsPerAsic, sizeof(kChannelsPerAsic), 1,
                     TID_DWORD) ||
      !set_odb_value(odb_path(kInfoPath, "Hardware/ChannelCount"),
                     &channel_count, sizeof(channel_count), 1, TID_DWORD) ||
      !set_odb_value(odb_path(kInfoPath, "Network/TCPDataPort"), &tcp_port,
                     sizeof(tcp_port), 1, TID_DWORD) ||
      !set_odb_value(odb_path(kInfoPath, "Network/RBCPPort"), &rbcp_port,
                     sizeof(rbcp_port), 1, TID_DWORD) ||
      !set_odb_value(
          odb_path(kInfoPath, "Capabilities/ConfigurationReadbackSupported"),
          &no, sizeof(no), 1, TID_BOOL) ||
      !set_odb_value(odb_path(kInfoPath,
                             "Capabilities/ScalerReadoutSupported"),
                     &no, sizeof(no), 1, TID_BOOL) ||
      !set_odb_value(odb_path(kInfoPath, "Capabilities/LowGainDecoded"),
                     &yes, sizeof(yes), 1, TID_BOOL))
    return false;

  const std::array<std::uint8_t, easiroc::kFirmwareVersionLength> empty_raw{};
  if (!set_odb_value(odb_path(kReadbackPath, "Firmware/Valid"), &no,
                     sizeof(no), 1, TID_BOOL) ||
      !set_odb_string(odb_path(kReadbackPath, "Firmware/Version"), "", 64) ||
      !set_odb_string(odb_path(kReadbackPath, "Firmware/SynthesisDate"), "",
                      64) ||
      !set_odb_value(odb_path(kReadbackPath, "Firmware/Raw"), empty_raw.data(),
                     empty_raw.size(), empty_raw.size(), TID_BYTE))
    return false;
  reset_run_snapshot(0);
  return publish_configuration_status(false, 0) &&
         publish_runtime_variables() && publish_run_snapshot();
}

INT read_bool_setting(const char* name, bool* value) {
  BOOL odb_value = *value ? TRUE : FALSE;
  INT size = sizeof(odb_value);
  const INT status =
      db_get_value(hDB, 0, name, &odb_value, &size, TID_BOOL, FALSE);
  if (status != DB_SUCCESS) {
    cm_msg(MERROR, "read_settings", "Cannot read %s (status %d)", name,
           status);
    return FE_ERR_ODB;
  }
  *value = odb_value != FALSE;
  return SUCCESS;
}

INT read_settings(FrontendSettings* settings) {
  char ip_address[64] = {};
  INT size = sizeof(ip_address);
  std::string path = odb_path(kSettingsPath, "Network/IPAddress");
  INT status = db_get_value(hDB, 0, path.c_str(), ip_address, &size,
                            TID_STRING, FALSE);
  if (status != DB_SUCCESS) {
    cm_msg(MERROR, "read_settings", "Cannot read %s (status %d)",
           path.c_str(), status);
    return FE_ERR_ODB;
  }

  FrontendSettings next;
  next.ip_address = ip_address;

  path = odb_path(kSettingsPath, "Enabled");
  status = read_bool_setting(path.c_str(), &next.enabled);
  if (status != SUCCESS) return status;

  path = odb_path(kSettingsPath, "Acquisition/ADCEnabled");
  status = read_bool_setting(path.c_str(), &next.enables.adc);
  if (status != SUCCESS) return status;

  path = odb_path(kSettingsPath, "Acquisition/TDCEnabled");
  status = read_bool_setting(path.c_str(), &next.enables.tdc);
  if (status != SUCCESS) return status;

  path = odb_path(kSettingsPath, "Acquisition/ScalerEnabled");
  status = read_bool_setting(path.c_str(), &next.enables.scaler);
  if (status != SUCCESS) return status;

  *settings = next;
  return SUCCESS;
}

void set_firmware_readback_valid(bool valid) {
  const BOOL value = valid ? TRUE : FALSE;
  set_odb_value(odb_path(kReadbackPath, "Firmware/Valid"), &value,
                sizeof(value), 1, TID_BOOL);
}

void reset_software_readout_state() {
  g_state.tcp.reset();
  g_state.parser.reset();
  g_state.pending_events.clear();
  g_state.rbcp.reset();
  g_state.daq_start_attempted = false;
}

void set_disabled_runtime_state() {
  reset_software_readout_state();
  g_state.runtime = {};
  g_state.firmware_observation = {};
  set_firmware_readback_valid(false);
}

void run_diagnostic(std::string host) {
  DiagnosticResult result;
  result.host = host;

  try {
    const auto firmware = easiroc::readFirmwareVersion(host);
    result.firmware = firmware;
    const std::time_t observed = std::time(nullptr);
    result.firmware_observed_at_unix_time =
        observed < 0 ? 0 : static_cast<std::uint64_t>(observed);
    result.firmware_observed_at_iso8601 = formatIso8601Utc(observed);
    result.rbcp_communication_ok = true;
  } catch (const std::exception& error) {
    result.error = std::string("RBCP firmware read: ") + error.what();
  }

  try {
    easiroc::probeDataConnection(host);
    result.tcp_reachable = true;
  } catch (const std::exception& error) {
    if (!result.error.empty()) result.error += "; ";
    result.error += std::string("TCP port 24 probe: ") + error.what();
  }

  {
    std::lock_guard<std::mutex> lock(g_diagnostic.result_mutex);
    g_diagnostic.result = std::move(result);
  }
  g_diagnostic.running.store(false, std::memory_order_release);
}

void start_diagnostic(const std::string& host) {
  if (g_diagnostic.running.load(std::memory_order_acquire)) return;
  if (g_diagnostic.thread.joinable()) g_diagnostic.thread.join();
  g_diagnostic.running.store(true, std::memory_order_release);
  g_diagnostic.thread = std::thread(run_diagnostic, host);
}

void publish_completed_diagnostic() {
  if (g_diagnostic.running.load(std::memory_order_acquire)) return;
  if (g_diagnostic.thread.joinable()) g_diagnostic.thread.join();

  std::optional<DiagnosticResult> result;
  {
    std::lock_guard<std::mutex> lock(g_diagnostic.result_mutex);
    result = std::move(g_diagnostic.result);
    g_diagnostic.result.reset();
  }
  if (!result) return;

  g_state.runtime.rbcp_communication_ok = result->rbcp_communication_ok;
  g_state.runtime.tcp_reachable = result->tcp_reachable;
  g_state.runtime.last_error = result->error;

  const BOOL invalid = FALSE;
  const std::string valid_path = odb_path(kReadbackPath, "Firmware/Valid");
  set_odb_value(valid_path, &invalid, sizeof(invalid), 1, TID_BOOL);
  g_state.firmware_observation = {};
  if (result->firmware) {
    const auto& firmware = *result->firmware;
    bool readback_ok =
        set_odb_string(odb_path(kReadbackPath, "Firmware/Version"),
                       firmware.versionString(), 64);
    readback_ok =
        set_odb_string(odb_path(kReadbackPath, "Firmware/SynthesisDate"),
                       firmware.synthesisDateString(), 64) && readback_ok;
    readback_ok =
        set_odb_value(odb_path(kReadbackPath, "Firmware/Raw"),
                      firmware.raw.data(), firmware.raw.size(),
                      firmware.raw.size(), TID_BYTE) && readback_ok;
    if (readback_ok) {
      const BOOL valid = TRUE;
      set_odb_value(valid_path, &valid, sizeof(valid), 1, TID_BOOL);
      g_state.firmware_observation.valid = true;
      g_state.firmware_observation.firmware = firmware;
      g_state.firmware_observation.observed_ip_address = result->host;
      g_state.firmware_observation.observed_at_unix_time =
          result->firmware_observed_at_unix_time;
      g_state.firmware_observation.observed_at_iso8601 =
          result->firmware_observed_at_iso8601;
      g_state.firmware_observation.source = "StoppedDiagnostic";
    }
  }

  if (result->rbcp_communication_ok && result->tcp_reachable) {
    cm_msg(MINFO, "update_status",
           "Read-only status OK: RBCP firmware read and TCP port 24 probe "
           "succeeded");
  } else {
    cm_msg(MERROR, "update_status", "Read-only status failed: %s",
           result->error.c_str());
  }
}

struct CleanupResult {
  bool daq_off_succeeded = true;
  bool drain_succeeded = true;
  std::string error;
};

CleanupResult stop_acquisition(const char* caller, bool drain_after_stop) {
  CleanupResult result;
  if (g_state.daq_start_attempted && g_state.rbcp) {
    const auto stop = g_state.daq_control.stopValue();
    try {
      cm_msg(MINFO, caller, "RBCP DAQ OFF: address 0x%08x value 0x%02x",
             static_cast<unsigned>(stop.address),
             static_cast<unsigned>(stop.value));
      g_state.rbcp->write(stop.address, stop.value);
      g_state.daq_start_attempted = false;
    } catch (const std::exception& exception) {
      result.daq_off_succeeded = false;
      g_state.runtime.rbcp_communication_ok = false;
      result.error = std::string("DAQ OFF failed; hardware state unknown: ") +
                     exception.what();
      cm_msg(MERROR, caller, "%s", result.error.c_str());
    }
  }

  if (drain_after_stop && result.daq_off_succeeded && g_state.tcp) {
    try {
      const std::size_t drained =
          g_state.tcp->drain(kDrainQuietMs, kDrainMaximumMs);
      g_state.runtime.statistics.last_drain_bytes = drained;
      g_state.runtime.statistics.total_drain_bytes += drained;
      cm_msg(MINFO, caller, "Post-acquisition drain discarded %zu byte(s)",
             drained);
    } catch (const std::exception& exception) {
      result.drain_succeeded = false;
      if (!result.error.empty()) result.error += "; ";
      result.error += std::string("post-acquisition drain failed: ") +
                      exception.what();
      cm_msg(MERROR, caller, "%s", result.error.c_str());
    }
  }

  g_state.runtime.acquisition_running = false;
  g_state.runtime.tcp_connected = false;
  g_state.tcp.reset();
  g_state.parser.reset();
  g_state.pending_events.clear();
  if (!g_state.daq_start_attempted) g_state.rbcp.reset();
  return result;
}

void handle_acquisition_error(const std::string& message) {
  if (g_state.runtime.acquisition_fault) return;
  g_state.runtime.acquisition_fault = true;
  cm_msg(MERROR, "poll_event", "Acquisition failed: %s", message.c_str());
  const CleanupResult cleanup = stop_acquisition("poll_event", false);
  std::string full_error = message;
  if (!cleanup.error.empty()) full_error += "; " + cleanup.error;
  g_state.runtime.last_error = full_error;
  publish_runtime_variables();

  char transition_error[256] = {};
  const INT status = cm_transition(TR_STOP, 0, transition_error,
                                   sizeof(transition_error), TR_ASYNC, FALSE);
  if (status != CM_SUCCESS)
    cm_msg(MERROR, "poll_event",
           "Cannot request asynchronous run stop (status %d): %s", status,
           transition_error);
}

bool finalize_run_snapshot(const FrontendSettings& settings) {
  auto& snapshot = g_state.run_snapshot;
  snapshot.requested = settings;
  snapshot.enabled_for_run = settings.enabled;
  snapshot.firmware =
      settings.enabled && g_state.firmware_observation.valid &&
              g_state.firmware_observation.observed_ip_address ==
                  settings.ip_address
          ? g_state.firmware_observation
          : FirmwareObservation{};
  snapshot.frontend_bor_complete = true;
  if (publish_run_snapshot()) return true;
  snapshot.frontend_bor_complete = false;
  return false;
}

bool configuration_ready(const FrontendSettings& settings) {
  if (!g_state.run_snapshot.frontend_bor_complete) return false;
  if (!settings.enabled) return true;
  return g_state.runtime.enabled_for_run &&
         g_state.runtime.rbcp_communication_ok &&
         g_state.runtime.tcp_reachable && g_state.runtime.tcp_connected &&
         g_state.runtime.acquisition_running &&
         !g_state.runtime.acquisition_fault && g_state.daq_start_attempted &&
         g_state.rbcp && g_state.tcp && g_state.parser;
}

}  // namespace

const char* frontend_name = "feeasiroc";
const char* frontend_file_name = __FILE__;
BOOL frontend_call_loop = FALSE;
INT display_period = 0;
INT max_event_size = 1024 * 1024;
INT max_event_size_frag = 0;
INT event_buffer_size = 2 * 1024 * 1024;

INT read_physics_event(char*, INT);
INT read_status_event(char*, INT);
INT read_configuration_event(char*, INT);
INT poll_event(INT source, INT count, BOOL test);

BOOL equipment_common_overwrite = TRUE;

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
EQUIPMENT equipment[] = {
    {"NIM-EASIROC Physics",
     {1, 0, "SYSTEM", EQ_POLLED, 0, "MIDAS", TRUE, RO_RUNNING, 100, 0, 0,
      0, "", "", "", "", "", FALSE},
     read_physics_event},
    {"NIM-EASIROC Status",
     {2, 0, "SYSTEM", EQ_PERIODIC, 0, "MIDAS", TRUE,
      RO_RUNNING | RO_STOPPED | RO_PAUSED, 10000, 0, 0, 0, "", "", "", "",
      "", FALSE},
     read_status_event},
    {"EASIROC Configuration",
     {6, 0, "SYSTEM", EQ_PERIODIC, 0, "MIDAS", TRUE, RO_BOR, 0, 0, 0, 0,
      "", "", "", "", "", FALSE},
     read_configuration_event},
    {""}};
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

INT frontend_init() {
  if (!initialize_odb()) return FE_ERR_ODB;

  const INT settings_status = read_settings(&g_state.settings);
  if (settings_status != SUCCESS) return settings_status;
  if (!g_state.settings.enabled) {
    set_disabled_runtime_state();
    publish_runtime_variables();
    cm_msg(MINFO, "frontend_init",
           "NIM-EASIROC disabled in ODB; startup hardware diagnostics "
           "skipped");
    return SUCCESS;
  }
  start_diagnostic(g_state.settings.ip_address);

  cm_msg(MINFO, "frontend_init",
         "Started read-only RBCP firmware and TCP port 24 checks; no register "
         "write or TCP stream receive is performed");
  return SUCCESS;
}

INT frontend_exit() {
  if (g_diagnostic.thread.joinable()) g_diagnostic.thread.join();
  const CleanupResult cleanup = stop_acquisition("frontend_exit", true);
  g_state.run_active = false;
  if (!cleanup.error.empty()) g_state.runtime.last_error = cleanup.error;
  g_state.runtime.acquisition_fault = !cleanup.daq_off_succeeded ||
                                      !cleanup.drain_succeeded;
  publish_runtime_variables();
  if (!cleanup.daq_off_succeeded) return FE_ERR_HW;
  return cleanup.drain_succeeded ? SUCCESS : FE_ERR_HW;
}

INT begin_of_run(INT run_number, char* error) {
  if (error != nullptr) error[0] = '\0';
  if (!publish_configuration_status(false, run_number)) {
    if (error != nullptr)
      std::snprintf(error, 256,
                    "Cannot reset EASIROC configuration status at BOR");
    return FE_ERR_ODB;
  }
  reset_run_snapshot(run_number);

  // The read-only startup diagnostic must not share TCP/RBCP access with an
  // active acquisition.
  if (g_diagnostic.thread.joinable()) g_diagnostic.thread.join();
  publish_completed_diagnostic();

  FrontendSettings run_settings;
  const INT status = read_settings(&run_settings);
  if (status != SUCCESS) {
    if (error != nullptr)
      std::snprintf(error, 256, "Cannot read NIM-EASIROC settings from ODB");
    mark_configuration_failed(run_number);
    return status;
  }

  g_state.settings = run_settings;
  g_state.run_active = true;
  if (!run_settings.enabled) {
    set_disabled_runtime_state();
    if (!finalize_run_snapshot(run_settings)) {
      g_state.run_active = false;
      if (error != nullptr)
        std::snprintf(error, 256,
                      "Cannot publish completed EASIROC RunSnapshot");
      mark_configuration_failed(run_number);
      return FE_ERR_ODB;
    }
    publish_runtime_variables();
    if (!configuration_ready(run_settings)) {
      g_state.run_snapshot.frontend_bor_complete = false;
      publish_run_snapshot();
      g_state.run_active = false;
      if (error != nullptr)
        std::snprintf(error, 256,
                      "EASIROC configuration readiness check failed");
      mark_configuration_failed(run_number);
      return FE_ERR_HW;
    }
    if (!publish_configuration_status(true, run_number)) {
      g_state.run_snapshot.frontend_bor_complete = false;
      publish_run_snapshot();
      g_state.run_active = false;
      if (error != nullptr)
        std::snprintf(error, 256,
                      "Cannot publish successful EASIROC configuration status");
      mark_configuration_failed(run_number);
      return FE_ERR_ODB;
    }
    cm_msg(MINFO, "begin_of_run",
           "Run %d: NIM-EASIROC disabled by BOR Settings snapshot; "
           "hardware access skipped",
           run_number);
    return SUCCESS;
  }

  g_state.runtime.enabled_for_run = true;

  if (!run_settings.enables.adc || !run_settings.enables.tdc ||
      run_settings.enables.scaler) {
    const char* message =
        "Physics readout supports only ADCEnabled=y, TDCEnabled=y, "
        "ScalerEnabled=n";
    cm_msg(MERROR, "begin_of_run", "%s", message);
    if (error != nullptr) std::snprintf(error, 256, "%s", message);
    g_state.run_active = false;
    g_state.runtime.last_error = message;
    publish_runtime_variables();
    mark_configuration_failed(run_number);
    return FE_ERR_ODB;
  }

  const CleanupResult previous = stop_acquisition("begin_of_run", false);
  if (!previous.daq_off_succeeded) {
    g_state.runtime.acquisition_fault = true;
    g_state.run_active = false;
    g_state.runtime.last_error = previous.error;
    publish_runtime_variables();
    if (error != nullptr)
      std::snprintf(error, 256, "%s", previous.error.c_str());
    mark_configuration_failed(run_number);
    return FE_ERR_HW;
  }

  g_state.runtime.event_counter = 0;
  g_state.runtime.statistics = {};
  g_state.runtime.acquisition_fault = false;
  g_state.daq_control.setEnables(g_state.settings.enables);
  const auto start = g_state.daq_control.startValue();
  try {
    g_state.runtime.tcp_reachable = false;
    g_state.rbcp = std::make_unique<RbcpClient>(g_state.settings.ip_address,
                                                kRbcpPort);
    g_state.tcp = std::make_unique<TcpConnection>(
        g_state.settings.ip_address, easiroc::kTcpDataPort,
        easiroc::kTcpConnectTimeoutMilliseconds);
    g_state.runtime.tcp_reachable = true;
    g_state.runtime.tcp_connected = true;
    const std::size_t drained =
        g_state.tcp->drain(kDrainQuietMs, kDrainMaximumMs);
    g_state.runtime.statistics.last_drain_bytes = drained;
    g_state.runtime.statistics.total_drain_bytes += drained;
    cm_msg(MINFO, "begin_of_run",
           "Run %d connected to %s:%u; pre-acquisition drain discarded "
           "%zu byte(s)",
           run_number, g_state.settings.ip_address.c_str(),
           static_cast<unsigned>(easiroc::kTcpDataPort), drained);

    g_state.parser = std::make_unique<easiroc::EventStreamParser>();
    g_state.pending_events.clear();
    g_state.daq_start_attempted = true;
    cm_msg(MINFO, "begin_of_run",
           "RBCP DAQ ON: address 0x%08x value 0x%02x",
           static_cast<unsigned>(start.address),
           static_cast<unsigned>(start.value));
    g_state.rbcp->write(start.address, start.value);
    g_state.runtime.rbcp_communication_ok = true;
    g_state.runtime.acquisition_running = true;
    g_state.runtime.acquisition_fault = false;
    g_state.runtime.last_error.clear();
    if (!finalize_run_snapshot(run_settings)) {
      const CleanupResult cleanup = stop_acquisition("begin_of_run", false);
      g_state.run_active = false;
      g_state.runtime.acquisition_fault = true;
      g_state.runtime.last_error =
          "Cannot publish completed EASIROC RunSnapshot";
      if (!cleanup.error.empty())
        g_state.runtime.last_error += "; " + cleanup.error;
      publish_runtime_variables();
      if (error != nullptr)
        std::snprintf(error, 256, "%s",
                      g_state.runtime.last_error.c_str());
      mark_configuration_failed(run_number);
      return FE_ERR_ODB;
    }
    publish_runtime_variables();
    if (!configuration_ready(run_settings)) {
      const CleanupResult cleanup =
          stop_acquisition("begin_of_run", false);
      g_state.run_snapshot.frontend_bor_complete = false;
      publish_run_snapshot();
      g_state.run_active = false;
      g_state.runtime.acquisition_fault = true;
      g_state.runtime.last_error =
          "EASIROC configuration readiness check failed";
      if (!cleanup.error.empty())
        g_state.runtime.last_error += "; " + cleanup.error;
      publish_runtime_variables();
      if (error != nullptr)
        std::snprintf(error, 256, "%s",
                      g_state.runtime.last_error.c_str());
      mark_configuration_failed(run_number);
      return FE_ERR_HW;
    }
    if (!publish_configuration_status(true, run_number)) {
      const CleanupResult cleanup =
          stop_acquisition("begin_of_run", false);
      g_state.run_snapshot.frontend_bor_complete = false;
      publish_run_snapshot();
      g_state.run_active = false;
      g_state.runtime.acquisition_fault = true;
      g_state.runtime.last_error =
          "Cannot publish successful EASIROC configuration status";
      if (!cleanup.error.empty())
        g_state.runtime.last_error += "; " + cleanup.error;
      publish_runtime_variables();
      if (error != nullptr)
        std::snprintf(error, 256, "%s",
                      g_state.runtime.last_error.c_str());
      mark_configuration_failed(run_number);
      return FE_ERR_ODB;
    }
    return SUCCESS;
  } catch (const std::exception& exception) {
    const std::string start_error =
        std::string("BOR acquisition setup failed: ") + exception.what();
    const CleanupResult cleanup = stop_acquisition("begin_of_run", false);
    std::string message = start_error;
    if (!cleanup.error.empty()) message += "; " + cleanup.error;
    cm_msg(MERROR, "begin_of_run", "%s", message.c_str());
    g_state.run_active = false;
    g_state.runtime.acquisition_fault = true;
    g_state.runtime.last_error = message;
    publish_runtime_variables();
    if (error != nullptr) std::snprintf(error, 256, "%s", message.c_str());
    mark_configuration_failed(run_number);
    return FE_ERR_HW;
  }
}

INT end_of_run(INT run_number, char* error) {
  if (error != nullptr) error[0] = '\0';
  if (!g_state.runtime.enabled_for_run) {
    set_disabled_runtime_state();
    g_state.run_active = false;
    publish_runtime_variables();
    cm_msg(MINFO, "end_of_run",
           "Run %d: NIM-EASIROC was disabled; software state cleared "
           "without hardware access",
           run_number);
    return SUCCESS;
  }
  cm_msg(MINFO, "end_of_run", "Stopping acquisition for run %d", run_number);
  const CleanupResult cleanup = stop_acquisition("end_of_run", true);
  g_state.run_active = false;
  if (!cleanup.error.empty()) {
    g_state.runtime.last_error = cleanup.error;
    if (error != nullptr)
      std::snprintf(error, 256, "%s", cleanup.error.c_str());
  }
  g_state.runtime.acquisition_fault =
      !cleanup.daq_off_succeeded || !cleanup.drain_succeeded;
  publish_runtime_variables();
  if (!cleanup.daq_off_succeeded || !cleanup.drain_succeeded)
    return FE_ERR_HW;
  return SUCCESS;
}

INT pause_run(INT, char* error) {
  if (error != nullptr) error[0] = '\0';
  return SUCCESS;
}

INT resume_run(INT, char* error) {
  if (error != nullptr) error[0] = '\0';
  return SUCCESS;
}

INT frontend_loop() { return SUCCESS; }

INT poll_event(INT, INT, BOOL test) {
  if (test) return FALSE;
  if (!g_state.run_active || !g_state.runtime.enabled_for_run) {
    ss_sleep(10);
    return FALSE;
  }
  if (!g_state.runtime.acquisition_running || !g_state.tcp ||
      !g_state.parser)
    return FALSE;
  if (!g_state.pending_events.empty()) return TRUE;

  std::vector<std::uint8_t> chunk;
  try {
    if (!g_state.tcp->dataAvailable(0)) return FALSE;
    chunk = g_state.tcp->receive(kReceiveChunkBytes, kReceiveTimeoutMs);
  } catch (const std::exception& exception) {
    const std::string message = exception.what();
    if (message.find("timeout") != std::string::npos)
      ++g_state.runtime.statistics.receive_timeout_count;
    else
      ++g_state.runtime.statistics.tcp_error_count;
    handle_acquisition_error(message);
    return FALSE;
  }

  g_state.runtime.statistics.received_bytes += chunk.size();
  ++g_state.runtime.statistics.receive_chunks;
  std::vector<easiroc::Event> events;
  try {
    events = g_state.parser->push(chunk);
  } catch (const std::exception& exception) {
    ++g_state.runtime.statistics.decode_error_count;
    handle_acquisition_error(exception.what());
    return FALSE;
  }

  for (const auto& event : events) {
    for (const auto& word : event.data) {
      if ((word.type == easiroc::DataType::kAdcHighGain ||
           word.type == easiroc::DataType::kAdcLowGain) &&
          word.overflow)
        ++g_state.runtime.statistics.adc_overflow_count;
    }
    try {
      g_state.pending_events.push_back(easiroc::organizeEvent(event));
    } catch (const std::exception& exception) {
      ++g_state.runtime.statistics.event_content_error_count;
      handle_acquisition_error(exception.what());
      return FALSE;
    }
  }
  return g_state.pending_events.empty() ? FALSE : TRUE;
}

INT interrupt_configure(INT, INT, PTYPE) { return SUCCESS; }

INT read_physics_event(char* pevent, INT) {
  if (!g_state.run_active || !g_state.runtime.enabled_for_run ||
      g_state.pending_events.empty())
    return 0;

  const easiroc::DecodedEvent event =
      std::move(g_state.pending_events.front());
  g_state.pending_events.pop_front();
  const easiroc::BankPayloads payloads = easiroc::makeBankPayloads(event);

  static_assert(sizeof(WORD) == sizeof(std::uint16_t),
                "MIDAS WORD must be 16 bits");
  bk_init(pevent);

  WORD* data = nullptr;
  bk_create(pevent, "EAHG", TID_WORD, reinterpret_cast<void**>(&data));
  for (const auto value : payloads.high_gain) *data++ = value;
  bk_close(pevent, data);

  // EALG payload generation and validation are active, but writing this bank
  // is intentionally disabled until low-gain storage is requested.
  if constexpr (kWriteLowGainBank) {
    bk_create(pevent, "EALG", TID_WORD, reinterpret_cast<void**>(&data));
    for (const auto value : payloads.low_gain) *data++ = value;
    bk_close(pevent, data);
  }

  bk_create(pevent, "ETLE", TID_WORD, reinterpret_cast<void**>(&data));
  for (const auto value : payloads.leading) *data++ = value;
  bk_close(pevent, data);

  bk_create(pevent, "ETTR", TID_WORD, reinterpret_cast<void**>(&data));
  for (const auto value : payloads.trailing) *data++ = value;
  bk_close(pevent, data);

  ++g_state.runtime.event_counter;
  return bk_size(pevent);
}

INT read_status_event(char*, INT) {
  publish_completed_diagnostic();
  publish_runtime_variables();

  if (g_state.run_active) return 0;

  FrontendSettings settings;
  if (read_settings(&settings) == SUCCESS) {
    if (settings.enabled) {
      start_diagnostic(settings.ip_address);
    } else {
      set_disabled_runtime_state();
      publish_runtime_variables();
    }
  }

  // Status is published directly into ODB. Returning zero suppresses an empty
  // MIDAS event; no TCP stream data is received here.
  return 0;
}

INT read_configuration_event(char* pevent, INT) {
  if (!g_state.run_snapshot.frontend_bor_complete) return 0;

  HNDLE key = 0;
  const INT find_status = db_find_key(hDB, 0, kRunSnapshotPath, &key);
  if (find_status != DB_SUCCESS) {
    cm_msg(MERROR, "read_configuration_event",
           "Cannot find completed EASIROC RunSnapshot: status %d",
           find_status);
    return 0;
  }

  char* json = nullptr;
  int json_capacity = 0;
  int json_length = 0;
  const INT json_status =
      db_copy_json_save(hDB, key, &json, &json_capacity, &json_length);
  if (json_status != DB_SUCCESS || json == nullptr || json_length <= 0) {
    cm_msg(MERROR, "read_configuration_event",
           "Cannot serialize EASIROC RunSnapshot JSON: status %d length %d",
           json_status, json_length);
    std::free(json);
    return 0;
  }
  if (json_length > max_event_size - 64) {
    cm_msg(MERROR, "read_configuration_event",
           "EASIROC RunSnapshot JSON is too large: %d bytes", json_length);
    std::free(json);
    return 0;
  }

  bk_init32(pevent);
  char* data = nullptr;
  bk_create(pevent, "ECFG", TID_CHAR, reinterpret_cast<void**>(&data));
  std::memcpy(data, json, static_cast<std::size_t>(json_length));
  bk_close(pevent, data + json_length);
  std::free(json);
  return bk_size(pevent);
}
