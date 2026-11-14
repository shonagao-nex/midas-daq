#include "../v792_config.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr DWORD kBase = 0x00600000;

struct Operation {
  char kind;
  DWORD address;
  WORD value;
  std::string description;
};

std::vector<Operation> operations;
WORD iped = 0x00FF;
WORD bits = 0;
WORD firmware = 0x0034;
bool fail_read = false;
bool fail_write = false;
constexpr DWORD kThresholdBase = kBase + V792_THRES_BASE;
WORD thresholds[32];
bool fail_thresholds = false;
size_t write_count = 0;
size_t fail_write_at = 0;

bool read16(MVME_INTERFACE *vme, DWORD address, WORD &value, const char *description)
{
  assert(vme == reinterpret_cast<MVME_INTERFACE *>(1));
  operations.push_back({'R', address, 0, description});
  if (fail_read) return false;
  if (address == kBase + V792_IPED_RW) value = iped;
  else if (address == kBase + V792_BIT_SET2_RW) value = bits;
  else if (address == kBase + V792_FIRM_REV) value = firmware;
  else if (address >= kThresholdBase && address < kThresholdBase + sizeof(thresholds)) {
    if (fail_thresholds) return false;
    assert((address - kThresholdBase) % 2 == 0);
    value = thresholds[(address - kThresholdBase) / 2];
  }
  else assert(false);
  return true;
}

bool write16(MVME_INTERFACE *vme, DWORD address, WORD value, const char *description)
{
  assert(vme == reinterpret_cast<MVME_INTERFACE *>(1));
  operations.push_back({'W', address, value, description});
  ++write_count;
  if (fail_write || write_count == fail_write_at) return false;
  if (address >= kThresholdBase && address < kThresholdBase + sizeof(thresholds)) {
    assert((address - kThresholdBase) % 2 == 0);
    thresholds[(address - kThresholdBase) / 2] = value;
  }
  return true;
}

void reset()
{
  operations.clear();
  iped = 0x00FF;
  bits = 0;
  firmware = 0x0034;
  fail_read = false;
  fail_write = false;
  fail_thresholds = false;
  for (unsigned ch = 0; ch < 32; ++ch)
    thresholds[ch] = static_cast<WORD>(ch);
  write_count = 0;
  fail_write_at = 0;
}

}

int main()
{
  const v792_config::Access access = {
      reinterpret_cast<MVME_INTERFACE *>(1), kBase, "test_v792_config_mock", read16, write16};
  V792Settings settings = v792_config::default_v792_settings();
  settings.iped = 0x00FF;
  settings.zero_suppression_enabled = TRUE;
  settings.all_trigger_enabled = TRUE;

  reset();
  assert(v792_config::configure_for_run(access, settings));
  assert(operations.size() == 3 + 32);
  assert(operations[0].address == kBase + V792_IPED_RW && operations[0].value == 0x00FF);
  assert(operations[1].address == kBase + V792_BIT_CLEAR2_WO &&
         operations[1].value == v792_config::kV792LowThreshold);
  assert(operations[2].address == kBase + V792_BIT_SET2_RW &&
         operations[2].value == v792_config::kV792AllTrigger);

  reset();
  settings.zero_suppression_enabled = FALSE;
  settings.all_trigger_enabled = FALSE;
  assert(v792_config::configure_for_run(access, settings));
  assert(operations[1].address == kBase + V792_BIT_SET2_RW);
  assert(operations[2].address == kBase + V792_BIT_CLEAR2_WO);
  settings.zero_suppression_enabled = TRUE;
  settings.all_trigger_enabled = TRUE;

  reset();
  fail_write_at = 2;
  assert(!v792_config::configure_for_run(access, settings));
  assert(operations.size() == 2);

  reset();
  bits = v792_config::kV792AllTrigger;
  v792_config::Readback result;
  assert(v792_config::verify_configuration(access, settings, result) == v792_config::VerifyStatus::Matched);
  assert(operations.size() == 3 + 32);
  assert(operations[3].address == kThresholdBase);
  assert(result.firmware == firmware && result.thresholds[31] == 31);

  reset();
  bits = v792_config::kV792LowThreshold;
  assert(v792_config::verify_configuration(access, settings, result) == v792_config::VerifyStatus::Mismatch);
  assert(operations.size() == 3 + 32);

  reset();
  fail_read = true;
  assert(v792_config::verify_configuration(access, settings, result) == v792_config::VerifyStatus::ReadFailure);
  assert(operations.size() == 1);

  reset();
  fail_thresholds = true;
  assert(v792_config::verify_configuration(access, settings, result) == v792_config::VerifyStatus::ReadFailure);
  assert(operations.size() == 3 + 1);

  reset();
  assert(v792_config::clear_data(access, false));
  assert(operations.size() == 2);
  assert(operations[0].address == kBase + V792_BIT_SET2_RW && operations[0].value == 0x0004);
  assert(operations[1].address == kBase + V792_BIT_CLEAR2_WO && operations[1].value == 0x0004);
  assert(operations[0].description == "V792 Data Clear set");

  reset();
  fail_write_at = 1;
  assert(!v792_config::clear_data(access, true));
  assert(operations.size() == 1 && operations[0].description == "V792 manual Data Clear set");

  reset();
  fail_write_at = 2;
  assert(!v792_config::clear_data(access, true));
  assert(operations.size() == 2 && operations[1].description == "V792 manual Data Clear clear");

  reset();
  bits = v792_config::kV792AllTrigger;
  assert(settings.channel_enable_mask == 0xFFFFFFFFu);
  auto masked = settings;
  masked.channel_enable_mask = 0x0000FFFFu;
  // Start with ch0–15 disabled and ch16–31 enabled to exercise both transitions.
  for (unsigned ch = 0; ch < 16; ++ch)
    thresholds[ch] |= 0x0100;
  assert(v792_config::configure_for_run(access, masked));
  for (unsigned ch = 0; ch < 32; ++ch)
    assert(thresholds[ch] == (ch | (ch >= 16 ? 0x0100u : 0u)));
  assert(v792_config::verify_configuration(access, masked, result) == v792_config::VerifyStatus::Matched);
  assert(result.thresholds[15] == 0x000F && result.thresholds[16] == 0x0110);

  thresholds[31] &= 0x00FF;
  assert(v792_config::verify_configuration(access, masked, result) == v792_config::VerifyStatus::Mismatch);
  assert(v792_config::configure_for_run(access, settings));
  for (unsigned ch = 0; ch < 32; ++ch)
    assert(thresholds[ch] == ch);
  assert(v792_config::verify_configuration(access, settings, result) == v792_config::VerifyStatus::Matched);

  reset();
  fail_write_at = 4;
  assert(!v792_config::configure_for_run(access, masked));
  assert(operations.back().address == kThresholdBase + 2 * 16);
  assert(thresholds[16] == 16);

  reset();
  fail_thresholds = true;
  assert(!v792_config::configure_for_run(access, masked));
  assert(operations.back().address == kThresholdBase);

  std::puts("test_v792_config_mock: passed");
}
