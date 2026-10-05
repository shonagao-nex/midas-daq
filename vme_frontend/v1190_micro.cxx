#include "v1190_micro.h"

namespace v1190_micro {

static const DWORD MICRO_DATA = 0x102E;
static const DWORD MICRO_HANDSHAKE = 0x1030;
static const WORD MICRO_WRITE_OK = 0x0001;
static const WORD MICRO_READ_OK = 0x0002;
static const WORD OPCODE_READ_ACQ_MODE = 0x0200;
static const unsigned MAX_POLLS = 1000;

// Wait for the requested micro-controller handshake bit.
bool wait(const Access &access, WORD ready_bit, const char *description)
{
  WORD handshake = 0;
  for (unsigned poll = 0; poll < MAX_POLLS; ++poll) {
    if (!access.read16(access.vme, access.base + MICRO_HANDSHAKE, handshake, "V1190 micro handshake")) return false;
    if (handshake & ready_bit) return true;
    ss_sleep(1);
  }
  cm_msg(MERROR, access.log_source, "V1190 micro %s timeout after %u polls (handshake 0x%04X)",
         description, MAX_POLLS, handshake);
  return false;
}

// Write an opcode once the micro-controller accepts writes.
bool write_opcode(const Access &access, WORD opcode)
{
  if (!wait(access, MICRO_WRITE_OK, "write-ready")) return false;
  return access.write16(access.vme, access.base + MICRO_DATA, opcode, "V1190 micro opcode");
}

// Write an opcode followed by each operand after its handshake.
bool write_command(const Access &access, WORD opcode, const WORD *operands, size_t count)
{
  if (!write_opcode(access, opcode)) return false;
  for (size_t i = 0; i < count; ++i) {
    if (!wait(access, MICRO_WRITE_OK, "operand write-ready") ||
        !access.write16(access.vme, access.base + MICRO_DATA, operands[i], "V1190 micro operand")) {
      cm_msg(MERROR, access.log_source, "V1190 opcode 0x%04X operand %zu/%zu write failed", opcode, i + 1, count);
      return false;
    }
  }
  return true;
}

// Read each response word after writing the opcode and checking readiness.
bool read_command(const Access &access, WORD opcode, WORD *words, size_t count)
{
  if (!write_opcode(access, opcode)) return false;
  for (size_t i = 0; i < count; ++i) {
    if (!wait(access, MICRO_READ_OK, "response read-ready") ||
        !access.read16(access.vme, access.base + MICRO_DATA, words[i], "V1190 micro response")) {
      cm_msg(MERROR, access.log_source, "V1190 opcode 0x%04X response word %zu/%zu read failed", opcode, i + 1, count);
      return false;
    }
  }
  return true;
}

// Read the acquisition mode with the existing V1190 opcode.
bool read_acquisition_mode(const Access &access, WORD &mode)
{
  return read_command(access, OPCODE_READ_ACQ_MODE, &mode, 1);
}

}
