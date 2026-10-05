#include "../v1190.h"
#include "../v1190_config.h"
#include <cassert>
#include <cstdio>
#include <vector>

namespace {

constexpr DWORD kBase = 0x00C10000;
std::vector<DWORD> reads;
std::vector<DWORD> writes;
std::vector<WORD> values;
size_t next_value = 0;
bool fail_read = false;
bool fail_write = false;
bool never_ready = false;

bool read16(MVME_INTERFACE *vme, DWORD address, WORD &value, const char *)
{
  assert(vme == reinterpret_cast<MVME_INTERFACE *>(1));
  reads.push_back(address);
  if (fail_read) return false;
  value = address == kBase + 0x1030 ? (never_ready ? 0x0000 : 0x0003) : values.at(next_value++);
  return true;
}

bool write16(MVME_INTERFACE *vme, DWORD address, WORD value, const char *)
{
  assert(vme == reinterpret_cast<MVME_INTERFACE *>(1));
  assert(address == kBase + 0x102E);
  writes.push_back(value);
  return !fail_write;
}

void reset()
{
  reads.clear();
  writes.clear();
  values.clear();
  next_value = 0;
  fail_read = false;
  fail_write = false;
  never_ready = false;
}

}

int main()
{
  const v1190_micro::Access access = {
      reinterpret_cast<MVME_INTERFACE *>(1), kBase, "test_v1190_micro_mock", read16, write16};

  reset();
  const WORD operands[] = {0x1234, 0x5678};
  assert(v1190_micro::write_command(access, 0x4400, operands, 2));
  assert((writes == std::vector<DWORD>{0x4400, 0x1234, 0x5678}));
  assert(reads.size() == 3);
  for (DWORD address : reads) assert(address == kBase + 0x1030);

  reset();
  values = {0x0001, 0x0002};
  WORD result[2] = {};
  assert(v1190_micro::read_command(access, 0x4500, result, 2));
  assert((writes == std::vector<DWORD>{0x4500}));
  assert(result[0] == 0x0001 && result[1] == 0x0002);
  assert((reads == std::vector<DWORD>{kBase + 0x1030, kBase + 0x1030, kBase + 0x102E,
                                      kBase + 0x1030, kBase + 0x102E}));

  reset();
  values = {0x0001};
  WORD mode = 0;
  assert(v1190_config::read_acquisition_mode(access, mode));
  assert(writes.size() == 1 && writes[0] == 0x0200 && mode == 0x0001);

  reset();
  fail_read = true;
  assert(!v1190_micro::write_opcode(access, 0x4400));
  assert(writes.empty() && reads.size() == 1);

  reset();
  fail_write = true;
  assert(!v1190_micro::write_opcode(access, 0x4400));
  assert(writes.size() == 1 && reads.size() == 1);

  reset();
  never_ready = true;
  assert(!v1190_micro::write_opcode(access, 0x4400));
  assert(reads.size() == 1000 && writes.empty());

  std::puts("test_v1190_micro_mock: passed");
}
