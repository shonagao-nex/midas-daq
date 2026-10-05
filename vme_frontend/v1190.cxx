#include "v1190.h"

namespace v1190_clear {

constexpr DWORD kSoftwareClear = 0x1016;
constexpr DWORD kStatus = 0x1002;
constexpr WORD kDataReady = 0x0001;

// Issue the V1190 Software Clear with the existing D16 access callback.
bool software_clear(const Access &access, const char *description)
{
  return access.write16(access.vme, access.base + kSoftwareClear, 0, description);
}

// Read Status after clear and report whether DataReady remains asserted.
CheckResult verify_clear(const Access &access, const char *description)
{
  WORD status = 0;
  if (!access.read16(access.vme, access.base + kStatus, status, description)) return CheckResult::ReadFailed;
  return status & kDataReady ? CheckResult::DataReady : CheckResult::Empty;
}

}

namespace v1190_micro {

static const DWORD MICRO_DATA = 0x102E;
static const DWORD MICRO_HANDSHAKE = 0x1030;
static const WORD MICRO_WRITE_OK = 0x0001;
static const WORD MICRO_READ_OK = 0x0002;
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

}
