#ifndef V1190_H
#define V1190_H

#include "midas.h"
#include "mvmestd.h"
#include <cstddef>

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

namespace v1190_micro {

struct Access {
  MVME_INTERFACE *vme;
  DWORD base;
  const char *log_source;
  bool (*read16)(MVME_INTERFACE *, DWORD, WORD &, const char *);
  bool (*write16)(MVME_INTERFACE *, DWORD, WORD, const char *);
};

// Wait for the V1190 micro-controller handshake bit.
bool wait(const Access &access, WORD ready_bit, const char *description);
// Write one opcode after the micro-controller becomes ready.
bool write_opcode(const Access &access, WORD opcode);
// Write an opcode and its operands in order.
bool write_command(const Access &access, WORD opcode, const WORD *operands, size_t count);
// Read a response after writing its opcode.
bool read_command(const Access &access, WORD opcode, WORD *words, size_t count);

}

#endif
