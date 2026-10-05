#include "v792_config.h"
#include "v7xx_config.h"

namespace v792_config {

// Apply IPED, zero suppression, and ALL TRG in the established order.
bool configure_for_run(const Access &access, const V792Settings &settings)
{
  const DWORD zs_reg = settings.zero_suppression_enabled ? V792_BIT_CLEAR2_WO : V792_BIT_SET2_RW;
  const DWORD at_reg = settings.all_trigger_enabled ? V792_BIT_SET2_RW : V792_BIT_CLEAR2_WO;
  return access.write16(access.vme, access.base + V792_IPED_RW, settings.iped, "V792 Iped") &&
         access.write16(access.vme, access.base + zs_reg, v7xx_config::kV792LowThreshold, "V792 zero suppression") &&
         access.write16(access.vme, access.base + at_reg, v7xx_config::kV792AllTrigger, "V792 ALL TRG");
}

// Read V792 registers and compare their run settings.
VerifyStatus verify_configuration(const Access &access, const V792Settings &settings, Readback &result)
{
  if (!access.read16(access.vme, access.base + V792_IPED_RW, result.iped, "V792 Iped verify") ||
      !access.read16(access.vme, access.base + V792_BIT_SET2_RW, result.bits, "V792 Bit Set 2 verify") ||
      !access.read16(access.vme, access.base + V792_FIRM_REV, result.firmware, "V792 Firmware verify") ||
      access.read_thresholds(access.vme, access.base, result.thresholds) != V792_MAX_CHANNELS)
    return VerifyStatus::ReadFailure;

  result.zero_suppression = !(result.bits & v7xx_config::kV792LowThreshold);
  result.all_trigger = !!(result.bits & v7xx_config::kV792AllTrigger);
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
  return ok ? VerifyStatus::Matched : VerifyStatus::Mismatch;
}

// Write the Data Clear bit and then clear that bit.
bool clear_data(const Access &access, bool manual)
{
  const char *set_desc = manual ? "V792 manual Data Clear set" : "V792 Data Clear set";
  const char *clear_desc = manual ? "V792 manual Data Clear clear" : "V792 Data Clear clear";
  return access.write16(access.vme, access.base + V792_BIT_SET2_RW, 0x0004, set_desc) &&
         access.write16(access.vme, access.base + V792_BIT_CLEAR2_WO, 0x0004, clear_desc);
}

}
