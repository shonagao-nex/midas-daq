#include "hul_bank.h"
#include "hul_monitor.h"
#include "hul_snapshot.h"
#include "hul_stream.h"
#include "hul_run_guard.h"
#include "official/FPGAModule.hh"
#include "official/RegisterMap.hh"
#include "official/UDPRBCP.hh"

#include "midas.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

extern EQUIPMENT equipment[];

namespace {
using namespace HUL::Scaler;
constexpr char kScalerName[] = "HUL Scaler 1";
constexpr int kScalerIndex = 0;
constexpr short kScalerFinalEventId = 11;

struct Settings {
  char ip[64] = "192.168.10.60";
  DWORD rbcp_port = 4660;
  DWORD tcp_port = 24;
  DWORD enable_block = 3;
  DWORD freshness_ms = 2000;
  DWORD drain_ms = 2000;
};

struct ScalerModule {
  hul::ReachabilityMonitor monitor;
  const char* equipment_name = kScalerName;
  int equipment_index = kScalerIndex;
  short final_event_id = kScalerFinalEventId;
  HNDLE enabled_key = 0;
  WORD enabled_mode = 0;
  bool enable_locked = false;
  bool enabled_for_run = false;
  bool run_registered = false;
  std::mutex mutex;
  hul::SnapshotAccumulator accumulator;
  hul::FinalGuard final_guard;
  std::thread receiver;
  std::atomic<bool> drain{false};
  std::atomic<bool> stop{false};
  std::atomic<bool> active{false};
  bool eor_prepared = false;
  bool gate_confirmed_off = false;
  bool fault_reported = false;
  int32_t run_number = 0;
  int tcp_fd = -1;
  std::string fault;
  Settings settings;
  std::unique_ptr<RBCP::UDPRBCP> rbcp;
  std::unique_ptr<HUL::FPGAModule> module;
};
ScalerModule scaler1;
HNDLE hDB = 0;

std::string equipment_path(const ScalerModule& module, const char* suffix) {
  return std::string("/Equipment/") + module.equipment_name + suffix;
}

bool unlock_enabled(ScalerModule& module) {
  if (!module.enable_locked) return true;
  if (db_set_mode(hDB, module.enabled_key, module.enabled_mode, FALSE) != DB_SUCCESS)
    return false;
  module.enable_locked = false;
  return true;
}

bool lock_and_read_enabled(ScalerModule& module, bool* enabled) {
  const std::string path = equipment_path(module, "/Common/Enabled");
  HNDLE key = 0;
  KEY info{};
  if (db_find_key(hDB, 0, path.c_str(), &key) != DB_SUCCESS ||
      db_get_key(hDB, key, &info) != DB_SUCCESS ||
      info.type != TID_BOOL || !(info.access_mode & MODE_WRITE))
    return false;
  if (db_set_mode(hDB, key, MODE_READ, FALSE) != DB_SUCCESS) return false;
  module.enabled_key = key;
  module.enabled_mode = info.access_mode;
  module.enable_locked = true;
  BOOL value = FALSE;
  INT size = sizeof(value);
  if (db_get_value(hDB, 0, path.c_str(), &value, &size, TID_BOOL, FALSE) != DB_SUCCESS) {
    unlock_enabled(module);
    return false;
  }
  *enabled = value != FALSE;
  return true;
}

bool run_is_stopped() {
  INT state = 0;
  INT size = sizeof(state);
  return db_get_value(hDB, 0, "/Runinfo/State", &state, &size,
                      TID_INT, FALSE) == DB_SUCCESS && state == STATE_STOPPED;
}

uint64_t unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

bool get_or_create(ScalerModule& module, const char* key, void* data, INT size,
                   DWORD type, bool create) {
  const std::string path = equipment_path(module, "/Settings/") + key;
  return db_get_value(hDB, 0, path.c_str(), data, &size, type,
                      create ? TRUE : FALSE) == DB_SUCCESS;
}

bool load_settings(ScalerModule& module, bool create) {
  Settings s;
  if (!get_or_create(module, "IPAddress", s.ip, sizeof(s.ip), TID_STRING, create) ||
      !get_or_create(module, "RBCPPort", &s.rbcp_port, sizeof(s.rbcp_port), TID_DWORD, create) ||
      !get_or_create(module, "TCPPort", &s.tcp_port, sizeof(s.tcp_port), TID_DWORD, create) ||
      !get_or_create(module, "EnableBlock", &s.enable_block, sizeof(s.enable_block), TID_DWORD, create) ||
      !get_or_create(module, "FreshnessMs", &s.freshness_ms, sizeof(s.freshness_ms), TID_DWORD, create) ||
      !get_or_create(module, "DrainMs", &s.drain_ms, sizeof(s.drain_ms), TID_DWORD, create))
    return false;
  s.ip[sizeof(s.ip) - 1] = '\0';
  in_addr addr{};
  if (inet_pton(AF_INET, s.ip, &addr) != 1 || s.rbcp_port == 0 ||
      s.rbcp_port > 65535 || s.tcp_port == 0 || s.tcp_port > 65535 ||
      s.enable_block != 3 || s.freshness_ms == 0 || s.drain_ms < 200 ||
      s.drain_ms > 10000)
    return false;
  module.settings = s;
  return true;
}

bool ensure_variable(ScalerModule& module, const char* name, int count) {
  const std::string path = equipment_path(module, "/Variables/") + name;
  HNDLE key = 0;
  const INT found = db_find_key(hDB, 0, path.c_str(), &key);
  if (found == DB_NO_KEY) {
    const uint32_t zeros[64]{};
    return db_set_value(hDB, 0, path.c_str(), zeros,
                        count * sizeof(uint32_t), count, TID_DWORD) == DB_SUCCESS;
  }
  if (found != DB_SUCCESS) return false;
  KEY info{};
  return db_get_key(hDB, key, &info) == DB_SUCCESS &&
         info.type == TID_DWORD && info.num_values == count;
}

void status_string(ScalerModule& module, const char* key, const std::string& value) {
  const std::string path = equipment_path(module, "/Status/") + key;
  if (db_set_value(hDB, 0, path.c_str(), value.c_str(),
                   value.size() + 1, 1, TID_STRING) != DB_SUCCESS)
    cm_msg(MERROR, "fehul", "Cannot publish %s", path.c_str());
}

void set_fault(ScalerModule& module, const std::string& message) {
  std::lock_guard<std::mutex> lock(module.mutex);
  if (module.fault.empty()) module.fault = message;
}

bool register_write(ScalerModule& module, uint32_t address, uint32_t value, int bytes = 1) {
  return module.module && module.module->WriteModule(address, value, bytes) == 0;
}

bool register_read(ScalerModule& module, uint32_t address, uint32_t* value, int bytes = 1) {
  if (!module.module || !value || bytes < 1 || bytes > 4) return false;
  uint32_t result = 0;
  for (int i = 0; i < bytes; ++i) {
    if (module.module->ReadModule_nByte(address + (uint32_t(i) << 16), 1) < 0)
      return false;
    auto begin = module.module->GetDataIteratorBegin();
    if (begin == module.module->GetDataIteratorEnd()) return false;
    result |= uint32_t(*begin) << (8 * i);
  }
  *value = result;
  return true;
}

bool write_checked(ScalerModule& module, uint32_t address, uint32_t value, int bytes = 1) {
  uint32_t readback = 0;
  return register_write(module, address, value, bytes) &&
         register_read(module, address, &readback, bytes) && readback == value;
}

bool gate(ScalerModule& module, bool on) {
  const bool ok = write_checked(module, DCT::kAddrDaqGate, on ? 1 : 0);
  if (!on) module.gate_confirmed_off = ok;
  return ok;
}

int connect_tcp(const Settings& s) {
  const int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (fd < 0) return -1;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(s.tcp_port);
  if (inet_pton(AF_INET, s.ip, &address.sin_addr) != 1) {
    close(fd);
    return -1;
  }
  int status = connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  if (status < 0 && errno == EINPROGRESS) {
    pollfd pfd{fd, POLLOUT, 0};
    status = poll(&pfd, 1, 2000);
    int error = 0;
    socklen_t length = sizeof(error);
    if (status <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error)
      status = -1;
    else status = 0;
  }
  if (status < 0) {
    close(fd);
    return -1;
  }
  int yes = 1;
  if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

// After Gate OFF, discard prior-run bytes before EVB/counter reset.
bool drain_old_bytes(int fd) {
  const auto deadline = hul::Clock::now() + std::chrono::seconds(2);
  size_t total = 0;
  char bytes[4096];
  while (hul::Clock::now() < deadline && total < 1024 * 1024) {
    pollfd pfd{fd, POLLIN, 0};
    const int ready = poll(&pfd, 1, 100);
    if (ready < 0 && errno == EINTR) continue;
    if (ready < 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) return false;
    if (ready == 0) return true;
    const ssize_t count = recv(fd, bytes, sizeof(bytes), 0);
    if (count <= 0) return false;
    total += static_cast<size_t>(count);
  }
  return false;
}

void receiver(ScalerModule& module) {
  hul::StreamReader stream(module.tcp_fd);
  hul::Clock::time_point drain_started{};
  while (!module.stop.load()) {
    if (module.drain.load() && drain_started == hul::Clock::time_point{})
      drain_started = hul::Clock::now();
    if (module.drain.load() && hul::Clock::now() - drain_started >=
                               std::chrono::milliseconds(module.settings.drain_ms))
      break;
    hul::ScalerEvent event;
    std::string error;
    const hul::ReadStatus result = stream.next(&event, 200, &error);
    if (result == hul::ReadStatus::Event) {
      std::lock_guard<std::mutex> lock(module.mutex);
      module.accumulator.accept(event, hul::Clock::now(), unix_ms());
    } else if (result == hul::ReadStatus::Idle) {
      if (module.drain.load()) break;
    } else {
      set_fault(module, error.empty() ? "Scaler TCP EOF" : error);
      break;
    }
  }
}

void stop_receiver(ScalerModule& module) {
  module.stop = true;
  if (module.receiver.joinable()) module.receiver.join();
  if (module.tcp_fd >= 0) close(module.tcp_fd);
  module.tcp_fd = -1;
}

bool send_final(ScalerModule& module) {
  if (!equipment[module.equipment_index].buffer_handle) return false;
  if (module.final_guard.may_enqueue()) {
    alignas(8) char bytes[2048]{};
    auto* header = reinterpret_cast<EVENT_HEADER*>(bytes);
    header->event_id = module.final_event_id;
    header->trigger_mask = 0;
    header->serial_number = equipment[module.equipment_index].serial_number;
    header->time_stamp = ss_time();
    hul::Record record;
    {
      std::lock_guard<std::mutex> lock(module.mutex);
      record = module.accumulator.take(hul::Clock::now(), module.settings.freshness_ms,
                                  true, !module.fault.empty());
    }
    header->data_size = hul::encode_record(reinterpret_cast<char*>(header + 1), record);
    const bool enqueued =
        bm_send_event(equipment[module.equipment_index].buffer_handle, header, 0, BM_WAIT) == BM_SUCCESS;
    module.final_guard.enqueue_result(enqueued);
    if (!enqueued) return false;
    ++equipment[module.equipment_index].serial_number;
  }
  if (module.final_guard.may_flush())
    module.final_guard.flush_result(
        bm_flush_cache(equipment[module.equipment_index].buffer_handle, BM_WAIT) == BM_SUCCESS);
  return module.final_guard.complete();
}
}  // namespace

const char* frontend_name = "fehul";
const char* frontend_file_name = __FILE__;
BOOL frontend_call_loop = TRUE;
INT display_period = 0;
INT max_event_size = 2048;
INT max_event_size_frag = 0;
INT event_buffer_size = 8192;
BOOL equipment_common_overwrite = FALSE;

INT read_periodic(char* pevent, INT);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
EQUIPMENT equipment[] = {
    {"HUL Scaler 1", {10, 0, "SYSTEM", EQ_PERIODIC, 0, "MIDAS", TRUE,
                      RO_RUNNING | RO_ODB, 1000, 0, 0, 1}, read_periodic},
    {""}};
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

INT poll_event(INT, INT, BOOL) { return 0; }
INT interrupt_configure(INT, INT, PTYPE) { return SUCCESS; }

INT start_abort(INT, char*) {
  scaler1.monitor.cancel();
  status_string(scaler1, "Connection", "Unknown");
  ScalerModule& module = scaler1;
  if (module.module && !gate(module, false))
    cm_msg(MERROR, frontend_name, "Gate OFF could not be confirmed after START abort");
  stop_receiver(module);
  module.module.reset();
  module.rbcp.reset();
  module.active = false;
  module.eor_prepared = false;
  module.enabled_for_run = false;
  module.run_registered = false;
  status_string(module, "Gate", module.gate_confirmed_off ? "Closed" : "Unknown");
  if (!unlock_enabled(module))
    cm_msg(MERROR, frontend_name, "Cannot unlock %s Common/Enabled after START abort",
           module.equipment_name);
  return SUCCESS;
}

INT frontend_init() {
  cm_get_experiment_database(&hDB, nullptr);
  status_string(scaler1, "Connection", "Unknown");
  ScalerModule& module = scaler1;
  if (!load_settings(module, true)) return FE_ERR_ODB;
  // Publish the complete schema at frontend startup for History tag discovery.
  if (!ensure_variable(module, "HCUM", 64) ||
      !ensure_variable(module, "HDIF", 64) ||
      !ensure_variable(module, "HVAL", 15)) return FE_ERR_ODB;
  if (cm_register_transition(TR_STARTABORT, start_abort, 500) != CM_SUCCESS)
    return FE_ERR_ODB;
  status_string(module, "Gate", "Unknown");
  return SUCCESS;
}

INT frontend_exit() {
  scaler1.monitor.cancel();
  status_string(scaler1, "Connection", "Unknown");
  ScalerModule& module = scaler1;
  if (module.module && !gate(module, false))
    cm_msg(MERROR, frontend_name, "Gate OFF could not be confirmed on exit");
  stop_receiver(module);
  module.module.reset();
  module.rbcp.reset();
  if (run_is_stopped() && !unlock_enabled(module))
    cm_msg(MERROR, frontend_name, "Cannot unlock %s Common/Enabled on exit",
           module.equipment_name);
  else if (module.enable_locked)
    cm_msg(MERROR, frontend_name,
           "Leaving %s Common/Enabled read-only until STOPPED recovery",
           module.equipment_name);
  return SUCCESS;
}

INT begin_of_run(INT run_number, char* error) {
  scaler1.monitor.cancel();
  status_string(scaler1, "Connection", "Unknown");
  ScalerModule& module = scaler1;
  if (run_number <= 0 || module.run_registered || module.active ||
      module.eor_prepared) {
    std::snprintf(error, 256, "%s run already active or invalid", module.equipment_name);
    return FE_ERR_ODB;
  }
  bool enabled = false;
  if (!lock_and_read_enabled(module, &enabled)) {
    std::snprintf(error, 256, "Cannot lock/read %s Common/Enabled", module.equipment_name);
    return FE_ERR_ODB;
  }
  module.enabled_for_run = enabled;
  module.run_registered = true;
  module.run_number = run_number;
  if (!enabled) {
    status_string(module, "Connection", "Disabled");
      status_string(module, "Gate", "Not operated");
    return SUCCESS;
  }
  if (!load_settings(module, false)) {
    std::snprintf(error, 256, "%s settings invalid", module.equipment_name);
    module.run_registered = false;
    module.enabled_for_run = false;
    if (!unlock_enabled(module))
      cm_msg(MERROR, frontend_name, "Cannot unlock %s Common/Enabled after invalid settings",
             module.equipment_name);
    return FE_ERR_ODB;
  }
  module.rbcp.reset(new RBCP::UDPRBCP(module.settings.ip, module.settings.rbcp_port,
                                       RBCP::UDPRBCP::kNoDisp));
  module.module.reset(new HUL::FPGAModule(*module.rbcp));
  module.gate_confirmed_off = false;
  if (!gate(module, false)) goto fail;
  module.tcp_fd = connect_tcp(module.settings);
  if (module.tcp_fd < 0 || !drain_old_bytes(module.tcp_fd)) goto fail;
  // Reset strobes are each sent once; their lost ACK leaves hardware state
  // indeterminate and aborts BOR without an automatic retransmission.
  if (!register_write(module, DCT::kAddrResetEvb, 1) ||
      !register_write(module, SCR::kAddrCounterReset, 1) ||
      !write_checked(module, SCR::kAddrEnableBlock, 3) ||
      !write_checked(module, SCR::kAddrEnableHdrst, 0) ||
      !write_checked(module, IOM::kAddrExtL1, IOM::kReg_i_Nimin1) ||
      !write_checked(module, IOM::kAddrExtSpillGate, IOM::kReg_i_Default) ||
      !write_checked(module, IOM::kAddrExtBusy, IOM::kReg_i_Default) ||
      !write_checked(module, TRM::kAddrSelectTrigger, TRM::kRegL1Ext, 2))
    goto fail;
  {
    std::lock_guard<std::mutex> lock(module.mutex);
    module.accumulator.reset(run_number);
    module.fault.clear();
    module.fault_reported = false;
  }
  module.final_guard.begin(run_number);
  module.eor_prepared = false;
  module.drain = false;
  module.stop = false;
  module.receiver = std::thread(receiver, std::ref(module));
  if (!gate(module, true)) goto fail;
  module.active = true;
  status_string(module, "Gate", "Open");
  return SUCCESS;

fail:
  if (!gate(module, false)) status_string(module, "Gate", "Unknown");
  else status_string(module, "Gate", "Closed");
  stop_receiver(module);
  module.module.reset();
  module.rbcp.reset();
  module.run_registered = false;
  module.enabled_for_run = false;
  if (!unlock_enabled(module))
    cm_msg(MERROR, frontend_name, "Cannot unlock %s Common/Enabled after BOR failure",
           module.equipment_name);
  std::snprintf(error, 256, "HUL BOR communication or configuration failed");
  cm_msg(MERROR, frontend_name, "%s", error);
  return FE_ERR_HW;
}

INT end_of_run(INT run_number, char* error) {
  scaler1.monitor.cancel();
  status_string(scaler1, "Connection", "Unknown");
  ScalerModule& module = scaler1;
  if (!module.run_registered || run_number != module.run_number) {
    std::snprintf(error, 256, "HUL EOR run number mismatch");
    return FE_ERR_HW;
  }
  if (!module.enabled_for_run) {
    status_string(module, "Connection", "Disabled");
    module.run_registered = false;
    if (!unlock_enabled(module)) {
      std::snprintf(error, 256, "Cannot unlock disabled HUL Scaler 1");
      return FE_ERR_ODB;
    }
      return SUCCESS;
  }
  if ((!module.active && !module.eor_prepared) ||
      !module.final_guard.prepare(run_number)) {
    std::snprintf(error, 256, "HUL EOR run number mismatch");
    return FE_ERR_HW;
  }
  if (!module.eor_prepared) {
    // Exclude periodic snapshots before freezing the final value. Both paths
    // use the snapshot mutex, so no periodic take() can follow this point.
    {
      std::lock_guard<std::mutex> lock(module.mutex);
      module.active = false;
    }
    module.gate_confirmed_off = gate(module, false);
    module.drain = true;
    if (module.receiver.joinable()) module.receiver.join();
    if (module.tcp_fd >= 0) close(module.tcp_fd);
    module.tcp_fd = -1;
    module.eor_prepared = true;
  } else if (!module.gate_confirmed_off) {
    module.gate_confirmed_off = gate(module, false);
  }
  const bool final_ok = send_final(module);
  status_string(module, "Gate", module.gate_confirmed_off ? "Closed" : "Unknown");
  {
    std::lock_guard<std::mutex> lock(module.mutex);
    if (!module.fault.empty() && !module.fault_reported) {
      cm_msg(MERROR, frontend_name, "%s", module.fault.c_str());
      status_string(module, "LastError", module.fault);
      module.fault_reported = true;
    }
  }
  if (!module.gate_confirmed_off || !final_ok) {
    std::snprintf(error, 256, "HUL EOR Gate OFF or final event failed");
    cm_msg(MERROR, frontend_name, "%s", error);
    return FE_ERR_HW;
  }
  module.eor_prepared = false;
  module.module.reset();
  module.rbcp.reset();
  module.run_registered = false;
  module.enabled_for_run = false;
  if (!unlock_enabled(module)) {
    std::snprintf(error, 256, "Cannot unlock HUL Scaler 1 Common/Enabled");
    return FE_ERR_ODB;
  }
  return SUCCESS;
}

INT pause_run(INT, char* error) {
  std::snprintf(error, 256, "HUL pause is unsupported");
  return FE_ERR_HW;
}
INT resume_run(INT, char*) { return SUCCESS; }
INT frontend_loop() {
  auto& m = scaler1;
  BOOL enabled = FALSE;
  INT transition = 0, size = sizeof(enabled);
  bool known = db_get_value(hDB, 0, equipment_path(m, "/Common/Enabled").c_str(),
      &enabled, &size, TID_BOOL, FALSE) == DB_SUCCESS;
  const bool enabled_known = known;
  size = sizeof(transition);
  known = db_get_value(hDB, 0, "/Runinfo/Transition in progress", &transition,
      &size, TID_INT, FALSE) == DB_SUCCESS && known;
  char ip[64]{}; DWORD port = 0;
  size = sizeof(ip);
  known = db_get_value(hDB, 0, equipment_path(m, "/Settings/IPAddress").c_str(),
      ip, &size, TID_STRING, FALSE) == DB_SUCCESS && known;
  ip[sizeof(ip)-1] = 0;
  size = sizeof(port);
  known = db_get_value(hDB, 0, equipment_path(m, "/Settings/RBCPPort").c_str(),
      &port, &size, TID_DWORD, FALSE) == DB_SUCCESS && known;
  // MFE loop and transitions execute on the same thread. Cancel before BOR/EOR;
  // never wait for a monitoring reply or share the DAQ RBCP object.
  const char* state = m.monitor.tick(known && enabled, known && transition == 0, ip, port);
  status_string(m, "Connection", enabled_known && !enabled ? "Disabled" : known ? state : "Unknown");
  DWORD checked = ss_time();
  db_set_value(hDB, 0, equipment_path(m, "/Status/CommCheckedAt").c_str(),
               &checked, sizeof(checked), 1, TID_DWORD);
  return SUCCESS;
}

INT read_periodic(char* pevent, INT) {
  ScalerModule& module = scaler1;
  hul::Record record;
  std::string fault;
  {
    std::lock_guard<std::mutex> lock(module.mutex);
    if (!module.enabled_for_run || !module.active) return 0;
    record = module.accumulator.take(hul::Clock::now(), module.settings.freshness_ms,
                                     false, !module.fault.empty());
    if (!module.fault.empty() && !module.fault_reported) {
      fault = module.fault;
      module.fault_reported = true;
    }
  }
  if (!fault.empty()) {
    cm_msg(MERROR, frontend_name, "%s", fault.c_str());
      status_string(module, "LastError", fault);
  }
  return hul::encode_record(pevent, record);
}
