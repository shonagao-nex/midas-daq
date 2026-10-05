#ifndef V792_BASIC_H
#define V792_BASIC_H

#include "mvmestd.h"
#include "vme/v792.h"

namespace v792_basic {

// Keep the checked Data Clear sequence outside the MIDAS driver.
inline bool clear_data(MVME_INTERFACE *vme, DWORD base,
                       bool (*write16)(MVME_INTERFACE *, DWORD, WORD, const char *),
                       bool manual)
{
  const char *set_desc = manual ? "V792 manual Data Clear set" : "V792 Data Clear set";
  const char *clear_desc = manual ? "V792 manual Data Clear clear" : "V792 Data Clear clear";
  return write16(vme, base + V792_BIT_SET2_RW, 0x0004, set_desc) &&
         write16(vme, base + V792_BIT_CLEAR2_WO, 0x0004, clear_desc);
}

}

#endif
