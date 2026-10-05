#include "../v775_config.h"
#include "../v7xx_config.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr DWORD kBase = 0x00000000;

struct Operation {
  char kind;
  DWORD address;
  WORD value;
  std::string description;
};

std::vector<Operation> operations;
WORD full_scale = 0x00FF;
WORD bits = V775_BIT2_OVER_RANGE | V775_BIT2_LOW_THRESHOLD |
            V775_BIT2_COMMON_STOP | V775_BIT2_EMPTY_PROGRAM;
WORD firmware = 0x0034;
WORD fast_clear = 0x0005;
bool fail_read = false;
bool fail_thresholds = false;
size_t write_count = 0;
size_t fail_write_at = 0;

bool read16(MVME_INTERFACE *vme, DWORD address, WORD &value, const char *description)
{
  assert(vme == reinterpret_cast<MVME_INTERFACE *>(1));
  operations.push_back({'R', address, 0, description});
  if (fail_read) return false;
  if (address == kBase + V775_FULL_SCALE_RANGE) value = full_scale;
  else if (address == kBase + V775_BIT_SET2) value = bits;
  else if (address == kBase + V775_FIRMWARE_REVISION) value = firmware;
  else if (address == kBase + V775_FCLR_WINDOW) value = fast_clear;
  else assert(false);
  return true;
}

bool write16(MVME_INTERFACE *vme, DWORD address, WORD value, const char *description)
{
  assert(vme == reinterpret_cast<MVME_INTERFACE *>(1));
  operations.push_back({'W', address, value, description});
  ++write_count;
  return write_count != fail_write_at;
}

int read_thresholds(MVME_INTERFACE *vme, DWORD base, WORD *thresholds)
{
  assert(vme == reinterpret_cast<MVME_INTERFACE *>(1) && base == kBase);
  operations.push_back({'T', base, 0, "thresholds"});
  if (fail_thresholds) return 0;
  for (DWORD i = 0; i < V775_MAX_CHANNELS; ++i) thresholds[i] = static_cast<WORD>(i);
  return V775_MAX_CHANNELS;
}

void reset()
{
  operations.clear();
  full_scale = 0x00FF;
  bits = V775_BIT2_OVER_RANGE | V775_BIT2_LOW_THRESHOLD |
         V775_BIT2_COMMON_STOP | V775_BIT2_EMPTY_PROGRAM;
  firmware = 0x0034;
  fast_clear = 0x0005;
  fail_read = false;
  fail_thresholds = false;
  write_count = 0;
  fail_write_at = 0;
}

}

int main()
{
  const v775_config::Access access = {
      reinterpret_cast<MVME_INTERFACE *>(1), kBase, "test_v775_config_mock", read16, write16, read_thresholds};
  const V775Settings settings = v7xx_config::default_v775_settings();

  reset();
  assert(v775_config::configure_for_run(access, settings));
  assert(operations.size() == 3);
  assert(operations[0].address == kBase + V775_FULL_SCALE_RANGE && operations[0].value == settings.full_scale_range);
  assert(operations[1].address == kBase + V775_BIT_SET2 && operations[1].value == bits);
  assert(operations[2].address == kBase + V775_BIT_CLEAR2 &&
         operations[2].value == (V775_BIT2_VALID_CONTROL | V775_BIT2_SLIDE_ENABLE | V775_BIT2_ALL_TRIGGER));

  reset();
  fail_write_at = 2;
  assert(!v775_config::configure_for_run(access, settings));
  assert(operations.size() == 2);

  reset();
  v775_config::Readback result;
  assert(v775_config::verify_configuration(access, settings, result) == v775_config::VerifyStatus::Matched);
  assert(operations.size() == 5 && operations[4].kind == 'T');
  assert(result.firmware == firmware && result.fast_clear == fast_clear && result.thresholds[31] == 31);

  reset();
  full_scale = 0x00FE;
  bits &= ~V775_BIT2_COMMON_STOP;
  assert(v775_config::verify_configuration(access, settings, result) == v775_config::VerifyStatus::Mismatch);
  assert(operations.size() == 5);

  reset();
  fail_read = true;
  assert(v775_config::verify_configuration(access, settings, result) == v775_config::VerifyStatus::ReadFailure);
  assert(operations.size() == 1);

  reset();
  fail_thresholds = true;
  assert(v775_config::verify_configuration(access, settings, result) == v775_config::VerifyStatus::ReadFailure);
  assert(operations.size() == 5);

  reset();
  assert(v775_config::clear_data(access, false));
  assert(operations.size() == 2);
  assert(operations[0].address == kBase + V775_BIT_SET2 && operations[0].value == V775_BIT2_CLEAR_DATA);
  assert(operations[1].address == kBase + V775_BIT_CLEAR2 && operations[1].value == V775_BIT2_CLEAR_DATA);
  assert(operations[0].description == "V775 Data Clear set");

  reset();
  fail_write_at = 1;
  assert(!v775_config::clear_data(access, true));
  assert(operations.size() == 1 && operations[0].description == "V775 manual Data Clear set");

  reset();
  fail_write_at = 2;
  assert(!v775_config::clear_data(access, true));
  assert(operations.size() == 2 && operations[1].description == "V775 manual Data Clear clear");

  std::puts("test_v775_config_mock: passed");
}
