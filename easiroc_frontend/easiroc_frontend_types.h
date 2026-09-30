#ifndef EASIROC_FRONTEND_TYPES_H
#define EASIROC_FRONTEND_TYPES_H

#include "midas.h"
#include "easiroc_daq_control.h"
#include "easiroc_last_applied.h"
#include "easiroc_run_settings.h"
#include "easiroc_status.h"

#include <cstdint>
#include <string>

namespace easiroc_frontend {

struct FrontendSettings {
  bool enabled = true;
  std::string ip_address = "192.168.10.26";
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
  DWORD schema_version = easiroc::kEasirocRunSnapshotSchemaVersion;
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

}  // namespace easiroc_frontend

#endif
