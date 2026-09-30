#ifndef V7XX_CONFIG_H
#define V7XX_CONFIG_H

#include "vme_odb.h"
#include "v775.h"

namespace v7xx_config {

constexpr WORD kV792LowThreshold = 0x0010;
constexpr WORD kV792AllTrigger = 0x4000;

struct V775DiagnosticState {
    WORD saved_bit_set2 = 0;
    bool saved = false;
    bool empty_program_may_have_changed = false;
};

V792Settings default_v792_settings();
V775Settings default_v775_settings();
void v775_run_bits(const V775Settings &settings, WORD &set, WORD &clear);
void capture_v792_readback(V792ReadbackSnapshot &snapshot, WORD firmware,
                           WORD iped, WORD bits, BOOL zero_suppression,
                           BOOL all_trigger, const WORD (&thresholds)[32],
                           bool valid);
void capture_v775_readback(V775ReadbackSnapshot &snapshot, WORD firmware,
                           WORD full_scale, WORD fast_clear, WORD bits,
                           const WORD (&thresholds)[32], bool valid);

}  // namespace v7xx_config

#endif
