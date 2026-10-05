#ifndef MODULE_STATUS_SNAPSHOT_H
#define MODULE_STATUS_SNAPSHOT_H

#include "midas.h"
#include "mvmestd.h"

namespace module_status_snapshot {

using Read16 = bool (*)(MVME_INTERFACE *, DWORD, WORD &, const char *);
using Read32 = bool (*)(MVME_INTERFACE *, DWORD, DWORD &, const char *);
using ReadCounter = void (*)(MVME_INTERFACE *, DWORD, DWORD *);

struct V7xxSnapshot {
  WORD status1 = 0;
  WORD status2 = 0;
  DWORD counter = 0;
};

struct V1190Snapshot {
  WORD status = 0;
  WORD stored = 0;
  DWORD counter = 0;
};

// Read two V7xx status registers, then call the existing counter reader.
bool read_v7xx(MVME_INTERFACE *vme, DWORD base, DWORD status1_offset, DWORD status2_offset,
                const char *status1_label, const char *status2_label, Read16 read16,
                ReadCounter read_counter, V7xxSnapshot &snapshot);

// Read V1190 status, stored count, and event counter in their established order.
bool read_v1190(MVME_INTERFACE *vme, DWORD base, DWORD status_offset, DWORD stored_offset, DWORD counter_offset,
                 const char *status_label, const char *stored_label, const char *counter_label,
                 Read16 read16, Read32 read32, V1190Snapshot &snapshot);

}

#endif
