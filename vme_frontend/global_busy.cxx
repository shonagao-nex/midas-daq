#include "global_busy.h"
#include "global_busy_readiness.h"

#include <CAENVMElib.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace global_busy {
namespace {
constexpr const char* kStatus = "/Equipment/VME/Status";
constexpr const char* kPolarity = "/Equipment/VME/Settings/GlobalBusySetMeansBusy";
constexpr INT kProvisionalSetMeansBusy = 1;
// V3718 rev.4 IO_STATUS_SET (0x0C) uses bit 0 for OUT0. CAENVMELib 4.1.3
// applies Set/ClearOutputRegister masks to that register without remapping.
constexpr unsigned short kV3718Out0StatusBit = 0x0001;
constexpr const char* kDiagnosticBusy =
    "/Equipment/VME/Commands/GlobalBusyDiagnosticBusy";
constexpr INT kNoDiagnosticRequest = -1;
MVME_INTERFACE* bridge = nullptr;
bool physics_readout_allowed = false;

bool put(const char* path, const void* value, INT size, DWORD type) {
  HNDLE db = 0;
  cm_get_experiment_database(&db, nullptr);
  const INT rc = db_set_value(db, 0, path, value, size, 1, type);
  if (rc != DB_SUCCESS) cm_msg(MERROR, "global_busy", "ODB write %s: %d", path, rc);
  return rc == DB_SUCCESS;
}
bool get(const char* path, void* value, INT size, DWORD type) {
  HNDLE db = 0;
  cm_get_experiment_database(&db, nullptr);
  INT actual = size;
  return db_get_value(db, 0, path, value, &actual, type, FALSE) == DB_SUCCESS && actual == size;
}
bool status(const char* value) {
  return put("/Equipment/VME/Status/GlobalBusy", value,
             static_cast<INT>(std::strlen(value) + 1), TID_STRING);
}
bool client_connected(const char* name) {
  HNDLE db = 0, clients = 0, child = 0;
  cm_get_experiment_database(&db, nullptr);
  if (db_find_key(db, 0, "/System/Clients", &clients) != DB_SUCCESS) return false;
  for (INT i = 0; db_enum_key(db, clients, i, &child) == DB_SUCCESS; ++i) {
    const std::string path = db_get_path(db, child) + "/Name";
    char client_name[128] = {};
    INT size = sizeof(client_name);
    if (db_get_value(db, 0, path.c_str(), client_name, &size, TID_STRING, FALSE) == DB_SUCCESS &&
        std::strcmp(client_name, name) == 0) return true;
  }
  return false;
}
struct RunParticipants {
  bool vme;
  bool easiroc;
};
bool load_run_participants(INT run, RunParticipants* participants) {
  BOOL valid = FALSE, vme = FALSE, easiroc = FALSE;
  INT number = 0;
  if (!get("/DAQ/Status/Run/ParticipationValid", &valid, sizeof(valid), TID_BOOL) ||
      !get("/DAQ/Status/Run/ParticipationRunNumber", &number, sizeof(number), TID_INT) ||
      !get("/DAQ/Status/Run/VMEParticipating", &vme, sizeof(vme), TID_BOOL) ||
      !get("/DAQ/Status/Run/EASIROCParticipating", &easiroc, sizeof(easiroc), TID_BOOL) ||
      !valid || number != run)
    return false;
  participants->vme = vme != FALSE;
  participants->easiroc = easiroc != FALSE;
  return true;
}
bool ready(const char* client, const char* base, bool participating, INT run) {
  if (!participating) return ready_for_run(false, {}, run);
  char path[256];
  BOOL is_ready = FALSE;
  INT number = 0;
  const bool connected = client_connected(client);
  if (!connected) return ready_for_run(true, {}, run);
  std::snprintf(path, sizeof(path), "%s/DAQReady", base);
  if (!get(path, &is_ready, sizeof(is_ready), TID_BOOL)) return false;
  std::snprintf(path, sizeof(path), "%s/ReadyRunNumber", base);
  if (!get(path, &number, sizeof(number), TID_INT)) return false;
  return ready_for_run(true, {connected, is_ready != FALSE, number}, run);
}
}

bool initialize() {
  HNDLE db = 0, key = 0;
  INT effective = kProvisionalSetMeansBusy;
  cm_get_experiment_database(&db, nullptr);
  const INT found = db_find_key(db, 0, kPolarity, &key);
  if (found == DB_NO_KEY) {
    if (!put(kPolarity, &kProvisionalSetMeansBusy,
             sizeof(kProvisionalSetMeansBusy), TID_INT)) return false;
  } else if (found == DB_SUCCESS) {
    INT current = -1;
    if (!get(kPolarity, &current, sizeof(current), TID_INT)) return false;
    if (current == -1) {
      if (!put(kPolarity, &kProvisionalSetMeansBusy,
               sizeof(kProvisionalSetMeansBusy), TID_INT)) return false;
    } else if (current != 0 && current != 1) {
      cm_msg(MERROR, "global_busy", "Invalid GlobalBusySetMeansBusy=%d", current);
      return false;
    } else {
      effective = current;
    }
  } else {
    cm_msg(MERROR, "global_busy", "ODB lookup %s failed: %d", kPolarity, found);
    return false;
  }
  // Discard any unhandled request from an earlier frontend process.
  if (!put(kDiagnosticBusy, &kNoDiagnosticRequest,
           sizeof(kNoDiagnosticRequest), TID_INT)) return false;
  cm_msg(MINFO, "global_busy", "GlobalBusySetMeansBusy=%d (%s=BUSY ON)",
         effective, effective == 1 ? "SET" : "CLEAR");
  return publish_ready(true, false, 0) && status("UNKNOWN");
}
void attach(MVME_INTERFACE* vme) { bridge = vme; }

bool readout_allowed() { return physics_readout_allowed; }
void disable_readout() { physics_readout_allowed = false; }

bool publish_ready(bool participates, bool is_ready, INT run) {
  const BOOL p = participates ? TRUE : FALSE, r = is_ready ? TRUE : FALSE;
  return put("/Equipment/VME/Status/ParticipatesInGlobalBusy", &p, sizeof(p), TID_BOOL) &&
         put("/Equipment/VME/Status/DAQReady", &r, sizeof(r), TID_BOOL) &&
         put("/Equipment/VME/Status/ReadyRunNumber", &run, sizeof(run), TID_INT);
}

bool set_global_busy(bool busy) {
  INT set_means_busy = -1;
  if (!bridge || !get(kPolarity, &set_means_busy, sizeof(set_means_busy), TID_INT) ||
      (set_means_busy != 0 && set_means_busy != 1)) {
    cm_msg(MERROR, "global_busy", "OUT0 polarity unconfigured or V3718 unavailable");
    status("UNKNOWN");
    return false;
  }
  const CVErrorCodes conf = CAENVME_SetOutputConf(bridge->handle, cvOutput0,
                                                   cvDirect, cvActiveHigh, cvManualSW);
  if (conf != cvSuccess) {
    cm_msg(MERROR, "global_busy", "OUT0 configuration failed: %d (%s)", conf,
           CAENVME_DecodeError(conf));
    status("UNKNOWN");
    return false;
  }
  unsigned int mux = 0;
  const CVErrorCodes readback = CAENVME_ReadRegister(
      bridge->handle, static_cast<CVRegisters>(0x09), &mux);
  if (readback != cvSuccess || (mux & 0xFu) != 9u) {
    cm_msg(MERROR, "global_busy", "OUT0 mux readback failed: rc=%d raw=0x%04X",
           readback, mux & 0xFFFFu);
    status("UNKNOWN");
    return false;
  }
  const bool set = busy == (set_means_busy == 1);
  const CVErrorCodes rc = set
      ? CAENVME_SetOutputRegister(bridge->handle, kV3718Out0StatusBit)
      : CAENVME_ClearOutputRegister(bridge->handle, kV3718Out0StatusBit);
  if (rc != cvSuccess) {
    cm_msg(MERROR, "global_busy", "OUT0 level write failed: %d (%s)", rc,
           CAENVME_DecodeError(rc));
    status("UNKNOWN");
    return false;
  }
  unsigned int software_set = 0, io_status = 0;
  const CVErrorCodes set_readback = CAENVME_ReadRegister(
      bridge->handle, static_cast<CVRegisters>(0x0C), &software_set);
  const CVErrorCodes io_readback = CAENVME_ReadRegister(
      bridge->handle, static_cast<CVRegisters>(0x0B), &io_status);
  if (set_readback != cvSuccess || io_readback != cvSuccess ||
      ((software_set & kV3718Out0StatusBit) != 0) != set) {
    cm_msg(MERROR, "global_busy",
           "OUT0 level readback failed: set_rc=%d io_rc=%d set=0x%04X io=0x%04X",
           set_readback, io_readback, software_set & 0xFFFFu, io_status & 0xFFFFu);
    status("UNKNOWN");
    return false;
  }
  cm_msg(MINFO, "global_busy",
         "OUT0 software control (mux=9), BUSY %s via %s; set=0x%04X io=0x%04X",
         busy ? "ON" : "OFF", set ? "SET" : "CLEAR",
         software_set & 0xFFFFu, io_status & 0xFFFFu);
  return status(busy ? "ON" : "OFF");
}

void process_diagnostic_request(bool run_stopped) {
  static DWORD last_check = 0;
  const DWORD now = ss_millitime();
  if (last_check != 0 && static_cast<DWORD>(now - last_check) < 100) return;
  last_check = now;
  INT requested = kNoDiagnosticRequest;
  if (!get(kDiagnosticBusy, &requested, sizeof(requested), TID_INT) ||
      requested == kNoDiagnosticRequest) return;
  // Acknowledge before touching OUT0 so a failed ODB write cannot replay it.
  if (!put(kDiagnosticBusy, &kNoDiagnosticRequest,
           sizeof(kNoDiagnosticRequest), TID_INT)) return;
  if (requested != 0 && requested != 1) {
    cm_msg(MERROR, "global_busy", "Rejected diagnostic BUSY value %d", requested);
    return;
  }
  if (!run_stopped) {
    cm_msg(MERROR, "global_busy", "Rejected diagnostic BUSY request outside STOPPED");
    return;
  }
  const bool busy = requested == 1;
  if (set_global_busy(busy))
    cm_msg(MINFO, "global_busy", "Diagnostic Global BUSY %s applied to OUT0",
           busy ? "ON" : "OFF");
}

INT before_start(INT, char* error) {
  disable_readout();
  cm_msg(MINFO, "global_busy", "START 400 enter");
  const bool reset = publish_ready(true, false, 0);
  cm_msg(MINFO, "global_busy", "START 400 DAQReady reset returned %s",
         reset ? "OK" : "ERROR");
  const bool asserted = set_global_busy(true);
  cm_msg(MINFO, "global_busy", "START 400 BUSY ON returned %s",
         asserted ? "OK" : "ERROR");
  if (reset && asserted) {
    cm_msg(MINFO, "global_busy", "START 400 exit SUCCESS");
    return SUCCESS;
  }
  std::snprintf(error, 256, "%s", asserted ? "Cannot reset VME DAQReady before BOR"
                                               : "Cannot assert Global BUSY before BOR");
  cm_msg(MINFO, "global_busy", "START 400 exit ERROR: %s", error);
  return asserted ? FE_ERR_ODB : FE_ERR_HW;
}
INT after_start(INT run, char* error) {
  cm_msg(MINFO, "global_busy", "START 600 enter run %d", run);
  RunParticipants participants = {};
  if (!load_run_participants(run, &participants)) {
    std::snprintf(error, 256, "No valid Global BUSY participants for run %d", run);
    cm_msg(MERROR, "global_busy", "%s", error);
    set_global_busy(true);
    return FE_ERR_ODB;
  }
  const bool vme_ready = participants.vme &&
      ready("fevme", kStatus, true, run);
  cm_msg(MINFO, "global_busy", "START 600 fevme participant=%s Ready=%s",
         participants.vme ? "yes" : "no", vme_ready ? "yes" : "no");
  const bool easiroc_ready = ready("feeasiroc", "/Equipment/EASIROC/Status",
                                   participants.easiroc, run);
  if (participants.easiroc)
    cm_msg(MINFO, "global_busy", "START 600 feeasiroc participant=yes Ready=%s",
           easiroc_ready ? "yes" : "no");
  else
    cm_msg(MINFO, "global_busy", "START 600 feeasiroc participant=no (ignored)");
  if (!vme_ready || !easiroc_ready) {
    std::snprintf(error, 256, "DAQ frontend disconnected or not Ready for run %d", run);
    cm_msg(MERROR, "global_busy", "%s", error);
    set_global_busy(true);
    cm_msg(MINFO, "global_busy", "START 600 exit ERROR (not Ready)");
    return FE_ERR_ODB;
  }
  cm_msg(MINFO, "global_busy", "START 600 releasing BUSY");
  if (!set_global_busy(false)) {
    std::snprintf(error, 256, "Cannot release Global BUSY for run %d", run);
    set_global_busy(true);
    cm_msg(MINFO, "global_busy", "START 600 exit ERROR (BUSY release)");
    return FE_ERR_HW;
  }
  physics_readout_allowed = true;
  cm_msg(MINFO, "global_busy", "START 600 exit SUCCESS");
  return SUCCESS;
}
INT before_stop(INT, char* error) {
  disable_readout();
  if (!set_global_busy(true))
    cm_msg(MERROR, "global_busy", "Cannot assert Global BUSY before EOR");
  if (error) error[0] = '\0';
  return SUCCESS;
}
INT start_abort(INT, char*) {
  disable_readout();
  publish_ready(true, false, 0);
  // Do not prevent the later VME STARTABORT callback from stopping readout
  // when OUT0 cannot be controlled. set_global_busy() records UNKNOWN.
  set_global_busy(true);
  return SUCCESS;
}
}
