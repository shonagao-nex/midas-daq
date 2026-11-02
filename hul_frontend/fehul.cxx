#include "hul_bank.h"
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
#include <memory>
#include <mutex>
#include <string>
#include <thread>

extern EQUIPMENT equipment[];

namespace {
using namespace HUL::Scaler;
constexpr char kSettings[] = "/Equipment/HUL/Settings/";
constexpr char kStatus[] = "/Equipment/HUL/Status/";
constexpr int kPeriodicIndex = 0;
constexpr int kFinalIndex = 1;

struct Settings {
  char ip[64] = "192.168.10.60";
  DWORD rbcp_port = 4660;
  DWORD tcp_port = 24;
  DWORD enable_block = 3;
  DWORD freshness_ms = 2000;
  DWORD drain_ms = 2000;
};

struct State {
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
State g;
HNDLE hDB = 0;

uint64_t unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

bool get_or_create(const char* key, void* data, INT size, DWORD type,
                   bool create) {
  const std::string path = std::string(kSettings) + key;
  return db_get_value(hDB, 0, path.c_str(), data, &size, type,
                      create ? TRUE : FALSE) == DB_SUCCESS;
}

bool load_settings(bool create) {
  Settings s;
  if (!get_or_create("IPAddress", s.ip, sizeof(s.ip), TID_STRING, create) ||
      !get_or_create("RBCPPort", &s.rbcp_port, sizeof(s.rbcp_port), TID_DWORD, create) ||
      !get_or_create("TCPPort", &s.tcp_port, sizeof(s.tcp_port), TID_DWORD, create) ||
      !get_or_create("EnableBlock", &s.enable_block, sizeof(s.enable_block), TID_DWORD, create) ||
      !get_or_create("FreshnessMs", &s.freshness_ms, sizeof(s.freshness_ms), TID_DWORD, create) ||
      !get_or_create("DrainMs", &s.drain_ms, sizeof(s.drain_ms), TID_DWORD, create))
    return false;
  s.ip[sizeof(s.ip) - 1] = '\0';
  in_addr addr{};
  if (inet_pton(AF_INET, s.ip, &addr) != 1 || s.rbcp_port == 0 ||
      s.rbcp_port > 65535 || s.tcp_port == 0 || s.tcp_port > 65535 ||
      s.enable_block != 3 || s.freshness_ms == 0 || s.drain_ms < 200 ||
      s.drain_ms > 10000)
    return false;
  g.settings = s;
  return true;
}

bool ensure_variable(const char* name, int count) {
  const std::string path = std::string("/Equipment/HUL/Variables/") + name;
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

void status_string(const char* key, const std::string& value) {
  const std::string path = std::string(kStatus) + key;
  if (db_set_value(hDB, 0, path.c_str(), value.c_str(),
                   value.size() + 1, 1, TID_STRING) != DB_SUCCESS)
    cm_msg(MERROR, "fehul", "Cannot publish %s", path.c_str());
}

void set_fault(const std::string& message) {
  std::lock_guard<std::mutex> lock(g.mutex);
  if (g.fault.empty()) g.fault = message;
}

bool register_write(uint32_t address, uint32_t value, int bytes = 1) {
  return g.module && g.module->WriteModule(address, value, bytes) == 0;
}

bool register_read(uint32_t address, uint32_t* value, int bytes = 1) {
  if (!g.module || !value || bytes < 1 || bytes > 4) return false;
  uint32_t result = 0;
  for (int i = 0; i < bytes; ++i) {
    if (g.module->ReadModule_nByte(address + (uint32_t(i) << 16), 1) < 0)
      return false;
    auto begin = g.module->GetDataIteratorBegin();
    if (begin == g.module->GetDataIteratorEnd()) return false;
    result |= uint32_t(*begin) << (8 * i);
  }
  *value = result;
  return true;
}

bool write_checked(uint32_t address, uint32_t value, int bytes = 1) {
  uint32_t readback = 0;
  return register_write(address, value, bytes) &&
         register_read(address, &readback, bytes) && readback == value;
}

bool gate(bool on) {
  const bool ok = write_checked(DCT::kAddrDaqGate, on ? 1 : 0);
  if (!on) g.gate_confirmed_off = ok;
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

void receiver() {
  hul::StreamReader stream(g.tcp_fd);
  hul::Clock::time_point drain_started{};
  while (!g.stop.load()) {
    if (g.drain.load() && drain_started == hul::Clock::time_point{})
      drain_started = hul::Clock::now();
    if (g.drain.load() && hul::Clock::now() - drain_started >=
                               std::chrono::milliseconds(g.settings.drain_ms))
      break;
    hul::ScalerEvent event;
    std::string error;
    const hul::ReadStatus result = stream.next(&event, 200, &error);
    if (result == hul::ReadStatus::Event) {
      std::lock_guard<std::mutex> lock(g.mutex);
      g.accumulator.accept(event, hul::Clock::now(), unix_ms());
    } else if (result == hul::ReadStatus::Idle) {
      if (g.drain.load()) break;
    } else {
      set_fault(error.empty() ? "Scaler TCP EOF" : error);
      break;
    }
  }
}

void stop_receiver() {
  g.stop = true;
  if (g.receiver.joinable()) g.receiver.join();
  if (g.tcp_fd >= 0) close(g.tcp_fd);
  g.tcp_fd = -1;
}

bool send_final() {
  if (!equipment[kFinalIndex].buffer_handle) return false;
  if (g.final_guard.may_enqueue()) {
    alignas(8) char bytes[2048]{};
    auto* header = reinterpret_cast<EVENT_HEADER*>(bytes);
    header->event_id = equipment[kFinalIndex].info.event_id;
    header->trigger_mask = 0;
    header->serial_number = equipment[kFinalIndex].serial_number;
    header->time_stamp = ss_time();
    hul::Record record;
    {
      std::lock_guard<std::mutex> lock(g.mutex);
      record = g.accumulator.take(hul::Clock::now(), g.settings.freshness_ms,
                                  true, !g.fault.empty());
    }
    header->data_size = hul::encode_record(reinterpret_cast<char*>(header + 1), record);
    const bool enqueued =
        bm_send_event(equipment[kFinalIndex].buffer_handle, header, 0, BM_WAIT) == BM_SUCCESS;
    g.final_guard.enqueue_result(enqueued);
    if (!enqueued) return false;
    ++equipment[kFinalIndex].serial_number;
  }
  if (g.final_guard.may_flush())
    g.final_guard.flush_result(
        bm_flush_cache(equipment[kFinalIndex].buffer_handle, BM_WAIT) == BM_SUCCESS);
  return g.final_guard.complete();
}
}  // namespace

const char* frontend_name = "fehul";
const char* frontend_file_name = __FILE__;
BOOL frontend_call_loop = FALSE;
INT display_period = 0;
INT max_event_size = 2048;
INT max_event_size_frag = 0;
INT event_buffer_size = 8192;
BOOL equipment_common_overwrite = FALSE;

INT read_periodic(char* pevent, INT);
INT read_final_placeholder(char*, INT) { return 0; }

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
EQUIPMENT equipment[] = {
    {"HUL", {10, 0, "SYSTEM", EQ_PERIODIC, 0, "MIDAS", TRUE,
                      RO_RUNNING | RO_ODB, 1000, 0, 0, 1}, read_periodic},
    {"HUL Final", {11, 0, "SYSTEM", EQ_PERIODIC, 0, "MIDAS", TRUE,
                   0, 0, 0, 0, 0}, read_final_placeholder},
    {""}};
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

INT poll_event(INT, INT, BOOL) { return 0; }
INT interrupt_configure(INT, INT, PTYPE) { return SUCCESS; }

INT frontend_init() {
  cm_get_experiment_database(&hDB, nullptr);
  if (!load_settings(true)) return FE_ERR_ODB;
  // Publish the complete schema at frontend startup for History tag discovery.
  if (!ensure_variable("HCUM", 64) || !ensure_variable("HDIF", 64) ||
      !ensure_variable("HVAL", 15)) return FE_ERR_ODB;
  status_string("Connection", "Disconnected");
  status_string("Gate", "Unknown");
  return SUCCESS;
}

INT frontend_exit() {
  if (g.module && !gate(false))
    cm_msg(MERROR, frontend_name, "Gate OFF could not be confirmed on exit");
  stop_receiver();
  g.module.reset();
  g.rbcp.reset();
  return SUCCESS;
}

INT begin_of_run(INT run_number, char* error) {
  if (run_number <= 0 || g.active || g.eor_prepared || !load_settings(false)) {
    std::snprintf(error, 256, "HUL settings invalid or run already active");
    return FE_ERR_ODB;
  }
  g.rbcp.reset(new RBCP::UDPRBCP(g.settings.ip, g.settings.rbcp_port,
                                   RBCP::UDPRBCP::kNoDisp));
  g.module.reset(new HUL::FPGAModule(*g.rbcp));
  g.gate_confirmed_off = false;
  if (!gate(false)) goto fail;
  g.tcp_fd = connect_tcp(g.settings);
  if (g.tcp_fd < 0 || !drain_old_bytes(g.tcp_fd)) goto fail;
  // Reset strobes are each sent once; their lost ACK leaves hardware state
  // indeterminate and aborts BOR without an automatic retransmission.
  if (!register_write(DCT::kAddrResetEvb, 1) ||
      !register_write(SCR::kAddrCounterReset, 1) ||
      !write_checked(SCR::kAddrEnableBlock, 3) ||
      !write_checked(SCR::kAddrEnableHdrst, 0) ||
      !write_checked(IOM::kAddrExtL1, IOM::kReg_i_Nimin1) ||
      !write_checked(IOM::kAddrExtSpillGate, IOM::kReg_i_Default) ||
      !write_checked(IOM::kAddrExtBusy, IOM::kReg_i_Default) ||
      !write_checked(TRM::kAddrSelectTrigger, TRM::kRegL1Ext, 2))
    goto fail;
  {
    std::lock_guard<std::mutex> lock(g.mutex);
    g.accumulator.reset(run_number);
    g.fault.clear();
    g.fault_reported = false;
  }
  g.run_number = run_number;
  g.final_guard.begin(run_number);
  g.eor_prepared = false;
  g.drain = false;
  g.stop = false;
  g.receiver = std::thread(receiver);
  if (!gate(true)) goto fail;
  g.active = true;
  status_string("Connection", "Connected");
  status_string("Gate", "Open");
  return SUCCESS;

fail:
  if (!gate(false)) status_string("Gate", "Unknown");
  else status_string("Gate", "Closed");
  stop_receiver();
  g.module.reset();
  g.rbcp.reset();
  std::snprintf(error, 256, "HUL BOR communication or configuration failed");
  cm_msg(MERROR, frontend_name, "%s", error);
  return FE_ERR_HW;
}

INT end_of_run(INT run_number, char* error) {
  if ((!g.active && !g.eor_prepared) || run_number != g.run_number ||
      !g.final_guard.prepare(run_number)) {
    std::snprintf(error, 256, "HUL EOR run number mismatch");
    return FE_ERR_HW;
  }
  if (!g.eor_prepared) {
    // Exclude periodic snapshots before freezing the final value. Both paths
    // use the snapshot mutex, so no periodic take() can follow this point.
    {
      std::lock_guard<std::mutex> lock(g.mutex);
      g.active = false;
    }
    g.gate_confirmed_off = gate(false);
    g.drain = true;
    if (g.receiver.joinable()) g.receiver.join();
    if (g.tcp_fd >= 0) close(g.tcp_fd);
    g.tcp_fd = -1;
    g.eor_prepared = true;
  } else if (!g.gate_confirmed_off) {
    g.gate_confirmed_off = gate(false);
  }
  const bool final_ok = send_final();
  status_string("Gate", g.gate_confirmed_off ? "Closed" : "Unknown");
  status_string("Connection", "Disconnected");
  {
    std::lock_guard<std::mutex> lock(g.mutex);
    if (!g.fault.empty() && !g.fault_reported) {
      cm_msg(MERROR, frontend_name, "%s", g.fault.c_str());
      status_string("LastError", g.fault);
      g.fault_reported = true;
    }
  }
  if (!g.gate_confirmed_off || !final_ok) {
    std::snprintf(error, 256, "HUL EOR Gate OFF or final event failed");
    cm_msg(MERROR, frontend_name, "%s", error);
    return FE_ERR_HW;
  }
  g.eor_prepared = false;
  g.module.reset();
  g.rbcp.reset();
  return SUCCESS;
}

INT pause_run(INT, char* error) {
  std::snprintf(error, 256, "HUL pause is unsupported");
  return FE_ERR_HW;
}
INT resume_run(INT, char*) { return SUCCESS; }
INT frontend_loop() { return SUCCESS; }

INT read_periodic(char* pevent, INT) {
  hul::Record record;
  std::string fault;
  {
    std::lock_guard<std::mutex> lock(g.mutex);
    if (!g.active) return 0;
    record = g.accumulator.take(hul::Clock::now(), g.settings.freshness_ms,
                                false, !g.fault.empty());
    if (!g.fault.empty() && !g.fault_reported) {
      fault = g.fault;
      g.fault_reported = true;
    }
  }
  if (!fault.empty()) {
    cm_msg(MERROR, frontend_name, "%s", fault.c_str());
    status_string("Connection", "Fault");
    status_string("LastError", fault);
  }
  return hul::encode_record(pevent, record);
}
