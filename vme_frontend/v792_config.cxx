#include "v792_config.h"
#include "v7xx_channel_mask.h"
#include "v792_basic.h"
#include <cstring>

namespace v792_config {

V792Settings default_v792_settings()
{
    V792Settings s = {};
    s.enabled = TRUE;
    s.channel_enable_mask = 0xFFFFFFFFu;
    s.iped = 0x00FF;
    s.zero_suppression_enabled = FALSE;
    s.all_trigger_enabled = FALSE;
    return s;
}

void capture_v792_readback(V792ReadbackSnapshot &snapshot, WORD firmware,
                           WORD iped, WORD bits, BOOL zero_suppression,
                           BOOL all_trigger, const WORD (&thresholds)[32],
                           bool valid)
{
    snapshot.valid = valid ? TRUE : FALSE;
    snapshot.firmware_revision = firmware;
    snapshot.iped = iped;
    snapshot.zero_suppression_enabled = zero_suppression;
    snapshot.all_trigger_enabled = all_trigger;
    snapshot.bit_set2_raw = bits;
    std::memcpy(snapshot.threshold, thresholds, sizeof(thresholds));
}


// Apply IPED, zero suppression, and ALL TRG in the established order.
bool configure_for_run(const Access &access, const V792Settings &settings)
{
  const DWORD zs_reg = settings.zero_suppression_enabled ? V792_BIT_CLEAR2_WO : V792_BIT_SET2_RW;
  const DWORD at_reg = settings.all_trigger_enabled ? V792_BIT_SET2_RW : V792_BIT_CLEAR2_WO;
  return access.write16(access.vme, access.base + V792_IPED_RW, settings.iped, "V792 Iped") &&
         access.write16(access.vme, access.base + zs_reg, kV792LowThreshold, "V792 zero suppression") &&
         access.write16(access.vme, access.base + at_reg, kV792AllTrigger, "V792 ALL TRG") &&
         v7xx_channel_mask::apply(access, V792_THRES_BASE, settings.channel_enable_mask,
                                 "V792 channel mask");
}

// Read V792 registers and compare their run settings.
VerifyStatus verify_configuration(const Access &access, const V792Settings &settings, Readback &result)
{
  if (!access.read16(access.vme, access.base + V792_IPED_RW, result.iped, "V792 Iped verify") ||
      !access.read16(access.vme, access.base + V792_BIT_SET2_RW, result.bits, "V792 Bit Set 2 verify") ||
      !access.read16(access.vme, access.base + V792_FIRM_REV, result.firmware, "V792 Firmware verify") ||
      !v7xx_channel_mask::readback(access, V792_THRES_BASE, result.thresholds,
                                   "V792 channel mask verify"))
    return VerifyStatus::ReadFailure;

  result.zero_suppression = !(result.bits & kV792LowThreshold);
  result.all_trigger = !!(result.bits & kV792AllTrigger);
  bool ok = true;
  const auto verify = [&](const char *item, unsigned expected, unsigned actual) {
    if (expected == actual) return true;
    cm_msg(MERROR, access.log_source, "V792 configuration verify failed: %s expected 0x%X, read back 0x%X",
           item, expected, actual);
    return false;
  };
  ok = verify("Iped", settings.iped, result.iped & 0xFF);
  ok = verify("ZeroSuppression", settings.zero_suppression_enabled, result.zero_suppression) && ok;
  ok = verify("AllTrigger", settings.all_trigger_enabled, result.all_trigger) && ok;
  ok = verify("ChannelEnableMask", settings.channel_enable_mask,
              v7xx_channel_mask::enabled_mask(result.thresholds)) && ok;
  return ok ? VerifyStatus::Matched : VerifyStatus::Mismatch;
}

// Write the Data Clear bit and then clear that bit.
bool clear_data(const Access &access, bool manual)
{
  return v792_basic::clear_data(access.vme, access.base, access.write16, manual);
}

}
