#pragma once

#include "midas.h"
#include "mvmestd.h"

namespace global_busy {
bool initialize();
void attach(MVME_INTERFACE* vme);
bool set_global_busy(bool busy);
void process_diagnostic_request(bool run_stopped);
bool publish_ready(bool participates, bool ready, INT run_number);
// Physics readout is permitted only after START 600 releases Global BUSY.
bool readout_allowed();
void disable_readout();
INT before_start(INT run_number, char* error);
INT after_start(INT run_number, char* error);
INT before_stop(INT run_number, char* error);
INT start_abort(INT run_number, char* error);
}
