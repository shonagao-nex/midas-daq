#include "midas.h"
#include "mfe.h"

#include "easiroc_daq_control.h"
#include "easiroc_last_applied.h"
#include "easiroc_manual_apply.h"
#include "easiroc_readout.h"
#include "easiroc_run_settings.h"
#include "easiroc_status.h"
#include "easiroc_stream.h"
#include "rbcp.h"
#include "tcp_probe.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
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
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr char kSettingsPath[] = "/Equipment/EASIROC/Settings";
constexpr char kCommandsPath[] = "/Equipment/EASIROC/Commands";
constexpr char kInfoPath[] = "/Equipment/EASIROC/Info";
constexpr char kReadbackPath[] = "/Equipment/EASIROC/Readback";
constexpr char kVariablesPath[] = "/Equipment/EASIROC/Variables";
constexpr char kRunSnapshotPath[] = "/Equipment/EASIROC/RunSnapshot";
constexpr DWORD kRunSnapshotSchemaVersion =
    easiroc::kEasirocRunSnapshotSchemaVersion;
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
  easiroc::AsicSlowControlSettings asic_slow_control;
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
  struct {
    bool attempted = false;
    bool sequence_succeeded = false;
    std::string error;
  } apply;
  easiroc::AsicSlowControlConsistencySnapshot consistency;
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
  easiroc::ManualApplyStatus asic_apply;
  easiroc::AppliedAsicSlowControlSettings last_applied;
  bool hardware_state_indeterminate = false;
  FirmwareObservation firmware_observation;
  EasirocRunSnapshot run_snapshot;

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

std::string odb_path(const char* base, std::string_view name) {
  return std::string(base) + "/" + std::string(name);
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

bool read_odb_dword(const std::string& path, DWORD* value) {
  INT size = sizeof(*value);
  const INT status =
      db_get_value(hDB, 0, path.c_str(), value, &size, TID_DWORD, FALSE);
  if (status != DB_SUCCESS) {
    cm_msg(MERROR, "read_odb", "Cannot read %s (status %d)", path.c_str(),
           status);
    return false;
  }
  return true;
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
  const std::string apply = odb_path(kRunSnapshotPath, "Apply");
  const std::string consistency = odb_path(kRunSnapshotPath, "Consistency");
  const std::string firmware = odb_path(kRunSnapshotPath, "Readback/Firmware");
  const BOOL enabled_for_run = snapshot.enabled_for_run ? TRUE : FALSE;
  const BOOL requested_enabled = snapshot.requested.enabled ? TRUE : FALSE;
  const BOOL adc = snapshot.requested.enables.adc ? TRUE : FALSE;
  const BOOL tdc = snapshot.requested.enables.tdc ? TRUE : FALSE;
  const BOOL scaler = snapshot.requested.enables.scaler ? TRUE : FALSE;
  const BOOL apply_at_bor =
      snapshot.requested.asic_slow_control.apply_at_bor ? TRUE : FALSE;
  const INT asic1_dac_code =
      snapshot.requested.asic_slow_control.asic[0].dac_code;
  const INT asic1_dac_slope =
      snapshot.requested.asic_slow_control.asic[0].dac_slope;
  const INT asic2_dac_code =
      snapshot.requested.asic_slow_control.asic[1].dac_code;
  const INT asic2_dac_slope =
      snapshot.requested.asic_slow_control.asic[1].dac_slope;
  const auto& asic1_input_dac =
      snapshot.requested.asic_slow_control.asic[0].input_dac;
  const auto& asic2_input_dac =
      snapshot.requested.asic_slow_control.asic[1].input_dac;
  const BOOL apply_attempted = snapshot.apply.attempted ? TRUE : FALSE;
  const BOOL apply_succeeded =
      snapshot.apply.sequence_succeeded ? TRUE : FALSE;
  const BOOL last_applied_valid =
      snapshot.consistency.last_applied_valid ? TRUE : FALSE;
  const BOOL configuration_match =
      snapshot.consistency.configuration_match ? TRUE : FALSE;
  const BOOL hardware_state_indeterminate =
      snapshot.consistency.hardware_state_indeterminate ? TRUE : FALSE;
  const DWORD last_applied_request_id =
      snapshot.consistency.last_applied_request_id;
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
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[0]),
           &apply_at_bor, sizeof(apply_at_bor), 1, TID_BOOL) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[1]),
           &asic1_dac_code, sizeof(asic1_dac_code), 1, TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[2]),
           &asic1_dac_slope, sizeof(asic1_dac_slope), 1, TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[3]),
           &asic2_dac_code, sizeof(asic2_dac_code), 1, TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[4]),
           &asic2_dac_slope, sizeof(asic2_dac_slope), 1, TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[5]),
           asic1_input_dac.data(), sizeof(asic1_input_dac),
           asic1_input_dac.size(), TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[6]),
           asic2_input_dac.data(), sizeof(asic2_input_dac),
           asic2_input_dac.size(), TID_INT) && ok;

  ok = set_odb_value(odb_path(apply.c_str(), "Attempted"),
                     &apply_attempted, sizeof(apply_attempted), 1,
                     TID_BOOL) && ok;
  ok = set_odb_value(odb_path(apply.c_str(), "SequenceSucceeded"),
                     &apply_succeeded, sizeof(apply_succeeded), 1,
                     TID_BOOL) && ok;
  ok = set_odb_string(odb_path(apply.c_str(), "Error"),
                      snapshot.apply.error, 256) && ok;

  ok = set_odb_value(odb_path(consistency.c_str(), "LastAppliedValid"),
                     &last_applied_valid, sizeof(last_applied_valid), 1,
                     TID_BOOL) && ok;
  ok = set_odb_value(odb_path(consistency.c_str(), "ConfigurationMatch"),
                     &configuration_match, sizeof(configuration_match), 1,
                     TID_BOOL) && ok;
  ok = set_odb_value(
           odb_path(consistency.c_str(), "HardwareStateIndeterminate"),
           &hardware_state_indeterminate,
           sizeof(hardware_state_indeterminate), 1, TID_BOOL) &&
       ok;
  ok = set_odb_value(odb_path(consistency.c_str(), "LastAppliedRequestId"),
                     &last_applied_request_id,
                     sizeof(last_applied_request_id), 1, TID_DWORD) && ok;
  ok = set_odb_value(odb_path(consistency.c_str(), "LastAppliedUnixTime"),
                     &snapshot.consistency.last_applied_unix_time,
                     sizeof(snapshot.consistency.last_applied_unix_time), 1,
                     TID_QWORD) && ok;
  ok = set_odb_string(
           odb_path(consistency.c_str(), "Status"),
           easiroc::hardwareConfigurationStatusName(
               snapshot.consistency.status),
           32) && ok;
  ok = set_odb_string(odb_path(consistency.c_str(), "Detail"),
                      snapshot.consistency.detail, 256) && ok;

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
  const std::string asic_slow_control =
      odb_path(kVariablesPath, "ASICSlowControl");
  const auto configuration = easiroc::compareAsicSlowControlSettings(
      g_state.settings.asic_slow_control, g_state.last_applied,
      g_state.hardware_state_indeterminate);
  const BOOL configuration_match =
      configuration.status == easiroc::HardwareConfigurationStatus::kMatch
          ? TRUE
          : FALSE;
  const BOOL hardware_state_indeterminate =
      g_state.hardware_state_indeterminate ? TRUE : FALSE;
  const DWORD active_request_id = g_state.asic_apply.active_request_id;
  const DWORD last_handled_request_id =
      g_state.asic_apply.last_handled_request_id;
  const DWORD last_successful_request_id =
      g_state.asic_apply.last_successful_request_id;
  const BOOL apply_in_progress =
      g_state.asic_apply.apply_in_progress ? TRUE : FALSE;
  const BOOL last_attempt_succeeded =
      g_state.asic_apply.last_attempt_succeeded ? TRUE : FALSE;
  ok = set_odb_value(
           odb_path(asic_slow_control.c_str(), "ActiveRequestId"),
           &active_request_id, sizeof(active_request_id), 1, TID_DWORD) && ok;
  ok = set_odb_value(
           odb_path(asic_slow_control.c_str(), "LastHandledRequestId"),
           &last_handled_request_id, sizeof(last_handled_request_id), 1,
           TID_DWORD) && ok;
  ok = set_odb_value(
           odb_path(asic_slow_control.c_str(), "LastSuccessfulRequestId"),
           &last_successful_request_id, sizeof(last_successful_request_id), 1,
           TID_DWORD) && ok;
  ok = set_odb_string(
           odb_path(asic_slow_control.c_str(), "ApplyState"),
           easiroc::manualApplyStateName(g_state.asic_apply.state), 32) && ok;
  ok = set_odb_value(
           odb_path(asic_slow_control.c_str(), "ApplyInProgress"),
           &apply_in_progress, sizeof(apply_in_progress), 1, TID_BOOL) && ok;
  ok = set_odb_value(
           odb_path(asic_slow_control.c_str(), "LastAttemptSucceeded"),
           &last_attempt_succeeded, sizeof(last_attempt_succeeded), 1,
           TID_BOOL) && ok;
  ok = set_odb_string(
           odb_path(asic_slow_control.c_str(), "LastApplyError"),
           g_state.asic_apply.last_apply_error, 256) && ok;
  PUBLISH_QWORD(asic_slow_control.c_str(), "LastApplyUnixTime",
                g_state.asic_apply.last_apply_unix_time);
  ok = set_odb_value(
           odb_path(asic_slow_control.c_str(), "ConfigurationMatch"),
           &configuration_match, sizeof(configuration_match), 1, TID_BOOL) &&
       ok;
  ok = set_odb_string(
           odb_path(asic_slow_control.c_str(), "ConfigurationStatus"),
           easiroc::hardwareConfigurationStatusName(configuration.status),
           32) && ok;
  ok = set_odb_string(
           odb_path(asic_slow_control.c_str(), "ConfigurationDetail"),
           configuration.detail, 256) && ok;
  ok = set_odb_value(
           odb_path(asic_slow_control.c_str(), "HardwareStateIndeterminate"),
           &hardware_state_indeterminate,
           sizeof(hardware_state_indeterminate), 1, TID_BOOL) &&
       ok;
#undef PUBLISH_QWORD
#undef PUBLISH_BOOL
  return ok;
}

bool initialize_manual_apply_mailbox() {
  const DWORD zero = 0;
  const BOOL no = FALSE;
  std::array<char, 32> idle{};
  std::snprintf(idle.data(), idle.size(), "%s", "Idle");
  std::array<char, 256> empty_error{};
  const std::uint64_t zero_time = 0;
  const std::string command =
      odb_path(kCommandsPath, "ASICSlowControl/ApplyRequestId");
  const std::string status =
      odb_path(kVariablesPath, "ASICSlowControl");

  if (!ensure_odb_value(command, &zero, sizeof(zero), 1, TID_DWORD) ||
      !ensure_odb_value(odb_path(status.c_str(), "ActiveRequestId"), &zero,
                        sizeof(zero), 1, TID_DWORD) ||
      !ensure_odb_value(odb_path(status.c_str(), "LastHandledRequestId"),
                        &zero, sizeof(zero), 1, TID_DWORD) ||
      !ensure_odb_value(odb_path(status.c_str(), "LastSuccessfulRequestId"),
                        &zero, sizeof(zero), 1, TID_DWORD) ||
      !ensure_odb_value(odb_path(status.c_str(), "ApplyState"), idle.data(),
                        idle.size(), 1, TID_STRING) ||
      !ensure_odb_value(odb_path(status.c_str(), "ApplyInProgress"), &no,
                        sizeof(no), 1, TID_BOOL) ||
      !ensure_odb_value(odb_path(status.c_str(), "LastAttemptSucceeded"),
                        &no, sizeof(no), 1, TID_BOOL) ||
      !ensure_odb_value(odb_path(status.c_str(), "LastApplyError"),
                        empty_error.data(), empty_error.size(), 1,
                        TID_STRING) ||
      !ensure_odb_value(odb_path(status.c_str(), "LastApplyUnixTime"),
                        &zero_time, sizeof(zero_time), 1, TID_QWORD))
    return false;

  DWORD request_id = 0;
  DWORD last_handled_request_id = 0;
  DWORD last_successful_request_id = 0;
  if (!read_odb_dword(command, &request_id) ||
      !read_odb_dword(odb_path(status.c_str(), "LastHandledRequestId"),
                      &last_handled_request_id) ||
      !read_odb_dword(odb_path(status.c_str(), "LastSuccessfulRequestId"),
                      &last_successful_request_id))
    return false;

  g_state.asic_apply = {};
  g_state.asic_apply.last_handled_request_id = last_handled_request_id;
  g_state.asic_apply.last_successful_request_id =
      last_successful_request_id;
  if (request_id > last_handled_request_id) {
    const std::time_t now = std::time(nullptr);
    const std::uint64_t unix_time =
        now < 0 ? 0 : static_cast<std::uint64_t>(now);
    g_state.asic_apply = easiroc::acknowledgeStaleManualApplyRequest(
        g_state.asic_apply, request_id, unix_time);
    cm_msg(MINFO, "frontend_init", "WARNING: %s (request %u)",
           g_state.asic_apply.last_apply_error.c_str(),
           static_cast<unsigned>(request_id));
  }
  return true;
}

bool initialize_last_applied_odb() {
  const std::string status = odb_path(kVariablesPath, "ASICSlowControl");
  const std::string last_applied = odb_path(status.c_str(), "LastApplied");
  const BOOL no = FALSE;
  const DWORD zero_request = 0;
  const std::uint64_t zero_time = 0;
  const INT zero_value = 0;
  const std::array<INT, easiroc::kInputDacChannelCount> zero_input_dac{};
  std::array<char, 32> unknown{};
  std::snprintf(unknown.data(), unknown.size(), "%s", "Unknown");
  std::array<char, 256> empty_detail{};

  return ensure_odb_value(odb_path(last_applied.c_str(), "Valid"), &no,
                          sizeof(no), 1, TID_BOOL) &&
         ensure_odb_value(odb_path(last_applied.c_str(), "RequestId"),
                          &zero_request, sizeof(zero_request), 1,
                          TID_DWORD) &&
         ensure_odb_value(odb_path(last_applied.c_str(), "ApplyUnixTime"),
                          &zero_time, sizeof(zero_time), 1, TID_QWORD) &&
         ensure_odb_value(
             odb_path(last_applied.c_str(), "ASIC1/DiscriminatorDACCode"),
             &zero_value, sizeof(zero_value), 1, TID_INT) &&
         ensure_odb_value(
             odb_path(last_applied.c_str(), "ASIC1/DiscriminatorDACSlope"),
             &zero_value, sizeof(zero_value), 1, TID_INT) &&
         ensure_odb_value(odb_path(last_applied.c_str(), "ASIC1/InputDAC"),
                          zero_input_dac.data(), sizeof(zero_input_dac),
                          zero_input_dac.size(), TID_INT) &&
         ensure_odb_value(
             odb_path(last_applied.c_str(), "ASIC2/DiscriminatorDACCode"),
             &zero_value, sizeof(zero_value), 1, TID_INT) &&
         ensure_odb_value(
             odb_path(last_applied.c_str(), "ASIC2/DiscriminatorDACSlope"),
             &zero_value, sizeof(zero_value), 1, TID_INT) &&
         ensure_odb_value(odb_path(last_applied.c_str(), "ASIC2/InputDAC"),
                          zero_input_dac.data(), sizeof(zero_input_dac),
                          zero_input_dac.size(), TID_INT) &&
         ensure_odb_value(odb_path(status.c_str(), "ConfigurationMatch"),
                          &no, sizeof(no), 1, TID_BOOL) &&
         ensure_odb_value(odb_path(status.c_str(), "ConfigurationStatus"),
                          unknown.data(), unknown.size(), 1, TID_STRING) &&
         ensure_odb_value(odb_path(status.c_str(), "ConfigurationDetail"),
                          empty_detail.data(), empty_detail.size(), 1,
                          TID_STRING) &&
         ensure_odb_value(
             odb_path(status.c_str(), "HardwareStateIndeterminate"), &no,
             sizeof(no), 1, TID_BOOL);
}

bool initialize_odb() {
  char default_ip[64] = {};
  std::snprintf(default_ip, sizeof(default_ip), "%s", kDefaultIpAddress);
  const BOOL yes = TRUE;
  const BOOL no = FALSE;
  const INT default_dac_code = easiroc::kDefaultDiscriminatorDacCode;
  const INT default_dac_slope = easiroc::kDefaultDiscriminatorDacSlope;
  const auto default_input_dac = easiroc::defaultInputDacValues();
  if (!ensure_odb_value(odb_path(kSettingsPath, "Enabled"),
                        &yes, sizeof(yes), 1, TID_BOOL) ||
      !ensure_odb_value(odb_path(kSettingsPath, "Network/IPAddress"),
                        default_ip, sizeof(default_ip), 1, TID_STRING) ||
      !ensure_odb_value(odb_path(kSettingsPath, "Acquisition/ADCEnabled"),
                        &yes, sizeof(yes), 1, TID_BOOL) ||
      !ensure_odb_value(odb_path(kSettingsPath, "Acquisition/TDCEnabled"),
                        &yes, sizeof(yes), 1, TID_BOOL) ||
      !ensure_odb_value(odb_path(kSettingsPath, "Acquisition/ScalerEnabled"),
                        &no, sizeof(no), 1, TID_BOOL) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[0]),
          &no, sizeof(no), 1, TID_BOOL) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[1]),
          &default_dac_code, sizeof(default_dac_code), 1, TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[2]),
          &default_dac_slope, sizeof(default_dac_slope), 1, TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[3]),
          &default_dac_code, sizeof(default_dac_code), 1, TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[4]),
          &default_dac_slope, sizeof(default_dac_slope), 1, TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[5]),
          default_input_dac.data(), sizeof(default_input_dac),
          default_input_dac.size(), TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[6]),
          default_input_dac.data(), sizeof(default_input_dac),
          default_input_dac.size(), TID_INT))
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
  if (!initialize_manual_apply_mailbox() || !initialize_last_applied_odb())
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

INT read_int_setting(const char* name, int* value) {
  INT odb_value = static_cast<INT>(*value);
  INT size = sizeof(odb_value);
  const INT status =
      db_get_value(hDB, 0, name, &odb_value, &size, TID_INT, FALSE);
  if (status != DB_SUCCESS) {
    cm_msg(MERROR, "read_settings", "Cannot read %s (status %d)", name,
           status);
    return FE_ERR_ODB;
  }
  *value = odb_value;
  return SUCCESS;
}

INT read_input_dac_setting(
    const char* name,
    std::array<int, easiroc::kInputDacChannelCount>* values) {
  std::array<INT, easiroc::kInputDacChannelCount> odb_values{};
  INT size = sizeof(odb_values);
  const INT status =
      db_get_value(hDB, 0, name, odb_values.data(), &size, TID_INT, FALSE);
  if (status != DB_SUCCESS || size != static_cast<INT>(sizeof(odb_values))) {
    cm_msg(MERROR, "read_settings",
           "Cannot read 32-element INT array %s (status %d, size %d)", name,
           status, size);
    return FE_ERR_ODB;
  }
  std::copy(odb_values.begin(), odb_values.end(), values->begin());
  return SUCCESS;
}

bool read_odb_qword(const std::string& path, std::uint64_t* value) {
  INT size = sizeof(*value);
  const INT status =
      db_get_value(hDB, 0, path.c_str(), value, &size, TID_QWORD, FALSE);
  if (status != DB_SUCCESS) {
    cm_msg(MERROR, "read_odb", "Cannot read %s (status %d)", path.c_str(),
           status);
    return false;
  }
  return true;
}

bool read_last_applied_settings(
    easiroc::AppliedAsicSlowControlSettings* result,
    bool* hardware_state_indeterminate) {
  const std::string status = odb_path(kVariablesPath, "ASICSlowControl");
  const std::string last_applied_path =
      odb_path(status.c_str(), "LastApplied");
  easiroc::AppliedAsicSlowControlSettings next;
  bool valid = false;
  std::string path = odb_path(last_applied_path.c_str(), "Valid");
  if (read_bool_setting(path.c_str(), &valid) != SUCCESS ||
      !read_odb_dword(odb_path(last_applied_path.c_str(), "RequestId"),
                      &next.request_id) ||
      !read_odb_qword(odb_path(last_applied_path.c_str(), "ApplyUnixTime"),
                      &next.apply_unix_time))
    return false;
  next.valid = valid;

  for (std::size_t asic = 0; asic < next.asic.size(); ++asic) {
    const std::string prefix = "ASIC" + std::to_string(asic + 1) + "/";
    path = odb_path(last_applied_path.c_str(),
                    prefix + "DiscriminatorDACCode");
    if (read_int_setting(path.c_str(), &next.asic[asic].dac_code) != SUCCESS)
      return false;
    path = odb_path(last_applied_path.c_str(),
                    prefix + "DiscriminatorDACSlope");
    if (read_int_setting(path.c_str(), &next.asic[asic].dac_slope) != SUCCESS)
      return false;
    path = odb_path(last_applied_path.c_str(), prefix + "InputDAC");
    if (read_input_dac_setting(path.c_str(), &next.asic[asic].input_dac) !=
        SUCCESS)
      return false;
  }

  bool indeterminate = false;
  path = odb_path(status.c_str(), "HardwareStateIndeterminate");
  if (read_bool_setting(path.c_str(), &indeterminate) != SUCCESS) return false;
  *result = next;
  *hardware_state_indeterminate = indeterminate;
  return true;
}

bool load_last_applied_settings() {
  return read_last_applied_settings(&g_state.last_applied,
                                    &g_state.hardware_state_indeterminate);
}

bool publish_last_applied_settings(
    const easiroc::AppliedAsicSlowControlSettings& last_applied) {
  const std::string base =
      odb_path(kVariablesPath, "ASICSlowControl/LastApplied");
  const BOOL invalid = FALSE;
  const BOOL valid = last_applied.valid ? TRUE : FALSE;
  const DWORD request_id = last_applied.request_id;
  bool ok = set_odb_value(odb_path(base.c_str(), "Valid"), &invalid,
                          sizeof(invalid), 1, TID_BOOL);
  ok = set_odb_value(odb_path(base.c_str(), "RequestId"), &request_id,
                     sizeof(request_id), 1, TID_DWORD) && ok;
  ok = set_odb_value(odb_path(base.c_str(), "ApplyUnixTime"),
                     &last_applied.apply_unix_time,
                     sizeof(last_applied.apply_unix_time), 1, TID_QWORD) && ok;
  for (std::size_t asic = 0; asic < last_applied.asic.size(); ++asic) {
    const std::string prefix = "ASIC" + std::to_string(asic + 1) + "/";
    const INT dac_code = last_applied.asic[asic].dac_code;
    const INT dac_slope = last_applied.asic[asic].dac_slope;
    ok = set_odb_value(
             odb_path(base.c_str(), prefix + "DiscriminatorDACCode"),
             &dac_code, sizeof(dac_code), 1, TID_INT) && ok;
    ok = set_odb_value(
             odb_path(base.c_str(), prefix + "DiscriminatorDACSlope"),
             &dac_slope, sizeof(dac_slope), 1, TID_INT) && ok;
    ok = set_odb_value(odb_path(base.c_str(), prefix + "InputDAC"),
                       last_applied.asic[asic].input_dac.data(),
                       sizeof(last_applied.asic[asic].input_dac),
                       last_applied.asic[asic].input_dac.size(), TID_INT) && ok;
  }
  if (ok)
    ok = set_odb_value(odb_path(base.c_str(), "Valid"), &valid,
                       sizeof(valid), 1, TID_BOOL);
  return ok;
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

  path = odb_path(kSettingsPath,
                  easiroc::kAsicSlowControlRequestedSnapshotPaths[0]);
  status = read_bool_setting(path.c_str(),
                             &next.asic_slow_control.apply_at_bor);
  if (status != SUCCESS) return status;

  for (std::size_t asic = 0; asic < next.asic_slow_control.asic.size(); ++asic) {
    const std::size_t first_path = 1 + asic * 2;
    path = odb_path(kSettingsPath,
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[first_path]);
    status = read_int_setting(
        path.c_str(), &next.asic_slow_control.asic[asic].dac_code);
    if (status != SUCCESS) return status;
    path = odb_path(
        kSettingsPath,
        easiroc::kAsicSlowControlRequestedSnapshotPaths[first_path + 1]);
    status = read_int_setting(
        path.c_str(), &next.asic_slow_control.asic[asic].dac_slope);
    if (status != SUCCESS) return status;

    path = odb_path(
        kSettingsPath,
        easiroc::kAsicSlowControlRequestedSnapshotPaths[5 + asic]);
    status = read_input_dac_setting(
        path.c_str(), &next.asic_slow_control.asic[asic].input_dac);
    if (status != SUCCESS) return status;
  }

  *settings = next;
  return SUCCESS;
}

easiroc::ManualApplyRunState read_manual_apply_run_state(bool* ok) {
  INT run_state = 0;
  INT size = sizeof(run_state);
  const INT status = db_get_value(hDB, 0, "/Runinfo/State", &run_state, &size,
                                  TID_INT, FALSE);
  if (status != DB_SUCCESS) {
    cm_msg(MERROR, "manual_apply", "Cannot read /Runinfo/State (status %d)",
           status);
    *ok = false;
    return easiroc::ManualApplyRunState::kUnknown;
  }
  *ok = true;
  switch (run_state) {
    case STATE_STOPPED:
      return easiroc::ManualApplyRunState::kStopped;
    case STATE_RUNNING:
      return easiroc::ManualApplyRunState::kRunning;
    case STATE_PAUSED:
      return easiroc::ManualApplyRunState::kPaused;
    default:
      return easiroc::ManualApplyRunState::kUnknown;
  }
}

void publish_completed_diagnostic();

void process_manual_apply_request() {
  const std::string request_path =
      odb_path(kCommandsPath, "ASICSlowControl/ApplyRequestId");
  DWORD request_id = 0;
  if (!read_odb_dword(request_path, &request_id) ||
      request_id <= g_state.asic_apply.last_handled_request_id)
    return;

  const bool another_apply_in_progress = g_state.asic_apply.apply_in_progress;
  const auto transition =
      easiroc::beginManualApplyRequest(g_state.asic_apply, request_id);
  if (!transition.handled) return;
  g_state.asic_apply = transition.pending;
  publish_runtime_variables();

  // Serialize manual writes with the read-only startup/status diagnostic so
  // two RBCP clients in this frontend never access the device concurrently.
  if (g_diagnostic.thread.joinable()) g_diagnostic.thread.join();
  publish_completed_diagnostic();

  const std::time_t now = std::time(nullptr);
  const std::uint64_t unix_time =
      now < 0 ? 0 : static_cast<std::uint64_t>(now);
  bool run_state_ok = false;
  const auto run_state = read_manual_apply_run_state(&run_state_ok);
  if (!run_state_ok) {
    g_state.asic_apply = easiroc::rejectManualApplyRequest(
        g_state.asic_apply,
        "Cannot verify Run state for manual ASIC slow-control apply",
        unix_time);
    publish_runtime_variables();
    return;
  }

  FrontendSettings apply_settings;
  if (read_settings(&apply_settings) != SUCCESS) {
    g_state.asic_apply = easiroc::rejectManualApplyRequest(
        g_state.asic_apply,
        "Cannot snapshot EASIROC Settings for manual slow-control apply",
        unix_time);
    publish_runtime_variables();
    return;
  }
  // This local copy is the complete immutable request snapshot. Neither the
  // image nor LastApplied is populated from live ODB after this point.
  g_state.settings = apply_settings;

  const easiroc::ManualApplyRequestContext context{
      run_state, apply_settings.enabled, another_apply_in_progress};
  std::unique_ptr<RbcpClient> manual_rbcp;
  const auto result = easiroc::executeManualApplyBackend(
      g_state.asic_apply, context, apply_settings.asic_slow_control,
      g_state.last_applied, g_state.hardware_state_indeterminate, unix_time,
      [](const easiroc::ManualApplyStatus& status) {
        g_state.asic_apply = status;
        publish_runtime_variables();
      },
      [&](std::uint32_t address, const std::vector<std::uint8_t>& data) {
        if (!manual_rbcp)
          manual_rbcp = std::make_unique<RbcpClient>(apply_settings.ip_address,
                                                     kRbcpPort);
        cm_msg(MINFO, "manual_apply",
               "ASIC slow-control RBCP write: address 0x%08x, %zu byte(s)",
               static_cast<unsigned>(address), data.size());
        manual_rbcp->write(address, data);
      },
      [](unsigned milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
      });

  g_state.hardware_state_indeterminate =
      result.hardware_state_indeterminate;
  g_state.asic_apply = result.terminal;
  if (result.last_applied_changed) {
    if (publish_last_applied_settings(result.last_applied)) {
      g_state.last_applied = result.last_applied;
    } else {
      g_state.asic_apply.last_successful_request_id =
          transition.pending.last_successful_request_id;
      g_state.asic_apply.state = easiroc::ManualApplyState::kFailed;
      g_state.asic_apply.last_attempt_succeeded = false;
      g_state.asic_apply.last_apply_error =
          "ASIC slow-control sequence succeeded, but LastApplied could not "
          "be saved to ODB";
      load_last_applied_settings();
    }
  }
  publish_runtime_variables();
  if (g_state.asic_apply.state == easiroc::ManualApplyState::kSucceeded) {
    cm_msg(MINFO, "manual_apply",
           "Request %u ASIC slow-control seven-transaction sequence "
           "succeeded; LastApplied records transmitted settings, not ASIC "
           "readback",
           static_cast<unsigned>(request_id));
  } else {
    cm_msg(MINFO, "manual_apply", "WARNING: Request %u %s: %s",
           static_cast<unsigned>(request_id),
           easiroc::manualApplyStateName(g_state.asic_apply.state),
           g_state.asic_apply.last_apply_error.c_str());
  }
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

void populate_run_snapshot(const FrontendSettings& settings) {
  auto& snapshot = g_state.run_snapshot;
  snapshot.requested = settings;
  snapshot.enabled_for_run = settings.enabled;
  snapshot.firmware =
      settings.enabled && g_state.firmware_observation.valid &&
              g_state.firmware_observation.observed_ip_address ==
                  settings.ip_address
          ? g_state.firmware_observation
          : FirmwareObservation{};
}

bool finalize_run_snapshot(const FrontendSettings& settings) {
  auto& snapshot = g_state.run_snapshot;
  populate_run_snapshot(settings);
  snapshot.frontend_bor_complete = true;
  if (publish_run_snapshot()) return true;
  snapshot.frontend_bor_complete = false;
  return false;
}

void publish_failed_run_snapshot(const FrontendSettings& settings) {
  populate_run_snapshot(settings);
  g_state.run_snapshot.frontend_bor_complete = false;
  if (!publish_run_snapshot())
    cm_msg(MERROR, "begin_of_run",
           "Cannot publish failed EASIROC RunSnapshot");
}

void warn_for_bor_consistency(
    const easiroc::AsicSlowControlConsistencySnapshot& consistency) {
  switch (consistency.status) {
    case easiroc::HardwareConfigurationStatus::kMismatch:
      cm_msg(MINFO, "begin_of_run",
             "WARNING: EASIROC slow-control Settings differ from "
             "LastApplied: %s",
             consistency.detail.c_str());
      return;
    case easiroc::HardwareConfigurationStatus::kUnknown:
      cm_msg(MINFO, "begin_of_run",
             "WARNING: EASIROC slow-control has no valid LastApplied "
             "configuration");
      return;
    case easiroc::HardwareConfigurationStatus::kIndeterminate:
      cm_msg(MINFO, "begin_of_run",
             "WARNING: EASIROC slow-control hardware state is "
             "indeterminate");
      return;
    case easiroc::HardwareConfigurationStatus::kMatch:
      return;
  }
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
  if (!load_last_applied_settings()) return FE_ERR_ODB;
  if (!publish_runtime_variables()) return FE_ERR_ODB;
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

  if (const auto validation_error =
          easiroc::validateAsicSlowControlBorSettings(
              run_settings.enabled, run_settings.asic_slow_control)) {
    cm_msg(MERROR, "begin_of_run", "%s", validation_error->c_str());
    if (error != nullptr)
      std::snprintf(error, 256, "%s", validation_error->c_str());
    g_state.run_snapshot.requested = run_settings;
    publish_failed_run_snapshot(run_settings);
    publish_runtime_variables();
    mark_configuration_failed(run_number);
    return FE_ERR_ODB;
  }

  easiroc::AppliedAsicSlowControlSettings bor_last_applied;
  bool bor_hardware_state_indeterminate = false;
  if (!read_last_applied_settings(&bor_last_applied,
                                  &bor_hardware_state_indeterminate)) {
    if (error != nullptr)
      std::snprintf(error, 256,
                    "Cannot read EASIROC LastApplied settings from ODB");
    publish_failed_run_snapshot(run_settings);
    publish_runtime_variables();
    mark_configuration_failed(run_number);
    return FE_ERR_ODB;
  }
  g_state.run_snapshot.consistency = easiroc::snapshotAsicSlowControlConsistency(
      run_settings.asic_slow_control, bor_last_applied,
      bor_hardware_state_indeterminate);
  if (easiroc::shouldWarnForAsicSlowControlConsistency(
          run_settings.enabled, g_state.run_snapshot.consistency)) {
    warn_for_bor_consistency(g_state.run_snapshot.consistency);
  }

  g_state.settings = run_settings;
  // Capture the validated requested configuration before any BOR hardware
  // access. The completed snapshot is published only after acquisition setup
  // succeeds, preserving FrontendBORComplete semantics.
  populate_run_snapshot(run_settings);
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
    publish_failed_run_snapshot(run_settings);
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
  process_manual_apply_request();
  publish_runtime_variables();

  if (g_state.run_active) return 0;

  FrontendSettings settings;
  if (read_settings(&settings) == SUCCESS) {
    g_state.settings = settings;
    publish_runtime_variables();
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
