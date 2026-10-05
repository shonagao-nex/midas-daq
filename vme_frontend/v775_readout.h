#ifndef V775_READOUT_H
#define V775_READOUT_H

#include "midas.h"
#include "mvmestd.h"
#include <cstddef>

namespace v775_readout {

constexpr size_t kMaxEventWords = 64;

struct Access {
  MVME_INTERFACE *vme;
  DWORD base;
  const char *log_source;
  decltype(&mvme_get_dmode) get_dmode;
  decltype(&mvme_set_dmode) set_dmode;
  decltype(&mvme_read) read;
};

struct EventInfo {
  size_t words = 0;
  DWORD event_counter = 0;
  unsigned expected_measurements = 0;
  unsigned measurements = 0;
  unsigned geo = 0;
  bool valid = false;
  bool eob_consumed = false;
};

// Read and validate one V775 event through its EOB.
EventInfo read_single_event(const Access &access, DWORD (&data)[kMaxEventWords]);

}

#endif
