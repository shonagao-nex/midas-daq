#pragma once

#include "alarm_policy.h"
#include "midas.h"

namespace daq_monitor {

//************************************//
// Hold component alarm transition state
//************************************//
struct MonitorAlarmState {
  AlarmRuntimeState vme_alarm;
  AlarmRuntimeState easiroc_alarm;
  AlarmRuntimeState logger_alarm;
  AlarmRuntimeState disk_alarm;
};

bool initialize_monitor_alarm_classes(HNDLE database);
bool validate_monitor_global_alarm_class();
bool update_monitor_alarms(MonitorAlarmState* state);

}  // namespace daq_monitor
