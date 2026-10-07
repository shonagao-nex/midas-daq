#include "global_busy.h"

#include <CAENVMElib.h>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <string>

namespace {
bool daq_ready = false;
bool busy_set = false;
unsigned set_calls = 0;
unsigned clear_calls = 0;
constexpr INT kRun = 42;

template <typename T>
INT copy_value(void* output, INT* size, const T& value) {
  if (*size < static_cast<INT>(sizeof(value))) return DB_TYPE_MISMATCH;
  std::memcpy(output, &value, sizeof(value));
  *size = sizeof(value);
  return DB_SUCCESS;
}
}

INT cm_get_experiment_database(HNDLE* db, HNDLE*) {
  *db = 1;
  return CM_SUCCESS;
}
INT cm_msg(INT, const char*, INT, const char*, const char*, ...) {
  return CM_SUCCESS;
}
DWORD ss_millitime(void) { return 0; }
INT db_find_key(HNDLE, HNDLE, const char* path, HNDLE* key) {
  if (std::strcmp(path, "/System/Clients") != 0) return DB_NO_KEY;
  *key = 1;
  return DB_SUCCESS;
}
INT db_enum_key(HNDLE, HNDLE, INT index, HNDLE* child) {
  if (index != 0) return DB_NO_MORE_SUBKEYS;
  *child = 2;
  return DB_SUCCESS;
}
std::string db_get_path(HNDLE, HNDLE) { return "/System/Clients/fevme"; }
INT db_get_value(HNDLE, HNDLE, const char* path, void* value,
                             INT* size, DWORD, BOOL) {
  if (std::strcmp(path, "/DAQ/Status/Run/ParticipationValid") == 0 ||
      std::strcmp(path, "/DAQ/Status/Run/VMEParticipating") == 0) {
    const BOOL yes = TRUE;
    return copy_value(value, size, yes);
  }
  if (std::strcmp(path, "/DAQ/Status/Run/EASIROCParticipating") == 0 ||
      std::strcmp(path, "/Equipment/VME/Status/DAQReady") == 0) {
    const BOOL flag = std::strstr(path, "DAQReady") ? daq_ready : FALSE;
    return copy_value(value, size, flag);
  }
  if (std::strcmp(path, "/DAQ/Status/Run/ParticipationRunNumber") == 0 ||
      std::strcmp(path, "/Equipment/VME/Status/ReadyRunNumber") == 0) {
    const INT run = kRun;
    return copy_value(value, size, run);
  }
  if (std::strcmp(path, "/Equipment/VME/Settings/GlobalBusySetMeansBusy") == 0) {
    const INT polarity = 1;
    return copy_value(value, size, polarity);
  }
  if (std::strcmp(path, "/System/Clients/fevme/Name") == 0) {
    const char name[] = "fevme";
    if (*size < static_cast<INT>(sizeof(name))) return DB_TYPE_MISMATCH;
    std::memcpy(value, name, sizeof(name));
    *size = sizeof(name);
    return DB_SUCCESS;
  }
  return DB_NO_KEY;
}
INT db_set_value(HNDLE, HNDLE, const char* path, const void* value,
                             INT, INT, DWORD) {
  if (std::strcmp(path, "/Equipment/VME/Status/DAQReady") == 0)
    daq_ready = *static_cast<const BOOL*>(value) != FALSE;
  return DB_SUCCESS;
}

extern "C" const char* CAENVME_DecodeError(CVErrorCodes) { return "mock"; }
extern "C" CVErrorCodes CAENVME_SetOutputConf(int32_t, CVOutputSelect,
                                                CVIOPolarity, CVLEDPolarity,
                                                CVIOSources) { return cvSuccess; }
extern "C" CVErrorCodes CAENVME_ReadRegister(int32_t, CVRegisters reg,
                                               unsigned int* value) {
  const unsigned address = static_cast<unsigned>(reg);
  *value = address == 0x09 ? 9u : (busy_set ? 1u : 0u);
  return cvSuccess;
}
extern "C" CVErrorCodes CAENVME_SetOutputRegister(int32_t, unsigned short) {
  busy_set = true;
  ++set_calls;
  return cvSuccess;
}
extern "C" CVErrorCodes CAENVME_ClearOutputRegister(int32_t, unsigned short) {
  busy_set = false;
  ++clear_calls;
  return cvSuccess;
}

int main() {
  MVME_INTERFACE bridge = {};
  bridge.handle = 1;
  global_busy::attach(&bridge);
  char error[256] = {};

  assert(global_busy::before_start(kRun, error) == SUCCESS);
  assert(busy_set && !daq_ready && clear_calls == 0);
  // A failed BOR leaves DAQReady false: START 600 must never release OUT0.
  assert(global_busy::after_start(kRun, error) == FE_ERR_ODB);
  assert(busy_set && !daq_ready && clear_calls == 0 && set_calls >= 2);
  assert(!global_busy::readout_allowed());

  daq_ready = true;
  assert(global_busy::after_start(kRun, error) == SUCCESS);
  assert(!busy_set && clear_calls == 1 && global_busy::readout_allowed());
  global_busy::attach(nullptr);
}
