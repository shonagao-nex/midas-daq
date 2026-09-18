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
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr char kSettingsPath[] = "/Equipment/NIM-EASIROC/Settings";
constexpr char kVariablesPath[] = "/Equipment/NIM-EASIROC/Variables";
constexpr std::uint16_t kTcpPort = 24;
constexpr int kConnectTimeoutMs = 1000;
constexpr int kReceiveTimeoutMs = 100;
constexpr int kDrainQuietMs = 100;
constexpr int kDrainMaximumMs = 1000;
constexpr std::size_t kReceiveChunkBytes = 4096;
constexpr bool kWriteLowGainBank = false;

constexpr char kSettingsRecord[] =
    "[.]\n"
    "IPAddress = STRING : [64] 192.168.10.26\n"
    "ADCEnabled = BOOL : y\n"
    "TDCEnabled = BOOL : y\n"
    "ScalerEnabled = BOOL : n\n";

constexpr char kVariablesRecord[] =
    "[.]\n"
    "FirmwareVersion = STRING : [64] Not read\n"
    "Connected = BOOL : n\n"
    "LastError = STRING : [256] \n";

struct FrontendSettings {
  std::string ip_address = "192.168.10.26";
  easiroc::DaqEnables enables;
};

// This state is passive: construction and destruction perform no hardware
// access. All communication is explicit in BOR, polling, and cleanup paths.
struct FrontendState {
  FrontendSettings settings;
  easiroc::DaqControl daq_control;

  std::unique_ptr<RbcpClient> rbcp;
  std::unique_ptr<TcpConnection> tcp;
  std::unique_ptr<easiroc::EventStreamParser> parser;
  std::deque<easiroc::DecodedEvent> pending_events;
  bool daq_start_attempted = false;
  bool acquisition_active = false;
  bool acquisition_fault = false;

  // Connection points for the existing Slow Control policy. They are not
  // encoded or applied by this frontend.
  easiroc::EasirocSlowControlConfig slow_control_1;
  easiroc::EasirocSlowControlConfig slow_control_2;
};

FrontendState g_state;

struct DiagnosticResult {
  bool connected = false;
  std::optional<std::string> firmware_version;
  std::string error;
};

struct DiagnosticWorker {
  std::thread thread;
  std::atomic<bool> running{false};
  std::mutex result_mutex;
  std::optional<DiagnosticResult> result;
};

DiagnosticWorker g_diagnostic;

INT create_odb_records() {
  INT status = db_create_record(hDB, 0, kSettingsPath, kSettingsRecord);
  if (status != DB_SUCCESS) {
    cm_msg(MERROR, "frontend_init", "Cannot create/check %s (status %d)",
           kSettingsPath, status);
    return FE_ERR_ODB;
  }

  status = db_create_record(hDB, 0, kVariablesPath, kVariablesRecord);
  if (status != DB_SUCCESS) {
    cm_msg(MERROR, "frontend_init", "Cannot create/check %s (status %d)",
           kVariablesPath, status);
    return FE_ERR_ODB;
  }
  return SUCCESS;
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
  std::string path = std::string(kSettingsPath) + "/IPAddress";
  INT status = db_get_value(hDB, 0, path.c_str(), ip_address, &size,
                            TID_STRING, FALSE);
  if (status != DB_SUCCESS) {
    cm_msg(MERROR, "read_settings", "Cannot read %s (status %d)",
           path.c_str(), status);
    return FE_ERR_ODB;
  }

  FrontendSettings next;
  next.ip_address = ip_address;

  path = std::string(kSettingsPath) + "/ADCEnabled";
  status = read_bool_setting(path.c_str(), &next.enables.adc);
  if (status != SUCCESS) return status;

  path = std::string(kSettingsPath) + "/TDCEnabled";
  status = read_bool_setting(path.c_str(), &next.enables.tdc);
  if (status != SUCCESS) return status;

  path = std::string(kSettingsPath) + "/ScalerEnabled";
  status = read_bool_setting(path.c_str(), &next.enables.scaler);
  if (status != SUCCESS) return status;

  *settings = next;
  return SUCCESS;
}

INT set_odb_string(const char* path, const std::string& value,
                   std::size_t capacity) {
  std::string bounded = value.substr(0, capacity - 1);
  std::vector<char> buffer(capacity, '\0');
  std::copy(bounded.begin(), bounded.end(), buffer.begin());
  const INT status = db_set_value(hDB, 0, path, buffer.data(), buffer.size(), 1,
                                  TID_STRING);
  if (status != DB_SUCCESS)
    cm_msg(MERROR, "update_status", "Cannot write %s to ODB (status %d)",
           path, status);
  return status;
}

void run_diagnostic(std::string host) {
  DiagnosticResult result;
  bool firmware_ok = false;
  bool tcp_ok = false;

  try {
    const auto firmware = easiroc::readFirmwareVersion(host);
    result.firmware_version = firmware.versionString();
    firmware_ok = true;
  } catch (const std::exception& error) {
    result.error = std::string("RBCP firmware read: ") + error.what();
  }

  try {
    easiroc::probeDataConnection(host);
    tcp_ok = true;
  } catch (const std::exception& error) {
    if (!result.error.empty()) result.error += "; ";
    result.error += std::string("TCP port 24 probe: ") + error.what();
  }

  result.connected = firmware_ok && tcp_ok;
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

  const BOOL connected = result->connected ? TRUE : FALSE;
  const std::string connected_path =
      std::string(kVariablesPath) + "/Connected";
  const INT status =
      db_set_value(hDB, 0, connected_path.c_str(), &connected,
                   sizeof(connected), 1, TID_BOOL);
  if (status != DB_SUCCESS)
    cm_msg(MERROR, "update_status", "Cannot write %s to ODB (status %d)",
           connected_path.c_str(), status);

  if (result->firmware_version) {
    const std::string path = std::string(kVariablesPath) + "/FirmwareVersion";
    set_odb_string(path.c_str(), *result->firmware_version, 64);
  }

  const std::string error_path = std::string(kVariablesPath) + "/LastError";
  set_odb_string(error_path.c_str(), result->error, 256);

  if (result->connected) {
    cm_msg(MINFO, "update_status",
           "Read-only status OK: RBCP firmware read and TCP port 24 probe "
           "succeeded");
  } else {
    cm_msg(MERROR, "update_status", "Read-only status failed: %s",
           result->error.c_str());
  }
}

void set_connected(bool connected) {
  const BOOL value = connected ? TRUE : FALSE;
  const std::string path = std::string(kVariablesPath) + "/Connected";
  const INT status = db_set_value(hDB, 0, path.c_str(), &value, sizeof(value),
                                  1, TID_BOOL);
  if (status != DB_SUCCESS)
    cm_msg(MERROR, "set_connected", "Cannot write %s (status %d)",
           path.c_str(), status);
}

void set_last_error(const std::string& error) {
  const std::string path = std::string(kVariablesPath) + "/LastError";
  set_odb_string(path.c_str(), error, 256);
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
      result.error = std::string("DAQ OFF failed; hardware state unknown: ") +
                     exception.what();
      cm_msg(MERROR, caller, "%s", result.error.c_str());
    }
  }

  if (drain_after_stop && result.daq_off_succeeded && g_state.tcp) {
    try {
      const std::size_t drained =
          g_state.tcp->drain(kDrainQuietMs, kDrainMaximumMs);
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

  g_state.acquisition_active = false;
  g_state.tcp.reset();
  g_state.parser.reset();
  g_state.pending_events.clear();
  if (!g_state.daq_start_attempted) g_state.rbcp.reset();
  set_connected(false);
  return result;
}

void handle_acquisition_error(const std::string& message) {
  if (g_state.acquisition_fault) return;
  g_state.acquisition_fault = true;
  cm_msg(MERROR, "poll_event", "Acquisition failed: %s", message.c_str());
  const CleanupResult cleanup = stop_acquisition("poll_event", false);
  std::string full_error = message;
  if (!cleanup.error.empty()) full_error += "; " + cleanup.error;
  set_last_error(full_error);

  char transition_error[256] = {};
  const INT status = cm_transition(TR_STOP, 0, transition_error,
                                   sizeof(transition_error), TR_ASYNC, FALSE);
  if (status != CM_SUCCESS)
    cm_msg(MERROR, "poll_event",
           "Cannot request asynchronous run stop (status %d): %s", status,
           transition_error);
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
    {""}};
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

INT frontend_init() {
  const INT status = create_odb_records();
  if (status != SUCCESS) return status;

  const INT settings_status = read_settings(&g_state.settings);
  if (settings_status != SUCCESS) return settings_status;
  start_diagnostic(g_state.settings.ip_address);

  cm_msg(MINFO, "frontend_init",
         "Started read-only RBCP firmware and TCP port 24 checks; no register "
         "write or TCP stream receive is performed");
  return SUCCESS;
}

INT frontend_exit() {
  if (g_diagnostic.thread.joinable()) g_diagnostic.thread.join();
  const CleanupResult cleanup = stop_acquisition("frontend_exit", true);
  if (!cleanup.daq_off_succeeded) return FE_ERR_HW;
  return cleanup.drain_succeeded ? SUCCESS : FE_ERR_HW;
}

INT begin_of_run(INT run_number, char* error) {
  if (error != nullptr) error[0] = '\0';

  // The read-only startup diagnostic must not share TCP/RBCP access with an
  // active acquisition.
  if (g_diagnostic.thread.joinable()) g_diagnostic.thread.join();
  publish_completed_diagnostic();

  const CleanupResult previous = stop_acquisition("begin_of_run", false);
  if (!previous.daq_off_succeeded) {
    if (error != nullptr)
      std::snprintf(error, 256, "%s", previous.error.c_str());
    return FE_ERR_HW;
  }

  const INT status = read_settings(&g_state.settings);
  if (status != SUCCESS) {
    if (error != nullptr)
      std::snprintf(error, 256, "Cannot read NIM-EASIROC settings from ODB");
    return status;
  }

  if (!g_state.settings.enables.adc || !g_state.settings.enables.tdc ||
      g_state.settings.enables.scaler) {
    const char* message =
        "Physics readout requires ADCEnabled=y, TDCEnabled=y, "
        "ScalerEnabled=n";
    cm_msg(MERROR, "begin_of_run", "%s", message);
    if (error != nullptr) std::snprintf(error, 256, "%s", message);
    set_last_error(message);
    return FE_ERR_ODB;
  }

  g_state.daq_control.setEnables(g_state.settings.enables);
  const auto start = g_state.daq_control.startValue();
  try {
    g_state.rbcp =
        std::make_unique<RbcpClient>(g_state.settings.ip_address);
    g_state.tcp = std::make_unique<TcpConnection>(
        g_state.settings.ip_address, kTcpPort, kConnectTimeoutMs);
    const std::size_t drained =
        g_state.tcp->drain(kDrainQuietMs, kDrainMaximumMs);
    cm_msg(MINFO, "begin_of_run",
           "Run %d connected to %s:%u; pre-acquisition drain discarded "
           "%zu byte(s)",
           run_number, g_state.settings.ip_address.c_str(),
           static_cast<unsigned>(kTcpPort), drained);

    g_state.parser = std::make_unique<easiroc::EventStreamParser>();
    g_state.pending_events.clear();
    g_state.daq_start_attempted = true;
    cm_msg(MINFO, "begin_of_run",
           "RBCP DAQ ON: address 0x%08x value 0x%02x",
           static_cast<unsigned>(start.address),
           static_cast<unsigned>(start.value));
    g_state.rbcp->write(start.address, start.value);
    g_state.acquisition_active = true;
    g_state.acquisition_fault = false;
    set_connected(true);
    set_last_error("");
    return SUCCESS;
  } catch (const std::exception& exception) {
    const std::string start_error =
        std::string("BOR acquisition setup failed: ") + exception.what();
    const CleanupResult cleanup = stop_acquisition("begin_of_run", false);
    std::string message = start_error;
    if (!cleanup.error.empty()) message += "; " + cleanup.error;
    cm_msg(MERROR, "begin_of_run", "%s", message.c_str());
    set_last_error(message);
    if (error != nullptr) std::snprintf(error, 256, "%s", message.c_str());
    return FE_ERR_HW;
  }
}

INT end_of_run(INT run_number, char* error) {
  if (error != nullptr) error[0] = '\0';
  cm_msg(MINFO, "end_of_run", "Stopping acquisition for run %d", run_number);
  const CleanupResult cleanup = stop_acquisition("end_of_run", true);
  if (!cleanup.error.empty()) {
    set_last_error(cleanup.error);
    if (error != nullptr)
      std::snprintf(error, 256, "%s", cleanup.error.c_str());
  }
  g_state.acquisition_fault = false;
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
  if (test || !g_state.acquisition_active || !g_state.tcp ||
      !g_state.parser)
    return FALSE;
  if (!g_state.pending_events.empty()) return TRUE;

  try {
    if (!g_state.tcp->dataAvailable(0)) return FALSE;
    const auto chunk =
        g_state.tcp->receive(kReceiveChunkBytes, kReceiveTimeoutMs);
    auto events = g_state.parser->push(chunk);
    for (const auto& event : events)
      g_state.pending_events.push_back(easiroc::organizeEvent(event));
    return g_state.pending_events.empty() ? FALSE : TRUE;
  } catch (const std::exception& exception) {
    handle_acquisition_error(exception.what());
    return FALSE;
  }
}

INT interrupt_configure(INT, INT, PTYPE) { return SUCCESS; }

INT read_physics_event(char* pevent, INT) {
  if (g_state.pending_events.empty()) return 0;

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

  return bk_size(pevent);
}

INT read_status_event(char*, INT) {
  publish_completed_diagnostic();

  if (g_state.acquisition_active || g_state.daq_start_attempted) return 0;

  FrontendSettings settings;
  if (read_settings(&settings) == SUCCESS) {
    g_state.settings = settings;
    start_diagnostic(settings.ip_address);
  }

  // Status is published directly into ODB. Returning zero suppresses an empty
  // MIDAS event; no TCP stream data is received here.
  return 0;
}
