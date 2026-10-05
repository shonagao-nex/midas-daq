#include "v775_config.h"
#include "v7xx_config.h"

namespace v775_config {

// Apply full scale and Bit Set/Clear 2 in the established order.
bool configure_for_run(const Access &access, const V775Settings &settings)
{
  WORD set = 0, clear = 0;
  v7xx_config::v775_run_bits(settings, set, clear);
  return access.write16(access.vme, access.base + V775_FULL_SCALE_RANGE, settings.full_scale_range, "V775 Full Scale Range") &&
         access.write16(access.vme, access.base + V775_BIT_SET2, set, "V775 run bits set") &&
         access.write16(access.vme, access.base + V775_BIT_CLEAR2, clear, "V775 run bits clear");
}

// Read V775 registers and compare all run settings.
VerifyStatus verify_configuration(const Access &access, const V775Settings &settings, Readback &result)
{
  if (!access.read16(access.vme, access.base + V775_FULL_SCALE_RANGE, result.full_scale, "V775 FSR verify") ||
      !access.read16(access.vme, access.base + V775_BIT_SET2, result.bits, "V775 Bit Set 2 verify") ||
      !access.read16(access.vme, access.base + V775_FIRMWARE_REVISION, result.firmware, "V775 Firmware") ||
      !access.read16(access.vme, access.base + V775_FCLR_WINDOW, result.fast_clear, "V775 Fast Clear") ||
      access.read_thresholds(access.vme, access.base, result.thresholds) != V775_MAX_CHANNELS)
    return VerifyStatus::ReadFailure;

  const auto verify = [&](const char *item, unsigned expected, unsigned actual) {
    if (expected == actual) return true;
    cm_msg(MERROR, access.log_source, "V775 configuration verify failed: %s expected 0x%X, read back 0x%X",
           item, expected, actual);
    return false;
  };
  bool ok = verify("Full Scale Range", settings.full_scale_range, result.full_scale & 0xFF);
#define VERIFY_BIT(name, member, bit) ok = verify(name, settings.member, !!(result.bits & bit)) && ok
  VERIFY_BIT("OverRange", over_range_enabled, V775_BIT2_OVER_RANGE);
  VERIFY_BIT("LowThreshold", low_threshold_enabled, V775_BIT2_LOW_THRESHOLD);
  VERIFY_BIT("CommonStop", common_stop, V775_BIT2_COMMON_STOP);
  VERIFY_BIT("EmptyProgram", empty_program_enabled, V775_BIT2_EMPTY_PROGRAM);
  VERIFY_BIT("ValidControl", valid_control_enabled, V775_BIT2_VALID_CONTROL);
  VERIFY_BIT("SlidingScale", sliding_scale_enabled, V775_BIT2_SLIDE_ENABLE);
  VERIFY_BIT("AllTrigger", all_trigger_enabled, V775_BIT2_ALL_TRIGGER);
#undef VERIFY_BIT
  return ok ? VerifyStatus::Matched : VerifyStatus::Mismatch;
}

// Write the Data Clear bit and then clear that bit.
bool clear_data(const Access &access, bool manual)
{
  const char *set_desc = manual ? "V775 manual Data Clear set" : "V775 Data Clear set";
  const char *clear_desc = manual ? "V775 manual Data Clear clear" : "V775 Data Clear clear";
  return access.write16(access.vme, access.base + V775_BIT_SET2, V775_BIT2_CLEAR_DATA, set_desc) &&
         access.write16(access.vme, access.base + V775_BIT_CLEAR2, V775_BIT2_CLEAR_DATA, clear_desc);
}


bool save_diagnostic_settings(const DiagnosticAccess &access, v7xx_config::V775DiagnosticState &state)
{
  WORD bits = 0;
  if (!access.read16(access.vme, access.base + V775_BIT_SET2, bits, "V775 Bit Set 2 save")) return false;
  state.saved_bit_set2 = bits;
  state.saved = true;
  state.empty_program_may_have_changed = false;
  return true;
}

EnableResult enable_empty_program(const DiagnosticAccess &access, v7xx_config::V775DiagnosticState &state, WORD &readback)
{
  if (state.saved_bit_set2 & V775_BIT2_EMPTY_PROGRAM) return EnableResult::AlreadyEnabled;
  state.empty_program_may_have_changed = true;
  if (!access.write16(access.vme, access.base + V775_BIT_SET2, V775_BIT2_EMPTY_PROGRAM, "V775 Empty Program enable") ||
      !access.read16(access.vme, access.base + V775_BIT_SET2, readback, "V775 Bit Set 2 enable verify"))
    return EnableResult::AccessFailure;
  return (readback & V775_BIT2_EMPTY_PROGRAM) ? EnableResult::Enabled : EnableResult::VerifyFailure;
}

RestoreResult restore_diagnostic_settings(const DiagnosticAccess &access, v7xx_config::V775DiagnosticState &state, WORD &readback)
{
  if (!state.saved) return RestoreResult::NoSavedState;
  if ((state.saved_bit_set2 & V775_BIT2_EMPTY_PROGRAM) == 0 && state.empty_program_may_have_changed) {
    if (!access.write16(access.vme, access.base + V775_BIT_CLEAR2, V775_BIT2_EMPTY_PROGRAM, "V775 Empty Program restore clear") ||
        !access.read16(access.vme, access.base + V775_BIT_SET2, readback, "V775 Bit Set 2 restore verify"))
      return RestoreResult::AccessFailure;
    if (readback & V775_BIT2_EMPTY_PROGRAM) return RestoreResult::VerifyFailure;
    state.empty_program_may_have_changed = false;
    state.saved = false;
    return RestoreResult::Restored;
  }
  state.saved = false;
  return RestoreResult::NoClearNeeded;
}

bool read_before_sw_comm(const DiagnosticAccess &access, WORD &status1, WORD &status2, DWORD &counter)
{
  if (!access.read16(access.vme, access.base + V775_STATUS1, status1, "V775 Status 1 before SW Comm") ||
      !access.read16(access.vme, access.base + V775_STATUS2, status2, "V775 Status 2 before SW Comm")) return false;
  access.event_counter(access.vme, access.base, &counter);
  return true;
}

bool issue_sw_comm(const DiagnosticAccess &access)
{
  return access.write16(access.vme, access.base + V775_SW_COMM, 0, "V775 SW Comm");
}

PollResult poll_after_sw_comm(const DiagnosticAccess &access, WORD &status1, unsigned &polls_done)
{
  for (unsigned poll = 1; poll <= kDiagnosticMaxPolls; ++poll) {
    polls_done = poll;
    if (!access.read16(access.vme, access.base + V775_STATUS1, status1, "V775 Status 1 after SW Comm"))
      return PollResult::ReadFailure;
    if (status1 & V775_STATUS1_DATA_READY) return PollResult::Ready;
    if (poll < kDiagnosticMaxPolls) access.sleep_ms(1);
  }
  return PollResult::Timeout;
}

bool read_after_sw_comm(const DiagnosticAccess &access, WORD &status2, DWORD &counter)
{
  if (!access.read16(access.vme, access.base + V775_STATUS2, status2, "V775 Status 2 after SW Comm")) return false;
  access.event_counter(access.vme, access.base, &counter);
  return true;
}

}
