#ifndef V1190_H
#define V1190_H

#include "midas.h"
#include "mvmestd.h"

namespace v1190_clear {

struct Access {
  MVME_INTERFACE *vme;
  DWORD base;
  bool (*read16)(MVME_INTERFACE *, DWORD, WORD &, const char *);
  bool (*write16)(MVME_INTERFACE *, DWORD, WORD, const char *);
};

enum class CheckResult { Empty, ReadFailed, DataReady };

// Issue the V1190 Software Clear with the existing D16 access callback.
bool software_clear(const Access &access, const char *description);
// Read Status after clear and report whether DataReady remains asserted.
CheckResult verify_clear(const Access &access, const char *description);

}

#endif
