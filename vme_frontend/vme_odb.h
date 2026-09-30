#ifndef VME_ODB_H
#define VME_ODB_H

#include "midas.h"
#include "v1720e.h"
#include <cstddef>
#include <cstdint>

struct V1720ESettings {
    BOOL enabled;
    DWORD buffer_organization;
    DWORD record_length_samples;
    DWORD post_trigger;
    BOOL software_trigger_enabled;
    BOOL external_trigger_enabled;
    BOOL channel_self_trigger_enabled[V1720E_CHANNEL_COUNT];
    BOOL channel_enabled[V1720E_CHANNEL_COUNT];
    WORD dc_offset[V1720E_CHANNEL_COUNT];
};

struct V792Settings {
    BOOL enabled;
    WORD iped;
    BOOL zero_suppression_enabled;
    BOOL all_trigger_enabled;
};

struct V1190Settings {
    BOOL enabled;
    BOOL trigger_matching_enabled;
    DWORD window_width;
    INT window_offset;
    DWORD extra_search_margin;
    DWORD reject_margin;
    BOOL trigger_subtraction_enabled;
    DWORD edge_mode;
    DWORD resolution_ps;
    DWORD dead_time_ns;
    INT max_hits_per_event; /* -1 means unlimited. */
    BOOL tdc_header_enabled;
    BOOL empty_event_enabled;
    BOOL event_fifo_enabled;
    BOOL extended_trigger_time_enabled;
    BOOL channel_enabled[128];
};

struct V775Settings {
    BOOL enabled;
    WORD full_scale_range;
    BOOL over_range_enabled;
    BOOL low_threshold_enabled;
    BOOL common_stop;
    BOOL empty_program_enabled;
    BOOL valid_control_enabled;
    BOOL sliding_scale_enabled;
    BOOL all_trigger_enabled;
};

struct V792ReadbackSnapshot {
    BOOL valid;
    WORD firmware_revision;
    WORD iped;
    BOOL zero_suppression_enabled;
    BOOL all_trigger_enabled;
    WORD bit_set2_raw;
    WORD threshold[32];
};

struct V1190ReadbackSnapshot {
    BOOL valid;
    WORD firmware_revision;
    WORD configuration_rom_version;
    char board_type[32];
    V1190Settings settings;
    WORD error_mask;
    DWORD effective_fifo_size_words;
    WORD control_raw;
    WORD pout_selection;
    char pout_function[16];
    WORD almost_full_level_words;
};

struct V775ReadbackSnapshot {
    BOOL valid;
    WORD firmware_revision;
    WORD full_scale_range;
    WORD fast_clear_window;
    BOOL over_range_enabled;
    BOOL low_threshold_enabled;
    BOOL common_stop;
    BOOL empty_program_enabled;
    BOOL valid_control_enabled;
    BOOL sliding_scale_enabled;
    BOOL all_trigger_enabled;
    WORD bit_set2_raw;
    WORD threshold[32];
};

struct V1720EReadbackSnapshot {
    BOOL valid;
    DWORD board_info;
    DWORD roc_firmware_revision;
    DWORD buffer_organization;
    DWORD custom_size_raw;
    DWORD record_length_samples;
    DWORD post_trigger;
    BOOL software_trigger_enabled;
    BOOL external_trigger_enabled;
    BOOL channel_self_trigger_enabled[V1720E_CHANNEL_COUNT];
    BOOL channel_enabled[V1720E_CHANNEL_COUNT];
    WORD dc_offset[V1720E_CHANNEL_COUNT];
    BOOL zero_suppression_enabled;
    BOOL pack25_enabled;
    DWORD trigger_source_raw;
    DWORD channel_enable_raw;
    DWORD channel_config_raw;
};

struct VmeRunSnapshot {
    DWORD schema_version;
    char snapshot_id[128];
    INT run_number;
    uint64_t bor_unix_time;
    char bor_time_iso8601[32];
    char frontend_name[32];
    BOOL frontend_bor_complete;
    BOOL enabled_for_run;
    V792Settings v792_requested;
    V1190Settings v1190_requested;
    V775Settings v775_requested;
    V1720ESettings v1720e_requested;
    BOOL rpv130_enabled;
    BOOL rpv130_single_event_busy_enabled;
    V792ReadbackSnapshot v792_readback;
    V1190ReadbackSnapshot v1190_readback;
    V775ReadbackSnapshot v775_readback;
    V1720EReadbackSnapshot v1720e_readback;
};

struct V1720ERuntimeState {
    BOOL enabled_for_run;
    BOOL communication_ok;
    DWORD acquisition_control;
    DWORD acquisition_status;
    BOOL running;
    BOOL event_ready;
    BOOL external_clock;
    BOOL pll_locked;
    BOOL board_ready;
    DWORD event_stored;
    DWORD event_counter;
    DWORD trigger_time_tag;
    bool dirty;
};

struct V7xxRuntimeState {
    BOOL enabled_for_run;
    BOOL communication_ok;
    WORD status1;
    WORD status2;
    BOOL data_ready;
    BOOL busy;
    BOOL buffer_empty;
    BOOL buffer_full;
    DWORD event_counter;
    bool dirty;
};

struct V1190RuntimeState {
    BOOL enabled_for_run;
    BOOL communication_ok;
    WORD status;
    BOOL data_ready;
    BOOL almost_full;
    BOOL full;
    BOOL trigger_matching;
    DWORD event_stored;
    DWORD event_counter;
    bool dirty;
};

struct RunStatistics {
    uint64_t counter_mismatch_count;
    uint32_t first_mismatch_serial;
    uint32_t last_mismatch_serial;
    uint64_t v1720_malformed_count;
    uint64_t v1720_size_error_count;
    uint64_t v1720_mask_error_count;
    uint64_t v1720_read_timeout_count;
    uint64_t v1720_counter_discontinuity_count;
    uint64_t v1720_ttt_count;
    DWORD v1720_first_counter;
    DWORD v1720_last_counter;
    DWORD v1720_previous_counter;
    DWORD v1720_previous_ttt;
    DWORD v1720_min_ttt_delta;
    DWORD v1720_max_ttt_delta;
    bool v1720_have_previous;
};

namespace vme_odb {

bool make_odb_path(char *path, size_t capacity, const char *base,
                   const char *name);
bool set_absolute_odb_value(const char *path, const void *value,
                            INT size, INT count, DWORD type);
bool ensure_odb_value(const char *path, const void *default_value,
                      INT size, INT count, DWORD type);
bool get_absolute_odb_value(const char *path, void *value, INT size,
                            DWORD type);
bool publish_configuration_status(bool configuration_ok, INT run_number);
bool set_module_output(const char *base, const char *name,
                       const void *value, INT size, INT count, DWORD type);
bool initialize_rpv130_odb(bool &rpv130_enabled_for_run,
                            bool &single_event_busy_enabled_for_run);
bool publish_module_info(const char *path, DWORD base_address,
                         const char *address_modifier,
                         const char *register_width,
                         const char *event_width);
bool ensure_v792_settings_schema(const V792Settings &defaults);
bool ensure_v1190_settings_schema(const V1190Settings &defaults);
bool ensure_v775_settings_schema(const V775Settings &defaults);
bool read_v792_settings(V792Settings &settings);
bool read_v1190_settings(V1190Settings &settings);
bool read_v775_settings(V775Settings &settings);
bool ensure_v1720e_settings_schema(const V1720ESettings &defaults);
bool read_v1720e_settings(V1720ESettings &settings);
bool publish_v1720e_info();
void set_module_readback_valid(const char *path, bool valid);
void initialize_module_output_schema(
    V7xxRuntimeState &v792, V1190RuntimeState &v1190, V7xxRuntimeState &v775,
    DWORD &v792_last_publish, DWORD &v1190_last_publish,
    DWORD &v775_last_publish);
bool publish_v7xx_variables(const char *path, V7xxRuntimeState &runtime,
                             DWORD &last_publish);
bool publish_v1190_variables(V1190RuntimeState &runtime, DWORD &last_publish);
bool publish_v1720e_variables(V1720ERuntimeState &runtime, DWORD &last_publish);
bool publish_run_counters(const RunStatistics &counters, bool &dirty,
                          DWORD &last_publish);
void reset_run_snapshot(VmeRunSnapshot &snapshot, INT run_number,
                        const char *frontend_name);
bool publish_run_snapshot(const VmeRunSnapshot &snapshot);

}  // namespace vme_odb

#endif
