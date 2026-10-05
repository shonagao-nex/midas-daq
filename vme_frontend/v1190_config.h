#ifndef V1190_CONFIG_H
#define V1190_CONFIG_H

#include "vme_odb.h"
#include <cstddef>
#include <functional>

namespace v1190_config {

constexpr size_t kChannelMaskWords = 8;

struct Access {
    bool (*read16)(DWORD, WORD &, const char *);
    bool (*write16)(DWORD, WORD, const char *);
    std::function<bool(WORD)> micro_write_opcode;
    std::function<bool(WORD, const WORD *, size_t)> micro_write_command;
    std::function<bool(WORD, WORD *, size_t)> micro_read_command;
    std::function<bool(WORD &)> read_acquisition_mode;
};

struct V1190Configuration {
    WORD mode, trigger[5], edge, resolution, dead_time, header, max_hits;
    WORD error_mask, fifo_size, channels[kChannelMaskWords];
    WORD control, status, firmware, rom_version;
    WORD pout_selection, almost_full_level_words;
};

struct DiagnosticState {
    WORD saved_control = 0;
    bool saved = false;
    bool mode_may_have_changed = false;
    bool empty_event_may_have_changed = false;
};

struct State {
    V1190Settings run_settings;
    DiagnosticState diagnostic;
};

V1190Settings default_settings();
bool read_configuration(const Access &access, V1190Configuration &configuration);
bool encode_semantics(const V1190Settings &settings, WORD &resolution,
                      WORD &dead_time, WORD &max_hits,
                      WORD (&channels)[kChannelMaskWords]);
bool configure_pout_startup(const Access &access, bool enabled);
bool configure_for_run(const Access &access, const V1190Settings &settings,
                       bool fifo_blt32);
bool verify_configuration(const Access &access, const V1190Settings &settings,
                          bool fifo_blt32, V1190ReadbackSnapshot &readback);
bool restore_diagnostic_settings(const Access &access, DiagnosticState &state);
bool setup_soft_trigger_test(const Access &access, DiagnosticState &state);

}  // namespace v1190_config

#endif
