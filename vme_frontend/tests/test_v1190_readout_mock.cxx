#include "../v1190_readout.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
constexpr DWORD kBase = 0x00c10000;
MVME_INTERFACE *const kVme = reinterpret_cast<MVME_INTERFACE *>(1);
std::vector<DWORD> input;
size_t next_word;
int mode, fail_read_at, fail_set_mode;
std::vector<int> modes;
DWORD fifo_entry, fifo_data[10];
WORD fifo_status, stored_after;
int short_bytes, blt_status, blt_calls, fail_fifo16, fail_fifo32;
std::vector<char> order;
void reset()
{
  input.clear(); next_word = 0; mode = MVME_DMODE_D16; fail_read_at = -1; fail_set_mode = -1; modes.clear();
  fifo_status = 1; stored_after = 0; short_bytes = 0; blt_status = 0; blt_calls = 0;
  fail_fifo16 = fail_fifo32 = 0; order.clear();
  fifo_entry = (0x3456u << 16) | 10u;
  fifo_data[0] = 0x4000001fu | (0x123456u << 5);
  for (unsigned id = 0; id < 4; ++id) {
    fifo_data[1 + 2 * id] = 0x08000000u | (id << 24) | (0x123u << 12);
    fifo_data[2 + 2 * id] = 0x18000002u | (id << 24) | (0x123u << 12);
  }
  fifo_data[9] = 0x8000015fu;
}
int get_mode(MVME_INTERFACE *vme, int *value) { assert(vme == kVme); *value = mode; return MVME_SUCCESS; }
int set_mode(MVME_INTERFACE *vme, int value)
{
  assert(vme == kVme); modes.push_back(value);
  if (value == fail_set_mode) return -1;
  mode = value; return MVME_SUCCESS;
}
int read_word(MVME_INTERFACE *vme, void *value, mvme_addr_t address, mvme_size_t size)
{
  assert(vme == kVme && address == kBase && size == sizeof(DWORD) && mode == MVME_DMODE_D32);
  if (static_cast<int>(next_word) == fail_read_at) return -7;
  assert(next_word < input.size());
  *static_cast<DWORD *>(value) = input[next_word++];
  return MVME_SUCCESS;
}
bool read16(MVME_INTERFACE *vme, DWORD address, WORD &value, const char *description)
{
  assert(vme == kVme && description && std::strcmp(description, "V1190 Event FIFO D16 read") == 0);
  if (fail_fifo16) return false;
  if (address == kBase + V1190_FIFO_STATUS_OFFSET) { order.push_back('S'); value = fifo_status; }
  else { assert(address == kBase + V1190_FIFO_STORED_OFFSET); order.push_back('A'); value = stored_after; }
  return true;
}
bool read32(MVME_INTERFACE *vme, DWORD address, DWORD &value, const char *description)
{
  assert(vme == kVme && address == kBase + V1190_FIFO_ENTRY_OFFSET && description);
  if (fail_fifo32) return false;
  order.push_back('F'); value = fifo_entry; return true;
}
int blt(void *context, uint32_t address, void *destination, int requested, int *actual)
{
  assert(context == kVme && address == kBase && requested == 40);
  order.push_back('L'); ++blt_calls; *actual = requested - short_bytes;
  if (*actual > 0) std::memcpy(destination, fifo_data, *actual);
  return blt_status;
}
v1190_readout::Access access()
{
  return {kVme, kBase, "v1190_readout_mock", get_mode, set_mode, read_word, read16, read32, blt, kVme};
}
DWORD header(DWORD counter) { return 0x40000000u | (counter << 5); }
DWORD trailer(unsigned words) { return 0x80000000u | (words << 5); }
}
int main()
{
  DWORD data[v1190_readout::kMaxEventWords] = {};
  reset(); input = {header(42), 0, trailer(3)};
  auto event = v1190_readout::read_single_event(access(), data);
  assert(event.valid && event.trailer_consumed && event.words == 3 && event.event_counter == 42 && event.trailer_word_count == 3);
  assert(data[0] == header(42) && data[2] == trailer(3) && next_word == 3 && mode == MVME_DMODE_D16);
  assert((modes == std::vector<int>{MVME_DMODE_D32, MVME_DMODE_D16}));

  reset(); input = {0}; event = v1190_readout::read_single_event(access(), data);
  assert(!event.valid && next_word == 1 && mode == MVME_DMODE_D16);
  reset(); input = {header(42), trailer(4)}; event = v1190_readout::read_single_event(access(), data);
  assert(!event.valid && next_word == 2);
  for (DWORD word : {header(1), 0xc0000000u, 0x28000000u}) {
    reset(); input = {header(42), word}; event = v1190_readout::read_single_event(access(), data);
    assert(!event.valid && next_word == 2);
  }
  reset(); input.resize(v1190_readout::kMaxEventWords, 0); input[0] = header(42);
  event = v1190_readout::read_single_event(access(), data);
  assert(!event.valid && next_word == v1190_readout::kMaxEventWords && mode == MVME_DMODE_D16);
  reset(); input = {header(42), trailer(2)}; fail_read_at = 1;
  event = v1190_readout::read_single_event(access(), data);
  assert(!event.valid && next_word == 1 && mode == MVME_DMODE_D16);
  reset(); input = {header(42), trailer(2)}; fail_set_mode = MVME_DMODE_D32;
  event = v1190_readout::read_single_event(access(), data);
  assert(!event.valid && next_word == 0 && mode == MVME_DMODE_D16);
  reset(); input = {header(42), trailer(2)}; fail_set_mode = MVME_DMODE_D16;
  event = v1190_readout::read_single_event(access(), data);
  assert(!event.valid && event.trailer_consumed && event.event_counter == 42 && next_word == 2);

  V1190_FIFO_BLT_STATE state = {};
  reset(); auto result = v1190_readout::read_fifo_blt32_event(access(), data, false, state);
  assert(result.status == V1190_FIFO_BLT_OK && !result.stop_required && result.event.valid);
  assert(result.event.words == 10 && result.event.event_counter == 0x123456 && result.event.trailer_word_count == 10);
  assert(result.details.fifo_event_counter == 0x3456 && state.successful_event_count == 1 && state.successful_events_mod100 == 1);
  assert((order == std::vector<char>{'S','F','L'}) && std::memcmp(data, fifo_data, 40) == 0);
  reset(); fifo_data[9] = trailer(9);
  result = v1190_readout::read_fifo_blt32_event(access(), data, false, state);
  assert(result.status == V1190_FIFO_BLT_INVALID_EVENT && result.stop_required && !result.event.valid && state.successful_event_count == 1);
  reset(); fifo_entry = (0x3456u << 16) | (V1190_BLT_APERTURE_WORDS + 1u);
  result = v1190_readout::read_fifo_blt32_event(access(), data, false, state);
  assert(result.status == V1190_FIFO_BLT_INVALID_COUNT && result.stop_required && blt_calls == 0);
  reset(); short_bytes = 4;
  result = v1190_readout::read_fifo_blt32_event(access(), data, false, state);
  assert(result.status == V1190_FIFO_BLT_SHORT_TRANSFER && result.stop_required && state.successful_event_count == 1);
  reset(); blt_status = -2;
  result = v1190_readout::read_fifo_blt32_event(access(), data, false, state);
  assert(result.status == V1190_FIFO_BLT_TRANSFER_ERROR && result.details.caen_status == -2 && state.successful_event_count == 1);
  reset(); fail_fifo16 = 1;
  result = v1190_readout::read_fifo_blt32_event(access(), data, false, state);
  assert(result.status == V1190_FIFO_BLT_STATUS_ERROR && blt_calls == 0);
  reset(); fail_fifo32 = 1;
  result = v1190_readout::read_fifo_blt32_event(access(), data, false, state);
  assert(result.status == V1190_FIFO_BLT_ENTRY_ERROR && blt_calls == 0);
  reset(); state.successful_events_mod100 = 99;
  result = v1190_readout::read_fifo_blt32_event(access(), data, false, state);
  assert(result.status == V1190_FIFO_BLT_OK && state.successful_events_mod100 == 0 && state.successful_event_count == 2);
  assert((order == std::vector<char>{'S','F','L','A'}) && result.details.timing.stored_after_checked);
  std::puts("V1190 readout mock tests passed");
}
