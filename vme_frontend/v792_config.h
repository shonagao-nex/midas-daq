#ifndef V792_CONFIG_H
#define V792_CONFIG_H

#include "vme_odb.h"
#include "mvmestd.h"
#include "vme/v792.h"

namespace v792_config {

struct Access {
  MVME_INTERFACE *vme;
  DWORD base;
  const char *log_source;
  bool (*read16)(MVME_INTERFACE *, DWORD, WORD &, const char *);
  bool (*write16)(MVME_INTERFACE *, DWORD, WORD, const char *);
  int (*read_thresholds)(MVME_INTERFACE *, DWORD, WORD *);
};

struct Readback {
  WORD iped = 0;
  WORD bits = 0;
  WORD firmware = 0;
  WORD thresholds[V792_MAX_CHANNELS] = {};
  BOOL zero_suppression = FALSE;
  BOOL all_trigger = FALSE;
};

enum class VerifyStatus { ReadFailure, Matched, Mismatch };

// Apply the V792 run settings in their established register order.
bool configure_for_run(const Access &access, const V792Settings &settings);
// Read and verify V792 settings without publishing frontend state.
VerifyStatus verify_configuration(const Access &access, const V792Settings &settings, Readback &result);
// Clear the V792 data buffer using the established set/clear sequence.
bool clear_data(const Access &access, bool manual);

}

#endif
