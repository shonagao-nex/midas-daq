#ifndef DAQ_MONITOR_MONITOR_ODB_UTILS_H
#define DAQ_MONITOR_MONITOR_ODB_UTILS_H

#include "midas.h"
#include "status_policy.h"

#include <string>

namespace daq_monitor {

//************************************//
// Read one existing ODB scalar without changing its destination on failure
//************************************//
template <typename T>
bool read_value(HNDLE database, const char* path, DWORD type, T* value) {
  T candidate{};
  INT size = sizeof(candidate);
  const INT status =
      db_get_value(database, 0, path, &candidate, &size, type, FALSE);
  if (status != DB_SUCCESS || size != static_cast<INT>(sizeof(candidate)))
    return false;
  *value = candidate;
  return true;
}

bool read_string(HNDLE database, const std::string& path,
                 std::string* value);
RunState policy_run_state(INT state);

}  // namespace daq_monitor

#endif  // DAQ_MONITOR_MONITOR_ODB_UTILS_H
