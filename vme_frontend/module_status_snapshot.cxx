#include "module_status_snapshot.h"

namespace module_status_snapshot {

// Read two V7xx status registers, then call the existing counter reader.
bool read_v7xx(MVME_INTERFACE *vme, DWORD base, DWORD status1_offset, DWORD status2_offset,
                const char *status1_label, const char *status2_label, Read16 read16,
                ReadCounter read_counter, V7xxSnapshot &snapshot)
{
  if (!read16(vme, base + status1_offset, snapshot.status1, status1_label) ||
      !read16(vme, base + status2_offset, snapshot.status2, status2_label)) return false;
  read_counter(vme, base, &snapshot.counter);
  return true;
}

// Read V1190 status, stored count, and event counter in their established order.
bool read_v1190(MVME_INTERFACE *vme, DWORD base, DWORD status_offset, DWORD stored_offset, DWORD counter_offset,
                 const char *status_label, const char *stored_label, const char *counter_label,
                 Read16 read16, Read32 read32, V1190Snapshot &snapshot)
{
  return read16(vme, base + status_offset, snapshot.status, status_label) &&
         read16(vme, base + stored_offset, snapshot.stored, stored_label) &&
         read32(vme, base + counter_offset, snapshot.counter, counter_label);
}

}
