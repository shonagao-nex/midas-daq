#include "../v775_readout.h"
#include "../v775.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace {

constexpr DWORD kBase = 0x00000000;
constexpr DWORD kGeo = 3u << 27;
std::vector<DWORD> input;
size_t next_word = 0;
std::vector<int> modes;
int mode = MVME_DMODE_D16;
int fail_read_at = -1;
bool fail_get_mode = false;
bool fail_select_mode = false;
bool fail_restore_mode = false;

DWORD header(unsigned count)
{
  return kGeo | (V775_DATA_TYPE_HEADER << 24) | (count << 8);
}

DWORD eob(DWORD counter)
{
  return kGeo | (V775_DATA_TYPE_EOB << 24) | counter;
}

void reset(std::vector<DWORD> words)
{
  input = std::move(words);
  next_word = 0;
  modes.clear();
  mode = MVME_DMODE_D16;
  fail_read_at = -1;
  fail_get_mode = false;
  fail_select_mode = false;
  fail_restore_mode = false;
}

int get_dmode(MVME_INTERFACE *vme, int *value)
{
  assert(vme == reinterpret_cast<MVME_INTERFACE *>(1));
  if (fail_get_mode) return -1;
  *value = mode;
  return MVME_SUCCESS;
}

int set_dmode(MVME_INTERFACE *vme, int value)
{
  assert(vme == reinterpret_cast<MVME_INTERFACE *>(1));
  modes.push_back(value);
  if ((modes.size() == 1 && fail_select_mode) || (modes.size() == 2 && fail_restore_mode)) return -1;
  mode = value;
  return MVME_SUCCESS;
}

int read(MVME_INTERFACE *vme, void *destination, mvme_addr_t address, mvme_size_t bytes)
{
  assert(vme == reinterpret_cast<MVME_INTERFACE *>(1));
  assert(address == kBase && bytes == sizeof(DWORD) && mode == MVME_DMODE_D32);
  if (static_cast<int>(next_word) == fail_read_at || next_word >= input.size()) return -1;
  const DWORD word = input[next_word++];
  std::memcpy(destination, &word, sizeof(word));
  return MVME_SUCCESS;
}

}

int main()
{
  const v775_readout::Access access = {
      reinterpret_cast<MVME_INTERFACE *>(1), kBase, "test_v775_readout_mock", get_dmode, set_dmode, read};
  DWORD data[v775_readout::kMaxEventWords] = {};

  reset({header(1), kGeo | 0x1234, eob(0x123456), header(0)});
  auto event = v775_readout::read_single_event(access, data);
  assert(event.valid && event.eob_consumed && event.words == 3);
  assert(event.event_counter == 0x123456 && event.expected_measurements == 1 && event.measurements == 1);
  assert(event.geo == 3 && data[0] == header(1) && data[2] == eob(0x123456));
  assert(next_word == 3 && (modes == std::vector<int>{MVME_DMODE_D32, MVME_DMODE_D16}));

  reset({eob(1)});
  event = v775_readout::read_single_event(access, data);
  assert(!event.valid && !event.eob_consumed && next_word == 1 && mode == MVME_DMODE_D16);

  reset({header(1), eob(1)});
  event = v775_readout::read_single_event(access, data);
  assert(!event.valid && !event.eob_consumed && next_word == 2);

  reset({header(0), (4u << 27) | (V775_DATA_TYPE_EOB << 24)});
  event = v775_readout::read_single_event(access, data);
  assert(!event.valid && next_word == 2 && mode == MVME_DMODE_D16);

  reset({header(1), kGeo | (V775_DATA_TYPE_INVALID << 24)});
  event = v775_readout::read_single_event(access, data);
  assert(!event.valid && next_word == 2);

  reset({header(1), kGeo | (3u << 24)});
  event = v775_readout::read_single_event(access, data);
  assert(!event.valid && next_word == 2);

  reset({header(33), eob(1)});
  event = v775_readout::read_single_event(access, data);
  assert(!event.valid && event.expected_measurements == 33 && next_word == 1);

  std::vector<DWORD> long_event(v775_readout::kMaxEventWords + 1, kGeo | 0x1234);
  long_event[0] = header(32);
  reset(long_event);
  event = v775_readout::read_single_event(access, data);
  assert(!event.valid && next_word == 34 && next_word < v775_readout::kMaxEventWords && mode == MVME_DMODE_D16);

  reset({header(0), eob(1)});
  fail_read_at = 1;
  event = v775_readout::read_single_event(access, data);
  assert(!event.valid && next_word == 1 && mode == MVME_DMODE_D16);

  reset({header(0), eob(1)});
  fail_select_mode = true;
  event = v775_readout::read_single_event(access, data);
  assert(!event.valid && next_word == 0 && (modes == std::vector<int>{MVME_DMODE_D32, MVME_DMODE_D16}));

  reset({header(0), eob(1)});
  fail_restore_mode = true;
  event = v775_readout::read_single_event(access, data);
  assert(!event.valid && event.words == 0 && event.eob_consumed && next_word == 2);

  reset({header(0), eob(1)});
  fail_get_mode = true;
  event = v775_readout::read_single_event(access, data);
  assert(!event.valid && modes.empty() && next_word == 0);

  std::puts("test_v775_readout_mock: passed");
}
