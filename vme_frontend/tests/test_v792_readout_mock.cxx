#include "../v792_readout.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace {

constexpr DWORD kBase = 0x00600000;
constexpr DWORD kGeo = 3u << 27;
std::vector<DWORD> input;
size_t next_word = 0;
std::vector<int> modes;
int mode = MVME_DMODE_D16;
int fail_read_at = -1;
bool fail_get_mode = false;
bool fail_select_mode = false;
bool fail_restore_mode = false;
int blt_status = 0;
int blt_actual_override = -1;
int blt_calls = 0;

DWORD header(unsigned count)
{
  return kGeo | (2u << 24) | (count << 8);
}

DWORD footer(DWORD counter)
{
  return kGeo | (4u << 24) | counter;
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
  blt_status = 0;
  blt_actual_override = -1;
  blt_calls = 0;
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

int blt_read(void *context, uint32_t address, void *destination, int requested_bytes, int *actual_bytes)
{
  assert(context == &input && address == kBase && mode == MVME_DMODE_D32);
  ++blt_calls;
  const int actual = blt_actual_override >= 0 ? blt_actual_override : requested_bytes;
  assert(actual >= 0 && actual <= requested_bytes && actual % sizeof(DWORD) == 0);
  assert(next_word + static_cast<size_t>(actual / sizeof(DWORD)) <= input.size());
  std::memcpy(destination, input.data() + next_word, static_cast<size_t>(actual));
  next_word += static_cast<size_t>(actual / sizeof(DWORD));
  *actual_bytes = actual;
  return blt_status;
}

}

int main()
{
  const v792_readout::Access access = {
      reinterpret_cast<MVME_INTERFACE *>(1), kBase, "test_v792_readout_mock",
      get_dmode, set_dmode, read, blt_read, &input};
  DWORD data[v792_readout::kMaxEventWords] = {};

  reset({header(1), kGeo | 0x1234, footer(0x123456), header(0)});
  auto event = v792_readout::read_single_event(access, data);
  assert(event.valid && event.footer_consumed && event.words == 3);
  assert(event.event_counter == 0x123456 && event.expected_measurements == 1 && event.measurements == 1);
  assert(event.geo == 3 && data[0] == header(1) && data[2] == footer(0x123456));
  assert(next_word == 3 && (modes == std::vector<int>{MVME_DMODE_D32, MVME_DMODE_D16}));

  reset({footer(1)});
  event = v792_readout::read_single_event(access, data);
  assert(!event.valid && !event.footer_consumed && next_word == 1 && mode == MVME_DMODE_D16);

  reset({header(1), footer(1)});
  event = v792_readout::read_single_event(access, data);
  assert(!event.valid && next_word == 2);

  reset({header(0), (4u << 27) | (4u << 24)});
  event = v792_readout::read_single_event(access, data);
  assert(!event.valid && next_word == 2);

  reset({header(1), kGeo | (3u << 24)});
  event = v792_readout::read_single_event(access, data);
  assert(!event.valid && next_word == 2);

  reset({header(33), footer(1)});
  event = v792_readout::read_single_event(access, data);
  assert(!event.valid && event.expected_measurements == 33 && next_word == 1);

  std::vector<DWORD> long_event(v792_readout::kMaxEventWords + 1, kGeo | 0x1234);
  long_event[0] = header(32);
  reset(long_event);
  event = v792_readout::read_single_event(access, data);
  assert(!event.valid && next_word == 34 && next_word < v792_readout::kMaxEventWords);

  reset({header(0), footer(1)});
  fail_read_at = 1;
  event = v792_readout::read_single_event(access, data);
  assert(!event.valid && next_word == 1 && mode == MVME_DMODE_D16);

  reset({header(0), footer(1)});
  fail_select_mode = true;
  event = v792_readout::read_single_event(access, data);
  assert(!event.valid && next_word == 0 && (modes == std::vector<int>{MVME_DMODE_D32, MVME_DMODE_D16}));

  reset({header(0), footer(1)});
  fail_restore_mode = true;
  event = v792_readout::read_single_event(access, data);
  assert(!event.valid && event.words == 0 && event.footer_consumed && next_word == 2);

  reset({header(1), kGeo | 0x1234, footer(0x654321), header(0)});
  auto blt = v792_readout::read_blt32_event(access, data);
  assert(blt.event.valid && blt.event.words == 3 && blt.event.event_counter == 0x654321);
  assert(blt.status == V792_BLT_OK && !blt.stop_required && blt_calls == 1 && next_word == 3);
  assert(blt.details.requested_bytes == 8 && blt.details.actual_bytes == 8 && mode == MVME_DMODE_D16);

  reset({header(1), kGeo | (3u << 24), footer(1)});
  blt = v792_readout::read_blt32_event(access, data);
  assert(!blt.event.valid && blt.status == V792_BLT_INVALID_EVENT && blt.stop_required && next_word == 3);

  reset({header(1), kGeo | 0x1234, footer(1)});
  blt_actual_override = 4;
  blt = v792_readout::read_blt32_event(access, data);
  assert(!blt.event.valid && blt.status == V792_BLT_SHORT_TRANSFER && blt.stop_required);
  assert(blt.details.requested_bytes == 8 && blt.details.actual_bytes == 4);

  reset({header(1), kGeo | 0x1234, footer(1)});
  blt_status = -1;
  blt = v792_readout::read_blt32_event(access, data);
  assert(!blt.event.valid && blt.status == V792_BLT_TRANSFER_ERROR && blt.stop_required);

  reset({header(33), footer(1)});
  blt = v792_readout::read_blt32_event(access, data);
  assert(!blt.event.valid && blt.status == V792_BLT_INVALID_HEADER && blt.stop_required && blt_calls == 0);

  reset({header(0), footer(1)});
  fail_read_at = 0;
  blt = v792_readout::read_blt32_event(access, data);
  assert(!blt.event.valid && blt.status == V792_BLT_HEADER_READ_ERROR && blt.stop_required);

  reset({header(0), footer(1)});
  fail_restore_mode = true;
  blt = v792_readout::read_blt32_event(access, data);
  assert(!blt.event.valid && blt.status == V792_BLT_OK && blt.stop_required && blt.restore_status != MVME_SUCCESS);

  reset({header(0), footer(1)});
  fail_get_mode = true;
  blt = v792_readout::read_blt32_event(access, data);
  assert(!blt.event.valid && !blt.stop_required && modes.empty());

  std::puts("test_v792_readout_mock: passed");
}
