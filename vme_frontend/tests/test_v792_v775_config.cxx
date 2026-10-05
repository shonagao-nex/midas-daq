#include "../v792_config.h"
#include "../v775_config.h"
#include <cassert>
#include <cstdio>

int main()
{
    const auto adc = v792_config::default_v792_settings();
    const auto tdc = v775_config::default_v775_settings();
    assert(adc.enabled && adc.iped == 0x00FF && !adc.all_trigger_enabled);
    assert(!tdc.enabled && tdc.full_scale_range == 0x00FF);

    WORD set = 0, clear = 0;
    v775_config::v775_run_bits(tdc, set, clear);
    assert(set == (V775_BIT2_OVER_RANGE | V775_BIT2_LOW_THRESHOLD |
                   V775_BIT2_COMMON_STOP | V775_BIT2_EMPTY_PROGRAM));
    assert(clear == (V775_BIT2_VALID_CONTROL | V775_BIT2_SLIDE_ENABLE |
                     V775_BIT2_ALL_TRIGGER));

    WORD thresholds[32] = {};
    thresholds[31] = 0x123;
    V792ReadbackSnapshot adc_readback = {};
    v792_config::capture_v792_readback(adc_readback, 0x11, 0xFF,
                                       v792_config::kV792AllTrigger,
                                       TRUE, TRUE, thresholds, true);
    assert(adc_readback.valid && adc_readback.threshold[31] == 0x123);
    assert(adc_readback.zero_suppression_enabled &&
           adc_readback.all_trigger_enabled);

    V775ReadbackSnapshot tdc_readback = {};
    v775_config::capture_v775_readback(tdc_readback, 0x22, 0xFF, 0x03,
                                       set, thresholds, true);
    assert(tdc_readback.valid && tdc_readback.threshold[31] == 0x123);
    assert(tdc_readback.empty_program_enabled && tdc_readback.common_stop);
    assert(!tdc_readback.valid_control_enabled);
    std::puts("test_v792_v775_config: passed");
}
