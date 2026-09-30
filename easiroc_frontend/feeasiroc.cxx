#include "midas.h"
#include "mfe.h"

#include "easiroc_daq_control.h"
#include "easiroc_odb.h"
#include "easiroc_diagnostic_log.h"
#include "easiroc_last_applied.h"
#include "easiroc_manual_apply.h"
#include "easiroc_readout.h"
#include "easiroc_run_settings.h"
#include "easiroc_status.h"
#include "easiroc_stream.h"
#include "rbcp.h"
#include "tcp_probe.h"
#include "../common/manual_buffer_clear.h"

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

constexpr char kCommandsPath[] = "/Equipment/EASIROC/Commands";
constexpr char kVariablesPath[] = "/Equipment/EASIROC/Variables";
constexpr char kRunSnapshotPath[] = "/Equipment/EASIROC/RunSnapshot";
constexpr DWORD kRunSnapshotSchemaVersion =
    easiroc::kEasirocRunSnapshotSchemaVersion;
constexpr int kReceiveTimeoutMs = 100;
constexpr int kDrainQuietMs = 100;
constexpr int kDrainMaximumMs = 1000;
constexpr std::size_t kReceiveChunkBytes = 4096;
constexpr bool kWriteLowGainBank = false;

using easiroc_frontend::FrontendSettings;
using easiroc_frontend::RuntimeState;
using easiroc_frontend::FirmwareObservation;
using easiroc_frontend::EasirocRunSnapshot;
using easiroc_odb::odb_path;
using easiroc_odb::ensure_odb_value;
using easiroc_odb::read_odb_dword;
using easiroc_odb::publish_global_busy_ready;
using easiroc_odb::publish_configuration_status;
using easiroc_odb::initialize_acquisition_settings_odb;
using easiroc_odb::initialize_asic_settings_odb;
using easiroc_odb::initialize_hardware_info_odb;
using easiroc_odb::initialize_firmware_readback_odb;
using easiroc_odb::initialize_last_applied_odb;
using easiroc_odb::read_last_applied_settings;
using easiroc_odb::publish_last_applied_settings;
using easiroc_odb::read_settings;
using easiroc_odb::set_firmware_readback_valid;

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
  easiroc::DiagnosticLogPolicy diagnostic_log;
  easiroc::ManualApplyStatus asic_apply;
  daq::BufferClearStatus buffer_clear;
  std::string buffer_clear_result = "Not requested";
  std::uint64_t buffer_clear_drained_bytes = 0;
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

//************************************//
// Publish the current EASIROC run snapshot
//************************************//
bool publish_run_snapshot() {
  return easiroc_odb::publish_run_snapshot(g_state.run_snapshot);
}

//************************************//
// Evaluate runtime state and publish it to ODB
//************************************//
bool publish_runtime_variables() {
  const auto configuration = easiroc::compareAsicSlowControlSettings(
      g_state.settings.asic_slow_control, g_state.last_applied,
      g_state.hardware_state_indeterminate);
  const easiroc_odb::RuntimePublishView view{
      g_state.runtime, g_state.pending_events.size(),
      g_state.parser ? g_state.parser->bufferedBytes() : 0,
      configuration, g_state.hardware_state_indeterminate, g_state.asic_apply,
      g_state.buffer_clear, g_state.buffer_clear_result,
      g_state.buffer_clear_drained_bytes};
  return easiroc_odb::publish_runtime_variables(view);
}

bool initialize_manual_apply_mailbox() {
  const std::string command =
      odb_path(kCommandsPath, "ASICSlowControl/ApplyRequestId");
  const std::string status =
      odb_path(kVariablesPath, "ASICSlowControl");

  if (!easiroc_odb::initialize_manual_apply_mailbox_odb()) return false;

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

bool initialize_buffer_clear_mailbox() {
  const std::string command =
      odb_path(kCommandsPath, "BufferClearRequestId");
  const std::string status = odb_path(kVariablesPath, "BufferClear");
  if (!easiroc_odb::initialize_buffer_clear_mailbox_odb()) return false;

  DWORD request_id = 0;
  DWORD handled = 0;
  DWORD successful = 0;
  if (!read_odb_dword(command, &request_id) ||
      !read_odb_dword(odb_path(status.c_str(), "LastHandledRequestId"),
                      &handled) ||
      !read_odb_dword(odb_path(status.c_str(), "LastSuccessfulRequestId"),
                      &successful))
    return false;
  g_state.buffer_clear = {};
  g_state.buffer_clear.last_handled_request_id = handled;
  g_state.buffer_clear.last_successful_request_id = successful;
  if (request_id > handled) {
    const std::time_t now = std::time(nullptr);
    g_state.buffer_clear = daq::acknowledgeStaleBufferClearRequest(
        g_state.buffer_clear, request_id,
        now < 0 ? 0 : static_cast<std::uint64_t>(now));
    g_state.buffer_clear_result =
        "Not executed: stale startup request";
    cm_msg(MINFO, "frontend_init", "WARNING: %s (request %u)",
           g_state.buffer_clear.last_error.c_str(),
           static_cast<unsigned>(request_id));
  }
  return true;
}

//************************************//
// Initialize EASIROC settings and status in ODB
//************************************//
bool initialize_odb() {
  if (!initialize_acquisition_settings_odb() ||
      !initialize_asic_settings_odb() ||
      !initialize_hardware_info_odb() ||
      !initialize_firmware_readback_odb())
    return false;
  if (!initialize_manual_apply_mailbox() ||
      !initialize_buffer_clear_mailbox() || !initialize_last_applied_odb())
    return false;
  reset_run_snapshot(0);
  return publish_configuration_status(false, 0) &&
         publish_runtime_variables() && publish_run_snapshot();
}

bool load_last_applied_settings() {
  return read_last_applied_settings(&g_state.last_applied,
                                    &g_state.hardware_state_indeterminate);
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

//************************************//
// Carry the claimed request and prior apply state
//************************************//
struct PendingManualApplyRequest {
  DWORD request_id = 0;
  bool another_apply_in_progress = false;
  DWORD previous_successful_request_id = 0;
};

//************************************//
// Accept the next manual ASIC apply request
//************************************//
std::optional<PendingManualApplyRequest> accept_manual_apply_request() {
  const std::string request_path =
      odb_path(kCommandsPath, "ASICSlowControl/ApplyRequestId");
  DWORD request_id = 0;
  if (!read_odb_dword(request_path, &request_id) ||
      request_id <= g_state.asic_apply.last_handled_request_id)
    return std::nullopt;

  const bool another_apply_in_progress = g_state.asic_apply.apply_in_progress;
  const auto transition =
      easiroc::beginManualApplyRequest(g_state.asic_apply, request_id);
  if (!transition.handled) return std::nullopt;
  g_state.asic_apply = transition.pending;
  publish_runtime_variables();
  return PendingManualApplyRequest{
      request_id, another_apply_in_progress,
      transition.pending.last_successful_request_id};
}

struct ManualApplyPreparation {
  FrontendSettings settings;
  easiroc::ManualApplyRequestContext context;
};

//************************************//
// Snapshot run state and settings for one manual apply
//************************************//
std::optional<ManualApplyPreparation> prepare_manual_apply(
    bool another_apply_in_progress, std::uint64_t unix_time) {
  bool run_state_ok = false;
  const auto run_state = read_manual_apply_run_state(&run_state_ok);
  if (!run_state_ok) {
    g_state.asic_apply = easiroc::rejectManualApplyRequest(
        g_state.asic_apply,
        "Cannot verify Run state for manual ASIC slow-control apply",
        unix_time);
    publish_runtime_variables();
    return std::nullopt;
  }

  FrontendSettings apply_settings;
  if (read_settings(&apply_settings) != SUCCESS) {
    g_state.asic_apply = easiroc::rejectManualApplyRequest(
        g_state.asic_apply,
        "Cannot snapshot EASIROC Settings for manual slow-control apply",
        unix_time);
    publish_runtime_variables();
    return std::nullopt;
  }
  // This local copy is the complete immutable request snapshot. Neither the
  // image nor LastApplied is populated from live ODB after this point.
  g_state.settings = apply_settings;

  const easiroc::ManualApplyRequestContext context{
      run_state, apply_settings.enabled, another_apply_in_progress};
  return ManualApplyPreparation{std::move(apply_settings), context};
}

//************************************//
// Save the backend result and last transmitted ASIC settings
//************************************//
void record_manual_apply_result(const easiroc::ManualApplyBackendResult& result,
                                DWORD previous_successful_request_id) {
  g_state.hardware_state_indeterminate =
      result.hardware_state_indeterminate;
  g_state.asic_apply = result.terminal;
  if (result.last_applied_changed) {
    if (publish_last_applied_settings(result.last_applied)) {
      g_state.last_applied = result.last_applied;
    } else {
      g_state.asic_apply.last_successful_request_id =
          previous_successful_request_id;
      g_state.asic_apply.state = easiroc::ManualApplyState::kFailed;
      g_state.asic_apply.last_attempt_succeeded = false;
      g_state.asic_apply.last_apply_error =
          "ASIC slow-control sequence succeeded, but LastApplied could not "
          "be saved to ODB";
      load_last_applied_settings();
    }
  }
}

//************************************//
// Publish and log the terminal manual apply status
//************************************//
void publish_manual_apply_result(DWORD request_id) {
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

//************************************//
// Apply requested EASIROC settings while stopped
//************************************//
void process_manual_apply_request() {
  const auto request = accept_manual_apply_request();
  if (!request) return;

  // Serialize manual writes with the read-only startup/status diagnostic so
  // two RBCP clients in this frontend never access the device concurrently.
  if (g_diagnostic.thread.joinable()) g_diagnostic.thread.join();
  publish_completed_diagnostic();

  const std::time_t now = std::time(nullptr);
  const std::uint64_t unix_time =
      now < 0 ? 0 : static_cast<std::uint64_t>(now);
  const auto preparation =
      prepare_manual_apply(request->another_apply_in_progress, unix_time);
  if (!preparation) return;

  std::unique_ptr<RbcpClient> manual_rbcp;
  const auto result = easiroc::executeManualApplyBackend(
      g_state.asic_apply, preparation->context,
      preparation->settings.asic_slow_control,
      g_state.last_applied, g_state.hardware_state_indeterminate, unix_time,
      [](const easiroc::ManualApplyStatus& status) {
        g_state.asic_apply = status;
        publish_runtime_variables();
      },
      [&](std::uint32_t address, const std::vector<std::uint8_t>& data) {
        if (!manual_rbcp)
          manual_rbcp = std::make_unique<RbcpClient>(
              preparation->settings.ip_address, kRbcpPort);
        cm_msg(MINFO, "manual_apply",
               "ASIC slow-control RBCP write: address 0x%08x, %zu byte(s)",
               static_cast<unsigned>(address), data.size());
        manual_rbcp->write(address, data);
      },
      [](unsigned milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
      });

  record_manual_apply_result(result, request->previous_successful_request_id);
  publish_manual_apply_result(request->request_id);
}

//************************************//
// Accept and publish a new manual buffer-clear request
//************************************//
std::optional<DWORD> accept_manual_buffer_clear_request() {
  const std::string request_path =
      odb_path(kCommandsPath, "BufferClearRequestId");
  DWORD request_id = 0;
  if (!read_odb_dword(request_path, &request_id) ||
      request_id <= g_state.buffer_clear.last_handled_request_id)
    return std::nullopt;

  const auto transition =
      daq::beginBufferClearRequest(g_state.buffer_clear, request_id);
  if (!transition.handled) return std::nullopt;
  g_state.buffer_clear = transition.pending;
  g_state.buffer_clear_result = "Not attempted";
  g_state.buffer_clear_drained_bytes = 0;
  publish_runtime_variables();
  return request_id;
}

//************************************//
// Reject a manual buffer-clear request and publish its status
//************************************//
void reject_manual_buffer_clear_request(const std::string& error,
                                        std::uint64_t unix_time) {
  g_state.buffer_clear = daq::rejectBufferClearRequest(
      g_state.buffer_clear, error, unix_time);
  g_state.buffer_clear_result = "Not attempted: " + error;
  publish_runtime_variables();
}

//************************************//
// Verify STOPPED state and snapshot TCP settings for buffer clear
//************************************//
std::optional<FrontendSettings> prepare_manual_buffer_clear(
    DWORD request_id, std::uint64_t unix_time) {
  bool run_state_ok = false;
  const auto run_state = read_manual_apply_run_state(&run_state_ok);
  if (!run_state_ok || run_state != easiroc::ManualApplyRunState::kStopped) {
    const daq::BufferClearRunState clear_run_state =
        !run_state_ok
            ? daq::BufferClearRunState::kUnknown
            : (run_state == easiroc::ManualApplyRunState::kRunning
                   ? daq::BufferClearRunState::kRunning
                   : (run_state == easiroc::ManualApplyRunState::kPaused
                          ? daq::BufferClearRunState::kPaused
                          : daq::BufferClearRunState::kUnknown));
    const std::string error = daq::bufferClearRunStateRejection(
        clear_run_state, "EASIROC receive");
    reject_manual_buffer_clear_request(error, unix_time);
    cm_msg(MINFO, "manual_buffer_clear", "Request %u rejected: %s",
           static_cast<unsigned>(request_id), error.c_str());
    return std::nullopt;
  }
  if (g_state.run_active || g_state.runtime.acquisition_running ||
      g_state.tcp) {
    const std::string error =
        "Frontend acquisition state is active despite MIDAS STOPPED";
    reject_manual_buffer_clear_request(error, unix_time);
    return std::nullopt;
  }

  FrontendSettings settings;
  if (read_settings(&settings) != SUCCESS) {
    const std::string error =
        "Cannot snapshot EASIROC network settings";
    reject_manual_buffer_clear_request(error, unix_time);
    return std::nullopt;
  }
  return settings;
}

//************************************//
// Record a successful TCP drain and manual buffer-clear result
//************************************//
void complete_manual_buffer_clear_success(DWORD request_id,
                                          std::size_t drained,
                                          std::uint64_t unix_time) {
  g_state.buffer_clear_drained_bytes = drained;
  g_state.runtime.statistics.last_drain_bytes = drained;
  g_state.runtime.statistics.total_drain_bytes += drained;
  g_state.buffer_clear_result =
      "Succeeded: host TCP receive drain; no device FIFO-clear command "
      "issued; discarded " +
      std::to_string(drained) + " byte(s)";
  g_state.buffer_clear = daq::finishBufferClearRequest(
      g_state.buffer_clear, true, "", unix_time);
  cm_msg(MINFO, "manual_buffer_clear",
         "Request %u EASIROC TCP drain succeeded: discarded %zu byte(s); "
         "no device FIFO-clear, reset, or configuration command issued",
         static_cast<unsigned>(request_id), drained);
}

//************************************//
// Record a failed TCP drain and manual buffer-clear result
//************************************//
void complete_manual_buffer_clear_failure(DWORD request_id,
                                          const std::exception& exception,
                                          std::uint64_t unix_time) {
  const std::string error =
      std::string("EASIROC TCP receive drain failed: ") + exception.what();
  g_state.buffer_clear_result = "Failed: " + error;
  g_state.buffer_clear = daq::finishBufferClearRequest(
      g_state.buffer_clear, false, error, unix_time);
  cm_msg(MERROR, "manual_buffer_clear", "Request %u failed: %s",
         static_cast<unsigned>(request_id), error.c_str());
}

//************************************//
// Handle an EASIROC buffer-clear request
//************************************//
void process_manual_buffer_clear_request() {
  const auto request_id = accept_manual_buffer_clear_request();
  if (!request_id) return;

  const std::time_t now = std::time(nullptr);
  const std::uint64_t unix_time =
      now < 0 ? 0 : static_cast<std::uint64_t>(now);
  const auto settings = prepare_manual_buffer_clear(*request_id, unix_time);
  if (!settings) return;

  if (g_diagnostic.thread.joinable()) g_diagnostic.thread.join();
  publish_completed_diagnostic();

  g_state.buffer_clear =
      daq::markBufferClearExecuting(g_state.buffer_clear);
  publish_runtime_variables();
  try {
    TcpConnection connection(settings->ip_address, easiroc::kTcpDataPort,
                             kReceiveTimeoutMs);
    const std::size_t drained =
        connection.drain(kDrainQuietMs, kDrainMaximumMs);
    g_state.pending_events.clear();
    g_state.parser.reset();
    complete_manual_buffer_clear_success(*request_id, drained, unix_time);
  } catch (const std::exception& exception) {
    complete_manual_buffer_clear_failure(*request_id, exception, unix_time);
  }
  publish_runtime_variables();
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
  g_state.diagnostic_log.reset();
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

  set_firmware_readback_valid(false);
  g_state.firmware_observation = {};
  if (result->firmware) {
    const auto& firmware = *result->firmware;
    if (easiroc_odb::publish_firmware_readback_values(firmware)) {
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

  switch (g_state.diagnostic_log.observe(result->rbcp_communication_ok &&
                                         result->tcp_reachable)) {
    case easiroc::DiagnosticLogEvent::kInitialOk:
      cm_msg(MINFO, "update_status",
             "EASIROC communication check OK: RBCP firmware read and "
             "TCP port 24 probe succeeded");
      break;
    case easiroc::DiagnosticLogEvent::kRestored:
      cm_msg(MINFO, "update_status",
             "EASIROC communication restored: RBCP firmware read and "
             "TCP port 24 probe succeeded");
      break;
    case easiroc::DiagnosticLogEvent::kError:
      cm_msg(MERROR, "update_status", "Read-only status failed: %s",
             result->error.c_str());
      break;
    case easiroc::DiagnosticLogEvent::kNoLog:
      break;
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

//************************************//
// Handle an EASIROC acquisition failure
//************************************//
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

//************************************//
// Complete the EASIROC run configuration snapshot
//************************************//
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

//************************************//
// Check EASIROC configuration readiness
//************************************//
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
static INT start_abort(INT run_number, char* error);

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

//************************************//
// Initialize the EASIROC frontend
//************************************//
INT frontend_init() {
  if (!initialize_odb()) return FE_ERR_ODB;

  const INT transition_status =
      cm_register_transition(TR_STARTABORT, start_abort, 500);
  if (transition_status != CM_SUCCESS) {
    cm_msg(MERROR, "frontend_init",
           "Cannot register TR_STARTABORT callback: status %d",
           transition_status);
    return transition_status;
  }

  const INT settings_status = read_settings(&g_state.settings);
  if (settings_status != SUCCESS) return settings_status;
  if (!publish_global_busy_ready(g_state.settings.enabled, false, 0))
    return FE_ERR_ODB;
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

//************************************//
// Shut down the EASIROC frontend safely
//************************************//
INT frontend_exit() {
  publish_global_busy_ready(false, false, 0);
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

//************************************//
// Prepare EASIROC acquisition for a new run
//************************************//
INT begin_of_run(INT run_number, char* error) {
  if (error != nullptr) error[0] = '\0';
  if (!publish_global_busy_ready(false, false, 0)) return FE_ERR_ODB;
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
  if (!publish_global_busy_ready(run_settings.enabled, false, 0))
    return FE_ERR_ODB;
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
    if (!publish_global_busy_ready(true, true, run_number)) {
      if (error != nullptr)
        std::snprintf(error, 256, "Cannot publish EASIROC DAQReady");
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

//************************************//
// Stop EASIROC acquisition and publish status
//************************************//
INT end_of_run(INT run_number, char* error) {
  if (error != nullptr) error[0] = '\0';
  publish_global_busy_ready(g_state.runtime.enabled_for_run, false, 0);
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

//************************************//
// Restore EASIROC state after a failed START
//************************************//
static INT start_abort(INT run_number, char* error) {
  if (error != nullptr) error[0] = '\0';
  publish_global_busy_ready(g_state.runtime.enabled_for_run, false, 0);

  // A successful BOR enables legacy MFE readout before a peer frontend can
  // fail the common START. Quiesce software readout before sending DAQ OFF.
  readout_enable(FALSE);
  const CleanupResult cleanup =
      stop_acquisition("start_abort", false);
  g_state.run_active = false;
  g_state.run_snapshot.frontend_bor_complete = false;
  publish_run_snapshot();
  mark_configuration_failed(run_number);

  if (!cleanup.error.empty()) g_state.runtime.last_error = cleanup.error;
  g_state.runtime.acquisition_fault = !cleanup.daq_off_succeeded;
  publish_runtime_variables();

  run_state = STATE_STOPPED;
  cm_set_client_run_state(run_state);
  if (!cleanup.daq_off_succeeded) {
    if (error != nullptr)
      std::snprintf(error, 256, "%s", cleanup.error.c_str());
    return FE_ERR_HW;
  }
  cm_msg(MINFO, "start_abort",
         "STARTABORT rollback completed for run %d", run_number);
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

//************************************//
// Detect a complete EASIROC event
//************************************//
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

//************************************//
// Publish one EASIROC physics event
//************************************//
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

//************************************//
// Refresh EASIROC status without a data event
//************************************//
INT read_status_event(char*, INT) {
  publish_completed_diagnostic();
  process_manual_buffer_clear_request();
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

//************************************//
// Write the EASIROC run configuration bank
//************************************//
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
