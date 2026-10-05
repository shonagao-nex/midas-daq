#ifndef V792_READOUT_H
#define V792_READOUT_H

#include "midas.h"
#include "mvmestd.h"
#include "v792_blt32.h"
#include <cstddef>

namespace v792_readout {

constexpr size_t kMaxEventWords = 64;

struct Access {
  MVME_INTERFACE *vme;
  DWORD base;
  const char *log_source;
  decltype(&mvme_get_dmode) get_dmode;
  decltype(&mvme_set_dmode) set_dmode;
  decltype(&mvme_read) read;
  int (*blt_read)(void *, uint32_t, void *, int, int *);
  void *blt_context;
};

struct EventInfo {
  size_t words = 0;
  DWORD event_counter = 0;
  unsigned expected_measurements = 0;
  unsigned measurements = 0;
  unsigned geo = 0;
  bool valid = false;
  bool footer_consumed = false;
};

struct BltResult {
  EventInfo event;
  V792_BLT_STATUS status = V792_BLT_BAD_ARGUMENT;
  V792_BLT_RESULT details = {};
  int restore_status = MVME_SUCCESS;
  bool stop_required = false;
};

// Read one V792 event with bounded D32 cycles.
EventInfo read_single_event(const Access &access, DWORD (&data)[kMaxEventWords]);
// Read one V792 event through the existing BLT reader.
BltResult read_blt32_event(const Access &access, DWORD (&data)[kMaxEventWords]);

}

#endif
