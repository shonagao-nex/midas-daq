#include "monitor_odb_utils.h"

#include <cstddef>

namespace daq_monitor {
namespace {

constexpr std::size_t kStringCapacity = 512;

}  // namespace

//************************************//
// Read one existing ODB string with the monitor's fixed buffer size
//************************************//
bool read_string(HNDLE database, const std::string& path,
                 std::string* value) {
  char buffer[kStringCapacity] = {};
  INT size = sizeof(buffer);
  const INT status = db_get_value(database, 0, path.c_str(), buffer, &size,
                                  TID_STRING, FALSE);
  if (status != DB_SUCCESS || size <= 0)
    return false;
  buffer[sizeof(buffer) - 1] = '\0';
  *value = buffer;
  return true;
}

//************************************//
// Convert a MIDAS run state to the monitor policy state
//************************************//
RunState policy_run_state(INT state) {
  switch (state) {
    case STATE_RUNNING:
      return RunState::kRunning;
    case STATE_STOPPED:
      return RunState::kStopped;
    case STATE_PAUSED:
    default:
      return RunState::kPausedOrTransition;
  }
}

}  // namespace daq_monitor
