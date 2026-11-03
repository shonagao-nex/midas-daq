#ifndef EASIROC_FRONTEND_EASIROC_ODB_H
#define EASIROC_FRONTEND_EASIROC_ODB_H

#include "easiroc_frontend_types.h"
#include "easiroc_manual_apply.h"
#include "../common/manual_buffer_clear.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace easiroc_odb {

using easiroc_frontend::EasirocRunSnapshot;
using easiroc_frontend::FrontendSettings;
using easiroc_frontend::RuntimeState;

struct RuntimePublishView {
  const RuntimeState& runtime;
  std::size_t pending_event_count;
  std::uint64_t parser_buffered_bytes;
  const easiroc::HardwareConfigurationComparison& configuration;
  bool hardware_state_indeterminate;
  const easiroc::ManualApplyStatus& asic_apply;
  const daq::BufferClearStatus& buffer_clear;
  const std::string& buffer_clear_result;
  std::uint64_t buffer_clear_drained_bytes;
};

std::string odb_path(const char* base, std::string_view name);
bool ensure_odb_value(const std::string& path, const void* default_value,
                      INT size, INT count, DWORD type);
bool read_odb_dword(const std::string& path, DWORD* value);
bool publish_global_busy_ready(bool participates, bool ready, INT run_number);
bool publish_configuration_status(bool configuration_ok, INT run_number);
bool publish_run_snapshot(const EasirocRunSnapshot& snapshot);
bool publish_runtime_variables(const RuntimePublishView& view);
bool initialize_manual_apply_mailbox_odb();
bool initialize_buffer_clear_mailbox_odb();
bool initialize_last_applied_odb();
bool initialize_acquisition_settings_odb();
bool initialize_asic_settings_odb();
bool initialize_hardware_info_odb();
bool initialize_firmware_readback_odb();
// Hold the existing local ODB semaphore from state checks through result
// publication, excluding START's update of /Runinfo/Transition in progress.
// No cm_yield/callback dispatch is allowed inside this scope.
class ApplyOdbLock {
 public:
  ApplyOdbLock();
  ~ApplyOdbLock();
  ApplyOdbLock(const ApplyOdbLock&) = delete;
  ApplyOdbLock& operator=(const ApplyOdbLock&) = delete;
  bool locked() const { return locked_; }
 private:
  bool locked_ = false;
};
using DeviceSlowControlWrite = std::function<void(
    const std::string&, std::uint32_t, const std::vector<std::uint8_t>&)>;
void process_device_apply_requests(std::uint64_t unix_time,
    const DeviceSlowControlWrite& write, const easiroc::SlowControlDelay& delay);
// Empty means START is permitted. Missing/malformed status is an error.
std::string device_start_inhibit_reason();
bool read_last_applied_settings(
    easiroc::AppliedAsicSlowControlSettings* result,
    bool* hardware_state_indeterminate);
bool publish_last_applied_settings(
    const easiroc::AppliedAsicSlowControlSettings& last_applied);
INT read_settings(FrontendSettings* settings);
void set_firmware_readback_valid(bool valid);
bool publish_firmware_readback_values(const easiroc::FirmwareVersion& firmware);

}  // namespace easiroc_odb

#endif
