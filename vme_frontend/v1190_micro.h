#ifndef V1190_MICRO_H
#define V1190_MICRO_H

#include "midas.h"
#include "mvmestd.h"
#include <cstddef>

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
// Read the V1190 acquisition mode.
bool read_acquisition_mode(const Access &access, WORD &mode);

}

#endif
