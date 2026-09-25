#pragma once

#include "midas.h"
#include "mvmestd.h"

// Log one read-only snapshot before any frontend OUT0 configuration.
void log_v3718_out0_diagnostic(MVME_INTERFACE* vme);
