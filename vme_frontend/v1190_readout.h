#ifndef V1190_READOUT_H
#define V1190_READOUT_H

#include "midas.h"
#include "mvmestd.h"
#include "v1190_fifo_blt32.h"
#include <cstddef>

namespace v1190_readout {

constexpr size_t kMaxEventWords = 4096;

struct Access {
  MVME_INTERFACE *vme;
  DWORD base;
  const char *log_source;
  decltype(&mvme_get_dmode) get_dmode;
  decltype(&mvme_set_dmode) set_dmode;
  decltype(&mvme_read) read;
  bool (*read16)(MVME_INTERFACE *, DWORD, WORD &, const char *);
  bool (*read32)(MVME_INTERFACE *, DWORD, DWORD &, const char *);
  int (*blt_read)(void *, uint32_t, void *, int, int *);
  void *blt_context;
};

struct EventInfo {
  size_t words = 0;
  DWORD event_counter = 0;
  unsigned trailer_word_count = 0;
  bool valid = false;
  bool trailer_consumed = false;
};

struct BltResult {
  EventInfo event;
  V1190_FIFO_BLT_STATUS status = V1190_FIFO_BLT_BAD_ARGUMENT;
  V1190_FIFO_BLT_RESULT details = {};
  bool stop_required = false;
};

// Read one bounded D32 event through its Global Trailer.
EventInfo read_single_event(const Access &access, DWORD (&data)[kMaxEventWords]);
// Connect checked VME reads and the shared BLT transfer to the existing FIFO reader.
BltResult read_fifo_blt32_event(const Access &access, DWORD (&data)[kMaxEventWords], bool strict_sync_check,
                                V1190_FIFO_BLT_STATE &state);

}

#endif
