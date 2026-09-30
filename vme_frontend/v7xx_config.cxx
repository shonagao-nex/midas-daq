#include "v7xx_config.h"
#include <cstring>

namespace v7xx_config {

//************************************//
// Provide the established V792 run defaults
//************************************//
V792Settings default_v792_settings()
{
    V792Settings s = {};
    s.enabled = TRUE;
    s.iped = 0x00FF;
    s.zero_suppression_enabled = FALSE;
    s.all_trigger_enabled = FALSE;
    return s;
}

//************************************//
// Provide the established V775 run defaults
//************************************//
V775Settings default_v775_settings()
{
    V775Settings s = {};
    s.enabled = FALSE;
    s.full_scale_range = 0x00FF; // nominal 140 ns / 35 ps LSB
    s.over_range_enabled = TRUE;
    s.low_threshold_enabled = TRUE;
    s.common_stop = TRUE;
    s.empty_program_enabled = TRUE;
    s.valid_control_enabled = FALSE;
    s.sliding_scale_enabled = FALSE;
    s.all_trigger_enabled = FALSE;
    return s;
}

//************************************//
// Calculate the V775 Bit Set/Clear 2 command values
//************************************//
void v775_run_bits(const V775Settings &settings, WORD &set, WORD &clear)
{
    set = 0;
    clear = 0;
#define V775BIT(flag, bit) do { if (flag) set |= bit; else clear |= bit; } while (0)
    V775BIT(settings.over_range_enabled, V775_BIT2_OVER_RANGE);
    V775BIT(settings.low_threshold_enabled, V775_BIT2_LOW_THRESHOLD);
    V775BIT(settings.common_stop, V775_BIT2_COMMON_STOP);
    V775BIT(settings.empty_program_enabled, V775_BIT2_EMPTY_PROGRAM);
    V775BIT(settings.valid_control_enabled, V775_BIT2_VALID_CONTROL);
    V775BIT(settings.sliding_scale_enabled, V775_BIT2_SLIDE_ENABLE);
    V775BIT(settings.all_trigger_enabled, V775_BIT2_ALL_TRIGGER);
#undef V775BIT
}

//************************************//
// Save the verified V792 register values in the run snapshot
//************************************//
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

//************************************//
// Save the verified V775 register values in the run snapshot
//************************************//
void capture_v775_readback(V775ReadbackSnapshot &snapshot, WORD firmware,
                           WORD full_scale, WORD fast_clear, WORD bits,
                           const WORD (&thresholds)[32], bool valid)
{
    snapshot.valid = valid ? TRUE : FALSE;
    snapshot.firmware_revision = firmware;
    snapshot.full_scale_range = full_scale;
    snapshot.fast_clear_window = fast_clear;
    snapshot.over_range_enabled = !!(bits & V775_BIT2_OVER_RANGE);
    snapshot.low_threshold_enabled = !!(bits & V775_BIT2_LOW_THRESHOLD);
    snapshot.common_stop = !!(bits & V775_BIT2_COMMON_STOP);
    snapshot.empty_program_enabled = !!(bits & V775_BIT2_EMPTY_PROGRAM);
    snapshot.valid_control_enabled = !!(bits & V775_BIT2_VALID_CONTROL);
    snapshot.sliding_scale_enabled = !!(bits & V775_BIT2_SLIDE_ENABLE);
    snapshot.all_trigger_enabled = !!(bits & V775_BIT2_ALL_TRIGGER);
    snapshot.bit_set2_raw = bits;
    std::memcpy(snapshot.threshold, thresholds, sizeof(thresholds));
}

}  // namespace v7xx_config
