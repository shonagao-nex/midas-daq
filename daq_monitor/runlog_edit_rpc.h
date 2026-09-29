#ifndef DAQ_MONITOR_RUNLOG_EDIT_RPC_H
#define DAQ_MONITOR_RUNLOG_EDIT_RPC_H

#include "runlog_edit.h"

#include <cstdint>
#include <string>

namespace daq_monitor {

constexpr char kEditRunlogMetadataCommand[] = "edit_runlog_metadata";

// Called by the MIDAS RPC_JRPC_CXX callback. The arguments JSON is:
// {"run":64,"changes":{"Experiment":{"current":"old","value":"new"}}}
// Type and Comment use the same shape; current may be null if the field was
// absent. The returned JSON text goes in jrpc_cxx's result.reply string.
std::string handle_runlog_edit_rpc(const std::string& command,
                                   const std::string& arguments,
                                   const RunlogEditor& editor);

// Exposed separately to keep the browser-facing error contract testable.
std::string runlog_edit_rpc_response(const RunlogEditResult& result,
                                     std::int64_t run_number);

// Fail closed when ODB state cannot be read. A different completed run stays
// editable while the current run is RUNNING or in a transition.
bool runlog_edit_target_active(std::int64_t target_run,
                               std::int64_t current_run, int run_state,
                               int transition_in_progress, bool state_valid);

}  // namespace daq_monitor

#endif
