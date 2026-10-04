#include "easiroc_odb.h"
#include "mfe.h"
#include "easiroc_readout.h"
#include "rbcp.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <vector>

namespace easiroc_odb {
namespace {
constexpr char kSettingsPath[] = "/Equipment/EASIROC/Settings";
constexpr char kCommandsPath[] = "/Equipment/EASIROC/Commands";
constexpr char kInfoPath[] = "/Equipment/EASIROC/Info";
constexpr char kReadbackPath[] = "/Equipment/EASIROC/Readback";
constexpr char kVariablesPath[] = "/Equipment/EASIROC/Variables";
constexpr char kRunSnapshotPath[] = "/Equipment/EASIROC/RunSnapshot";
constexpr char kDefaultIpAddress[] = "192.168.10.26";
constexpr char kHardwareModel[] = "NIM-EASIROC";
constexpr DWORD kAsicCount = 2;
constexpr DWORD kChannelsPerAsic = 32;
static_assert(kAsicCount * kChannelsPerAsic == easiroc::kAdcChannelCount,
              "EASIROC hardware channel information is inconsistent");
}  // namespace

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

//************************************//
// Publish EASIROC participation and DAQ readiness
//************************************//
bool publish_global_busy_ready(bool participates, bool ready, INT run_number) {
  const BOOL p = participates ? TRUE : FALSE;
  const BOOL r = ready ? TRUE : FALSE;
  return set_odb_value("/Equipment/EASIROC/Status/ParticipatesInGlobalBusy",
                       &p, sizeof(p), 1, TID_BOOL) &&
         set_odb_value("/Equipment/EASIROC/Status/DAQReady",
                       &r, sizeof(r), 1, TID_BOOL) &&
         set_odb_value("/Equipment/EASIROC/Status/ReadyRunNumber",
                       &run_number, sizeof(run_number), 1, TID_INT);
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

//************************************//
// Publish run identity and participation snapshot fields
//************************************//
void publish_snapshot_metadata(const EasirocRunSnapshot& snapshot, bool& ok) {
  const std::string metadata = odb_path(kRunSnapshotPath, "Metadata");
  const BOOL enabled_for_run = snapshot.enabled_for_run ? TRUE : FALSE;
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
}

//************************************//
// Publish requested acquisition and network settings
//************************************//
void publish_snapshot_requested_acquisition(const EasirocRunSnapshot& snapshot,
    bool& ok) {
  const std::string requested = odb_path(kRunSnapshotPath, "Requested");
  const BOOL requested_enabled = snapshot.requested.enabled ? TRUE : FALSE;
  const BOOL adc = snapshot.requested.enables.adc ? TRUE : FALSE;
  const BOOL tdc = snapshot.requested.enables.tdc ? TRUE : FALSE;
  const BOOL scaler = snapshot.requested.enables.scaler ? TRUE : FALSE;
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
}

//************************************//
// Publish requested ASIC slow control settings
//************************************//
void publish_snapshot_requested_asic(const EasirocRunSnapshot& snapshot,
    bool& ok) {
  const std::string requested = odb_path(kRunSnapshotPath, "Requested");
  const BOOL apply_at_bor =
      snapshot.requested.asic_slow_control.apply_at_bor ? TRUE : FALSE;
  const INT asic1_dac_code =
      snapshot.requested.asic_slow_control.asic[0].dac_code;
  const INT asic1_dac_slope =
      snapshot.requested.asic_slow_control.asic[0].dac_slope;
  const INT asic1_hg_feedback =
      snapshot.requested.asic_slow_control.asic[0].hg_feedback_capacitance;
  const INT asic1_lg_feedback =
      snapshot.requested.asic_slow_control.asic[0].lg_feedback_capacitance;
  const INT asic1_hg_shaping =
      snapshot.requested.asic_slow_control.asic[0].hg_shaping_time;
  const INT asic1_lg_shaping =
      snapshot.requested.asic_slow_control.asic[0].lg_shaping_time;
  const INT asic2_dac_code =
      snapshot.requested.asic_slow_control.asic[1].dac_code;
  const INT asic2_dac_slope =
      snapshot.requested.asic_slow_control.asic[1].dac_slope;
  const INT asic2_hg_feedback =
      snapshot.requested.asic_slow_control.asic[1].hg_feedback_capacitance;
  const INT asic2_lg_feedback =
      snapshot.requested.asic_slow_control.asic[1].lg_feedback_capacitance;
  const INT asic2_hg_shaping =
      snapshot.requested.asic_slow_control.asic[1].hg_shaping_time;
  const INT asic2_lg_shaping =
      snapshot.requested.asic_slow_control.asic[1].lg_shaping_time;
  const auto& asic1_input_dac =
      snapshot.requested.asic_slow_control.asic[0].input_dac;
  const auto& asic2_input_dac =
      snapshot.requested.asic_slow_control.asic[1].input_dac;
  std::array<BOOL, easiroc::kInputDacChannelCount> asic1_channel_enabled{};
  std::array<BOOL, easiroc::kInputDacChannelCount> asic2_channel_enabled{};
  for (std::size_t channel = 0; channel < asic1_channel_enabled.size();
       ++channel) {
    asic1_channel_enabled[channel] =
        snapshot.requested.asic_slow_control.asic[0].channel_enabled[channel]
            ? TRUE
            : FALSE;
    asic2_channel_enabled[channel] =
        snapshot.requested.asic_slow_control.asic[1].channel_enabled[channel]
            ? TRUE
            : FALSE;
  }
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
           &asic1_hg_feedback, sizeof(asic1_hg_feedback), 1, TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[4]),
           &asic1_lg_feedback, sizeof(asic1_lg_feedback), 1, TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[5]),
           &asic1_hg_shaping, sizeof(asic1_hg_shaping), 1, TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[6]),
           &asic1_lg_shaping, sizeof(asic1_lg_shaping), 1, TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[7]),
           asic1_input_dac.data(), sizeof(asic1_input_dac),
           asic1_input_dac.size(), TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[8]),
           asic1_channel_enabled.data(), sizeof(asic1_channel_enabled),
           asic1_channel_enabled.size(), TID_BOOL) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[9]),
           &asic2_dac_code, sizeof(asic2_dac_code), 1, TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[10]),
           &asic2_dac_slope, sizeof(asic2_dac_slope), 1, TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[11]),
           &asic2_hg_feedback, sizeof(asic2_hg_feedback), 1, TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[12]),
           &asic2_lg_feedback, sizeof(asic2_lg_feedback), 1, TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[13]),
           &asic2_hg_shaping, sizeof(asic2_hg_shaping), 1, TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[14]),
           &asic2_lg_shaping, sizeof(asic2_lg_shaping), 1, TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[15]),
           asic2_input_dac.data(), sizeof(asic2_input_dac),
           asic2_input_dac.size(), TID_INT) && ok;
  ok = set_odb_value(
           odb_path(requested.c_str(),
                    easiroc::kAsicSlowControlRequestedSnapshotPaths[16]),
           asic2_channel_enabled.data(), sizeof(asic2_channel_enabled),
           asic2_channel_enabled.size(), TID_BOOL) && ok;

}

//************************************//
// Publish BOR apply result
//************************************//
void publish_snapshot_apply(const EasirocRunSnapshot& snapshot, bool& ok) {
  const std::string apply = odb_path(kRunSnapshotPath, "Apply");
  const BOOL apply_attempted = snapshot.apply.attempted ? TRUE : FALSE;
  const BOOL apply_succeeded =
      snapshot.apply.sequence_succeeded ? TRUE : FALSE;
  ok = set_odb_value(odb_path(apply.c_str(), "Attempted"),
                     &apply_attempted, sizeof(apply_attempted), 1,
                     TID_BOOL) && ok;
  ok = set_odb_value(odb_path(apply.c_str(), "SequenceSucceeded"),
                     &apply_succeeded, sizeof(apply_succeeded), 1,
                     TID_BOOL) && ok;
  ok = set_odb_string(odb_path(apply.c_str(), "Error"),
                      snapshot.apply.error, 256) && ok;
}

//************************************//
// Publish configuration consistency result
//************************************//
void publish_snapshot_consistency(const EasirocRunSnapshot& snapshot,
    bool& ok) {
  const std::string consistency = odb_path(kRunSnapshotPath, "Consistency");
  const BOOL last_applied_valid =
      snapshot.consistency.last_applied_valid ? TRUE : FALSE;
  const BOOL configuration_match =
      snapshot.consistency.configuration_match ? TRUE : FALSE;
  const BOOL hardware_state_indeterminate =
      snapshot.consistency.hardware_state_indeterminate ? TRUE : FALSE;
  const DWORD last_applied_request_id =
      snapshot.consistency.last_applied_request_id;
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
}

//************************************//
// Publish firmware readback observation
//************************************//
void publish_snapshot_firmware(const EasirocRunSnapshot& snapshot, bool& ok) {
  const std::string firmware = odb_path(kRunSnapshotPath, "Readback/Firmware");
  const BOOL firmware_valid = snapshot.firmware.valid ? TRUE : FALSE;
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
}

//************************************//
// Publish the EASIROC run snapshot and completion marker
//************************************//
bool publish_run_snapshot(const EasirocRunSnapshot& snapshot) {
  bool ok = true;
  publish_snapshot_metadata(snapshot, ok);
  publish_snapshot_requested_acquisition(snapshot, ok);
  publish_snapshot_requested_asic(snapshot, ok);
  publish_snapshot_apply(snapshot, ok);
  publish_snapshot_consistency(snapshot, ok);
  publish_snapshot_firmware(snapshot, ok);
  const std::string metadata = odb_path(kRunSnapshotPath, "Metadata");
  /* Publish the completion marker last. If any preceding write failed,
   * leave the fixed subtree explicitly incomplete. */
  const BOOL published_complete =
      (snapshot.frontend_bor_complete && ok) ? TRUE : FALSE;
  const bool complete_ok = set_odb_value(
      odb_path(metadata.c_str(), "FrontendBORComplete"), &published_complete,
      sizeof(published_complete), 1, TID_BOOL);
  ok = complete_ok && ok;
  return ok;}

//************************************//
// Publish EASIROC runtime status in ODB
//************************************//
bool publish_runtime_variables(const RuntimePublishView& view) {
  const BOOL enabled_for_run =
      view.runtime.enabled_for_run ? TRUE : FALSE;
  const BOOL rbcp_ok = view.runtime.rbcp_communication_ok ? TRUE : FALSE;
  const BOOL tcp_reachable = view.runtime.tcp_reachable ? TRUE : FALSE;
  const BOOL tcp_connected = view.runtime.tcp_connected ? TRUE : FALSE;
  const BOOL running = view.runtime.acquisition_running ? TRUE : FALSE;
  const BOOL fault = view.runtime.acquisition_fault ? TRUE : FALSE;
  const std::uint64_t pending = view.pending_event_count;
  const std::uint64_t buffered =
      view.parser_buffered_bytes;
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
                      view.runtime.last_error, 256) && ok;
  PUBLISH_QWORD(kVariablesPath, "EventCounter", view.runtime.event_counter);
  const std::string readout = odb_path(kVariablesPath, "Readout");
  PUBLISH_QWORD(readout.c_str(), "PendingEventCount", pending);
  PUBLISH_QWORD(readout.c_str(), "ParserBufferedBytes", buffered);
  const std::string statistics = odb_path(kVariablesPath, "Statistics");
  PUBLISH_QWORD(statistics.c_str(), "ReceivedBytes",
                view.runtime.statistics.received_bytes);
  PUBLISH_QWORD(statistics.c_str(), "ReceiveChunks",
                view.runtime.statistics.receive_chunks);
  PUBLISH_QWORD(statistics.c_str(), "TCPErrorCount",
                view.runtime.statistics.tcp_error_count);
  PUBLISH_QWORD(statistics.c_str(), "ReceiveTimeoutCount",
                view.runtime.statistics.receive_timeout_count);
  PUBLISH_QWORD(statistics.c_str(), "DecodeErrorCount",
                view.runtime.statistics.decode_error_count);
  PUBLISH_QWORD(statistics.c_str(), "EventContentErrorCount",
                view.runtime.statistics.event_content_error_count);
  PUBLISH_QWORD(statistics.c_str(), "ADCOverflowCount",
                view.runtime.statistics.adc_overflow_count);
  PUBLISH_QWORD(statistics.c_str(), "LastDrainBytes",
                view.runtime.statistics.last_drain_bytes);
  PUBLISH_QWORD(statistics.c_str(), "TotalDrainBytes",
                view.runtime.statistics.total_drain_bytes);
  const std::string asic_slow_control =
      odb_path(kVariablesPath, "ASICSlowControl");
  const auto& configuration = view.configuration;
  const BOOL configuration_match =
      configuration.status == easiroc::HardwareConfigurationStatus::kMatch
          ? TRUE
          : FALSE;
  const BOOL hardware_state_indeterminate =
      view.hardware_state_indeterminate ? TRUE : FALSE;
  const DWORD active_request_id = view.asic_apply.active_request_id;
  const DWORD last_handled_request_id =
      view.asic_apply.last_handled_request_id;
  const DWORD last_successful_request_id =
      view.asic_apply.last_successful_request_id;
  const BOOL apply_in_progress =
      view.asic_apply.apply_in_progress ? TRUE : FALSE;
  const BOOL last_attempt_succeeded =
      view.asic_apply.last_attempt_succeeded ? TRUE : FALSE;
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
           easiroc::manualApplyStateName(view.asic_apply.state), 32) && ok;
  ok = set_odb_value(
           odb_path(asic_slow_control.c_str(), "ApplyInProgress"),
           &apply_in_progress, sizeof(apply_in_progress), 1, TID_BOOL) && ok;
  ok = set_odb_value(
           odb_path(asic_slow_control.c_str(), "LastAttemptSucceeded"),
           &last_attempt_succeeded, sizeof(last_attempt_succeeded), 1,
           TID_BOOL) && ok;
  ok = set_odb_string(
           odb_path(asic_slow_control.c_str(), "LastApplyError"),
           view.asic_apply.last_apply_error, 256) && ok;
  PUBLISH_QWORD(asic_slow_control.c_str(), "LastApplyUnixTime",
                view.asic_apply.last_apply_unix_time);
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
  const std::string buffer_clear = odb_path(kVariablesPath, "BufferClear");
  const DWORD clear_active = view.buffer_clear.active_request_id;
  const DWORD clear_handled = view.buffer_clear.last_handled_request_id;
  const DWORD clear_successful =
      view.buffer_clear.last_successful_request_id;
  const BOOL clear_in_progress =
      view.buffer_clear.in_progress ? TRUE : FALSE;
  const BOOL clear_succeeded =
      view.buffer_clear.last_attempt_succeeded ? TRUE : FALSE;
  ok = set_odb_value(odb_path(buffer_clear.c_str(), "ActiveRequestId"),
                     &clear_active, sizeof(clear_active), 1, TID_DWORD) && ok;
  ok = set_odb_value(odb_path(buffer_clear.c_str(), "LastHandledRequestId"),
                     &clear_handled, sizeof(clear_handled), 1, TID_DWORD) && ok;
  ok = set_odb_value(
           odb_path(buffer_clear.c_str(), "LastSuccessfulRequestId"),
           &clear_successful, sizeof(clear_successful), 1, TID_DWORD) && ok;
  ok = set_odb_string(odb_path(buffer_clear.c_str(), "State"),
                      daq::bufferClearStateName(view.buffer_clear.state),
                      32) && ok;
  ok = set_odb_value(odb_path(buffer_clear.c_str(), "InProgress"),
                     &clear_in_progress, sizeof(clear_in_progress), 1,
                     TID_BOOL) && ok;
  ok = set_odb_value(
           odb_path(buffer_clear.c_str(), "LastAttemptSucceeded"),
           &clear_succeeded, sizeof(clear_succeeded), 1, TID_BOOL) && ok;
  ok = set_odb_string(odb_path(buffer_clear.c_str(), "LastError"),
                      view.buffer_clear.last_error, 256) && ok;
  ok = set_odb_value(odb_path(buffer_clear.c_str(), "LastClearUnixTime"),
                     &view.buffer_clear.last_clear_unix_time,
                     sizeof(view.buffer_clear.last_clear_unix_time), 1,
                     TID_QWORD) && ok;
  ok = set_odb_string(odb_path(buffer_clear.c_str(), "NIMEASIROCResult"),
                      view.buffer_clear_result, 256) && ok;
  ok = set_odb_value(odb_path(buffer_clear.c_str(), "DrainedBytes"),
                     &view.buffer_clear_drained_bytes,
                     sizeof(view.buffer_clear_drained_bytes), 1,
                     TID_QWORD) && ok;
#undef PUBLISH_QWORD
#undef PUBLISH_BOOL
  return ok;
}

bool initialize_last_applied_odb() {
  const std::string status = odb_path(kVariablesPath, "ASICSlowControl");
  const std::string last_applied = odb_path(status.c_str(), "LastApplied");
  const BOOL no = FALSE;
  const DWORD zero_request = 0;
  const std::uint64_t zero_time = 0;
  const INT zero_value = 0;
  const std::array<INT, easiroc::kInputDacChannelCount> zero_input_dac{};
  const std::array<BOOL, easiroc::kInputDacChannelCount> zero_channel_enabled{};
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
         ensure_odb_value(
             odb_path(last_applied.c_str(), "ASIC1/HGFeedbackCapacitance"),
             &zero_value, sizeof(zero_value), 1, TID_INT) &&
         ensure_odb_value(
             odb_path(last_applied.c_str(), "ASIC1/LGFeedbackCapacitance"),
             &zero_value, sizeof(zero_value), 1, TID_INT) &&
         ensure_odb_value(
             odb_path(last_applied.c_str(), "ASIC1/HGShapingTime"),
             &zero_value, sizeof(zero_value), 1, TID_INT) &&
         ensure_odb_value(
             odb_path(last_applied.c_str(), "ASIC1/LGShapingTime"),
             &zero_value, sizeof(zero_value), 1, TID_INT) &&
         ensure_odb_value(odb_path(last_applied.c_str(), "ASIC1/InputDAC"),
                          zero_input_dac.data(), sizeof(zero_input_dac),
                          zero_input_dac.size(), TID_INT) &&
         ensure_odb_value(odb_path(last_applied.c_str(), "ASIC1/ChannelEnabled"),
                          zero_channel_enabled.data(),
                          sizeof(zero_channel_enabled),
                          zero_channel_enabled.size(), TID_BOOL) &&
         ensure_odb_value(
             odb_path(last_applied.c_str(), "ASIC2/DiscriminatorDACCode"),
             &zero_value, sizeof(zero_value), 1, TID_INT) &&
         ensure_odb_value(
             odb_path(last_applied.c_str(), "ASIC2/DiscriminatorDACSlope"),
             &zero_value, sizeof(zero_value), 1, TID_INT) &&
         ensure_odb_value(
             odb_path(last_applied.c_str(), "ASIC2/HGFeedbackCapacitance"),
             &zero_value, sizeof(zero_value), 1, TID_INT) &&
         ensure_odb_value(
             odb_path(last_applied.c_str(), "ASIC2/LGFeedbackCapacitance"),
             &zero_value, sizeof(zero_value), 1, TID_INT) &&
         ensure_odb_value(
             odb_path(last_applied.c_str(), "ASIC2/HGShapingTime"),
             &zero_value, sizeof(zero_value), 1, TID_INT) &&
         ensure_odb_value(
             odb_path(last_applied.c_str(), "ASIC2/LGShapingTime"),
             &zero_value, sizeof(zero_value), 1, TID_INT) &&
         ensure_odb_value(odb_path(last_applied.c_str(), "ASIC2/InputDAC"),
                          zero_input_dac.data(), sizeof(zero_input_dac),
                          zero_input_dac.size(), TID_INT) &&
         ensure_odb_value(odb_path(last_applied.c_str(), "ASIC2/ChannelEnabled"),
                          zero_channel_enabled.data(),
                          sizeof(zero_channel_enabled),
                          zero_channel_enabled.size(), TID_BOOL) &&
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

//************************************//
// Initialize enabled, network, and acquisition ODB settings
//************************************//
bool initialize_acquisition_settings_odb() {
  char default_ip[64] = {};
  std::snprintf(default_ip, sizeof(default_ip), "%s", kDefaultIpAddress);
  const BOOL yes = TRUE;
  const BOOL no = FALSE;
  if (!ensure_odb_value(odb_path(kSettingsPath, "Enabled"),
                        &yes, sizeof(yes), 1, TID_BOOL) ||
      !ensure_odb_value(odb_path(kSettingsPath, "FrontendEnabled"),
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
  return true;
}

//************************************//
// Initialize requested ASIC slow control ODB settings
//************************************//
bool initialize_asic_settings_odb() {
  const BOOL no = FALSE;
  const INT default_dac_code = easiroc::kDefaultDiscriminatorDacCode;
  const INT default_dac_slope = easiroc::kDefaultDiscriminatorDacSlope;
  const INT default_feedback = easiroc::kDefaultFeedbackCapacitanceFemtofarads;
  const INT default_hg_shaping = easiroc::kDefaultHighGainShapingTimeNanoseconds;
  const INT default_lg_shaping = easiroc::kDefaultLowGainShapingTimeNanoseconds;
  const auto default_input_dac = easiroc::defaultInputDacValues();
  std::array<BOOL, easiroc::kInputDacChannelCount> default_channel_enabled{};
  default_channel_enabled.fill(TRUE);
  if (!ensure_odb_value(
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
          &default_feedback, sizeof(default_feedback), 1, TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[4]),
          &default_feedback, sizeof(default_feedback), 1, TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[5]),
          &default_hg_shaping, sizeof(default_hg_shaping), 1, TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[6]),
          &default_lg_shaping, sizeof(default_lg_shaping), 1, TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[7]),
          default_input_dac.data(), sizeof(default_input_dac),
          default_input_dac.size(), TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[8]),
          default_channel_enabled.data(), sizeof(default_channel_enabled),
          default_channel_enabled.size(), TID_BOOL) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[9]),
          &default_dac_code, sizeof(default_dac_code), 1, TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[10]),
          &default_dac_slope, sizeof(default_dac_slope), 1, TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[11]),
          &default_feedback, sizeof(default_feedback), 1, TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[12]),
          &default_feedback, sizeof(default_feedback), 1, TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[13]),
          &default_hg_shaping, sizeof(default_hg_shaping), 1, TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[14]),
          &default_lg_shaping, sizeof(default_lg_shaping), 1, TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[15]),
          default_input_dac.data(), sizeof(default_input_dac),
          default_input_dac.size(), TID_INT) ||
      !ensure_odb_value(
          odb_path(kSettingsPath,
                   easiroc::kAsicSlowControlRequestedSnapshotPaths[16]),
          default_channel_enabled.data(), sizeof(default_channel_enabled),
          default_channel_enabled.size(), TID_BOOL))
    return false;

  return true;
}

//************************************//
// Publish fixed hardware and network information in ODB
//************************************//
bool initialize_hardware_info_odb() {
  const BOOL yes = TRUE;
  const BOOL no = FALSE;
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

  return true;
}

//************************************//
// Initialize empty firmware readback fields in ODB
//************************************//
bool initialize_firmware_readback_odb() {
  const BOOL no = FALSE;
  const std::array<std::uint8_t, easiroc::kFirmwareVersionLength> empty_raw{};
  if (!set_odb_value(odb_path(kReadbackPath, "Firmware/Valid"), &no,
                     sizeof(no), 1, TID_BOOL) ||
      !set_odb_string(odb_path(kReadbackPath, "Firmware/Version"), "", 64) ||
      !set_odb_string(odb_path(kReadbackPath, "Firmware/SynthesisDate"), "",
                      64) ||
      !set_odb_value(odb_path(kReadbackPath, "Firmware/Raw"), empty_raw.data(),
                     empty_raw.size(), empty_raw.size(), TID_BYTE))
    return false;
  return true;
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

INT read_channel_enabled_setting(
    const char* name,
    std::array<bool, easiroc::kInputDacChannelCount>* values) {
  std::array<BOOL, easiroc::kInputDacChannelCount> odb_values{};
  INT size = sizeof(odb_values);
  const INT status =
      db_get_value(hDB, 0, name, odb_values.data(), &size, TID_BOOL, FALSE);
  if (status != DB_SUCCESS || size != static_cast<INT>(sizeof(odb_values))) {
    cm_msg(MERROR, "read_settings",
           "Cannot read 32-element BOOL array %s (status %d, size %d)", name,
           status, size);
    return FE_ERR_ODB;
  }
  for (std::size_t channel = 0; channel < values->size(); ++channel)
    (*values)[channel] = odb_values[channel] != FALSE;
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
    path = odb_path(last_applied_path.c_str(), prefix + "HGFeedbackCapacitance");
    if (read_int_setting(path.c_str(),
                         &next.asic[asic].hg_feedback_capacitance) != SUCCESS)
      return false;
    path = odb_path(last_applied_path.c_str(), prefix + "LGFeedbackCapacitance");
    if (read_int_setting(path.c_str(),
                         &next.asic[asic].lg_feedback_capacitance) != SUCCESS)
      return false;
    path = odb_path(last_applied_path.c_str(), prefix + "HGShapingTime");
    if (read_int_setting(path.c_str(), &next.asic[asic].hg_shaping_time) !=
        SUCCESS)
      return false;
    path = odb_path(last_applied_path.c_str(), prefix + "LGShapingTime");
    if (read_int_setting(path.c_str(), &next.asic[asic].lg_shaping_time) !=
        SUCCESS)
      return false;
    path = odb_path(last_applied_path.c_str(), prefix + "InputDAC");
    if (read_input_dac_setting(path.c_str(), &next.asic[asic].input_dac) !=
        SUCCESS)
      return false;
    path = odb_path(last_applied_path.c_str(), prefix + "ChannelEnabled");
    if (read_channel_enabled_setting(path.c_str(),
                                     &next.asic[asic].channel_enabled) !=
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
    const INT hg_feedback = last_applied.asic[asic].hg_feedback_capacitance;
    const INT lg_feedback = last_applied.asic[asic].lg_feedback_capacitance;
    const INT hg_shaping = last_applied.asic[asic].hg_shaping_time;
    const INT lg_shaping = last_applied.asic[asic].lg_shaping_time;
    std::array<BOOL, easiroc::kInputDacChannelCount> channel_enabled{};
    for (std::size_t channel = 0; channel < channel_enabled.size(); ++channel)
      channel_enabled[channel] = last_applied.asic[asic].channel_enabled[channel]
                                     ? TRUE
                                     : FALSE;
    ok = set_odb_value(
             odb_path(base.c_str(), prefix + "DiscriminatorDACCode"),
             &dac_code, sizeof(dac_code), 1, TID_INT) && ok;
    ok = set_odb_value(
             odb_path(base.c_str(), prefix + "DiscriminatorDACSlope"),
             &dac_slope, sizeof(dac_slope), 1, TID_INT) && ok;
    ok = set_odb_value(odb_path(base.c_str(), prefix + "HGFeedbackCapacitance"),
                       &hg_feedback, sizeof(hg_feedback), 1, TID_INT) && ok;
    ok = set_odb_value(odb_path(base.c_str(), prefix + "LGFeedbackCapacitance"),
                       &lg_feedback, sizeof(lg_feedback), 1, TID_INT) && ok;
    ok = set_odb_value(odb_path(base.c_str(), prefix + "HGShapingTime"),
                       &hg_shaping, sizeof(hg_shaping), 1, TID_INT) && ok;
    ok = set_odb_value(odb_path(base.c_str(), prefix + "LGShapingTime"),
                       &lg_shaping, sizeof(lg_shaping), 1, TID_INT) && ok;
    ok = set_odb_value(odb_path(base.c_str(), prefix + "InputDAC"),
                       last_applied.asic[asic].input_dac.data(),
                       sizeof(last_applied.asic[asic].input_dac),
                       last_applied.asic[asic].input_dac.size(), TID_INT) && ok;
    ok = set_odb_value(odb_path(base.c_str(), prefix + "ChannelEnabled"),
                       channel_enabled.data(), sizeof(channel_enabled),
                       channel_enabled.size(), TID_BOOL) && ok;
  }
  if (ok)
    ok = set_odb_value(odb_path(base.c_str(), "Valid"), &valid,
                       sizeof(valid), 1, TID_BOOL);
  return ok;
}

//************************************//
// Read and validate EASIROC run settings
//************************************//
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
    const std::size_t first_path = 1 + asic * 8;
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
        easiroc::kAsicSlowControlRequestedSnapshotPaths[first_path + 2]);
    status = read_int_setting(
        path.c_str(), &next.asic_slow_control.asic[asic].hg_feedback_capacitance);
    if (status != SUCCESS) return status;
    path = odb_path(
        kSettingsPath,
        easiroc::kAsicSlowControlRequestedSnapshotPaths[first_path + 3]);
    status = read_int_setting(
        path.c_str(), &next.asic_slow_control.asic[asic].lg_feedback_capacitance);
    if (status != SUCCESS) return status;

    path = odb_path(
        kSettingsPath,
        easiroc::kAsicSlowControlRequestedSnapshotPaths[first_path + 4]);
    status = read_int_setting(
        path.c_str(), &next.asic_slow_control.asic[asic].hg_shaping_time);
    if (status != SUCCESS) return status;
    path = odb_path(
        kSettingsPath,
        easiroc::kAsicSlowControlRequestedSnapshotPaths[first_path + 5]);
    status = read_int_setting(
        path.c_str(), &next.asic_slow_control.asic[asic].lg_shaping_time);
    if (status != SUCCESS) return status;

    path = odb_path(
        kSettingsPath,
        easiroc::kAsicSlowControlRequestedSnapshotPaths[first_path + 6]);
    status = read_input_dac_setting(
        path.c_str(), &next.asic_slow_control.asic[asic].input_dac);
    if (status != SUCCESS) return status;
    path = odb_path(
        kSettingsPath,
        easiroc::kAsicSlowControlRequestedSnapshotPaths[first_path + 7]);
    status = read_channel_enabled_setting(
        path.c_str(), &next.asic_slow_control.asic[asic].channel_enabled);
    if (status != SUCCESS) return status;
  }

  *settings = next;
  return SUCCESS;
}

//************************************//
// Publish the firmware readback validity marker
//************************************//
void set_firmware_readback_valid(bool valid) {
  const BOOL value = valid ? TRUE : FALSE;
  set_odb_value(odb_path(kReadbackPath, "Firmware/Valid"), &value,
                sizeof(value), 1, TID_BOOL);
}

//************************************//
// Publish firmware values before marking their readback valid
//************************************//
bool publish_firmware_readback_values(const easiroc::FirmwareVersion& firmware) {
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
  if (readback_ok) set_firmware_readback_valid(true);
  return readback_ok;
}

//************************************//
// Initialize manual ASIC apply mailbox schema
//************************************//
bool initialize_manual_apply_mailbox_odb() {
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

  return true;
}

//************************************//
// Initialize manual buffer clear mailbox schema
//************************************//
bool initialize_buffer_clear_mailbox_odb() {
  const DWORD zero = 0;
  const BOOL no = FALSE;
  const std::uint64_t zero_time = 0;
  std::array<char, 32> idle{};
  std::snprintf(idle.data(), idle.size(), "%s", "Idle");
  std::array<char, 256> empty{};
  const std::string command =
      odb_path(kCommandsPath, "BufferClearRequestId");
  const std::string status = odb_path(kVariablesPath, "BufferClear");
  if (!ensure_odb_value(command, &zero, sizeof(zero), 1, TID_DWORD) ||
      !ensure_odb_value(odb_path(status.c_str(), "ActiveRequestId"), &zero,
                        sizeof(zero), 1, TID_DWORD) ||
      !ensure_odb_value(odb_path(status.c_str(), "LastHandledRequestId"),
                        &zero, sizeof(zero), 1, TID_DWORD) ||
      !ensure_odb_value(odb_path(status.c_str(), "LastSuccessfulRequestId"),
                        &zero, sizeof(zero), 1, TID_DWORD) ||
      !ensure_odb_value(odb_path(status.c_str(), "State"), idle.data(),
                        idle.size(), 1, TID_STRING) ||
      !ensure_odb_value(odb_path(status.c_str(), "InProgress"), &no,
                        sizeof(no), 1, TID_BOOL) ||
      !ensure_odb_value(odb_path(status.c_str(), "LastAttemptSucceeded"), &no,
                        sizeof(no), 1, TID_BOOL) ||
      !ensure_odb_value(odb_path(status.c_str(), "LastError"), empty.data(),
                        empty.size(), 1, TID_STRING) ||
      !ensure_odb_value(odb_path(status.c_str(), "LastClearUnixTime"),
                        &zero_time, sizeof(zero_time), 1, TID_QWORD) ||
      !ensure_odb_value(odb_path(status.c_str(), "NIMEASIROCResult"),
                        empty.data(), empty.size(), 1, TID_STRING) ||
      !ensure_odb_value(odb_path(status.c_str(), "DrainedBytes"), &zero_time,
                        sizeof(zero_time), 1, TID_QWORD))
    return false;

  return true;
}

}  // namespace easiroc_odb
