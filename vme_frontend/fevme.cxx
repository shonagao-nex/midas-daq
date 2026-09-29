#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <atomic>
#include <string>

#include "midas.h"
#include "mfe.h"
#include "mvmestd.h"
#include "../common/manual_buffer_clear.h"
#include "vme/v792.h"
#include "v775.h"
#include "v1720e.h"
#include "v792_blt32.h"
#include "v1190_fifo_blt32.h"
#include "caenvme.h"
#include "rpv130.h"
#include "global_busy.h"
#include "out0_diagnostic.h"

/* Module locations. */
#define V792_BASE 0x00600000
#define V1190_BASE 0x00C10000
#define V775_BASE 0x00000000
#define V1720E_BASE V1720E_BASE_ADDRESS

#ifndef ENABLE_V792_SW_TRIGGER_TEST
#define ENABLE_V792_SW_TRIGGER_TEST 0
#endif
#ifndef ENABLE_V1190_SOFT_TRIGGER_TEST
#define ENABLE_V1190_SOFT_TRIGGER_TEST 0
#endif
#ifndef ENABLE_V775_SW_TRIGGER_TEST
#define ENABLE_V775_SW_TRIGGER_TEST 0
#endif

/* Bounded single-event readout limits and counter widths. */
static const size_t V792_MAX_EVENT_WORDS = 64;
enum V792ReadoutMode { V792_SINGLE_D32, V792_BLT32 };
/* Source selection for the V792 hardware comparison run. */
static const V792ReadoutMode V792_READOUT_MODE_SELECT = V792_BLT32;
static const size_t V1190_MAX_EVENT_WORDS = 4096;
enum V1190ReadoutMode { V1190_SINGLE_D32, V1190_EVENT_FIFO_BLT32 };
static const V1190ReadoutMode V1190_READOUT_MODE_SELECT = V1190_EVENT_FIFO_BLT32;
/* Set true for the original per-event Event FIFO Stored before/after checks. */
static const bool V1190_FIFO_STRICT_SYNC_CHECK = false;
static const size_t V775_MAX_EVENT_WORDS = 64;
static const DWORD V7XX_EVENT_COUNTER_MASK = 0x00FFFFFF;
static const DWORD V1190_EVENT_COUNTER_MASK = 0x003FFFFF;

/* Normal-run V792 configuration. Bit Set/Clear 2 uses command semantics. */
static const WORD V792_RUN_IPED = 0x00FF;
static const WORD V792_BIT2_LOW_THRESHOLD = 0x0010;
static const WORD V792_BIT2_ALL_TRIGGER = 0x4000;

/* Normal-run V775 configuration. Bit Set/Clear 2 uses command semantics. */
static const WORD V775_RUN_FULL_SCALE = 0x00FF; // nominal 140 ns / 35 ps LSB
static const WORD V775_RUN_SET_BITS = V775_BIT2_OVER_RANGE |
                                       V775_BIT2_LOW_THRESHOLD |
                                       V775_BIT2_COMMON_STOP |
                                       V775_BIT2_EMPTY_PROGRAM;
static const WORD V775_RUN_CLEAR_BITS = V775_BIT2_VALID_CONTROL |
                                         V775_BIT2_SLIDE_ENABLE |
                                         V775_BIT2_ALL_TRIGGER;

/* Finite ready/handshake polling limits. */
static const unsigned V1190_READY_MAX_POLLS = 100;
static const unsigned V775_READY_MAX_POLLS = 100;
static const unsigned V1720E_READY_MAX_POLLS = 100;
static const unsigned V775_SW_TRIGGER_MAX_POLLS = 100;
static const unsigned V1190_MICRO_MAX_POLLS = 1000;

/* V1190 regular registers and normal-run Control bits. */
static const DWORD V1190_CONTROL = 0x1000;
static const DWORD V1190_STATUS = 0x1002;
static const DWORD V1190_SOFT_CLEAR = 0x1016;
static const DWORD V1190_SOFT_TRIGGER = 0x101A;
static const DWORD V1190_EVENT_COUNTER = 0x101C;
static const DWORD V1190_EVENT_STORED = 0x1020;
static const DWORD V1190_ALMOST_FULL_LEVEL = 0x1022;
static const DWORD V1190_FIRMWARE_REVISION = 0x1026;
static const DWORD V1190_OUT_PROG = 0x102C;
static const WORD V1190_POUT_ALMOST_FULL = 2;
static const DWORD V1190_MICRO_DATA = 0x102E;
static const DWORD V1190_MICRO_HANDSHAKE = 0x1030;
static const DWORD V1190_EVENT_FIFO_STATUS = V1190_FIFO_STATUS_OFFSET;
static const DWORD V1190_EVENT_FIFO_STORED = V1190_FIFO_STORED_OFFSET;
static const WORD V1190_CONTROL_EMPTY_EVENT = 0x0008;
static const WORD V1190_CONTROL_EVENT_FIFO = 0x0100;
static const WORD V1190_CONTROL_EXT_TRIGGER_TIME = 0x0200;
static const WORD V1190_STATUS_DATA_READY = 0x0001;
static const WORD V1190_STATUS_ALMOST_FULL = 0x0002;
static const WORD V1190_STATUS_FULL = 0x0004;
static const WORD V1190_STATUS_TRIGGER_MATCH = 0x0008;
static const WORD V1190_FIFO_STATUS_DATA_READY = 0x0001;
static const WORD V1190_MICRO_WRITE_OK = 0x0001;
static const WORD V1190_MICRO_READ_OK = 0x0002;

/* V1190 microcontroller opcodes and operands from the V1190 manual. */
static const WORD V1190_OPCODE_TRIGGER_MATCH = 0x0000;
static const WORD V1190_OPCODE_CONTINUOUS = 0x0100;
static const WORD V1190_OPCODE_READ_ACQ_MODE = 0x0200;
static const WORD V1190_OPCODE_SET_WINDOW_WIDTH = 0x1000;
static const WORD V1190_OPCODE_SET_WINDOW_OFFSET = 0x1100;
static const WORD V1190_OPCODE_SET_EXTRA_MARGIN = 0x1200;
static const WORD V1190_OPCODE_SET_REJECT_MARGIN = 0x1300;
static const WORD V1190_OPCODE_DISABLE_TRIGGER_SUBTRACTION = 0x1500;
static const WORD V1190_OPCODE_ENABLE_TRIGGER_SUBTRACTION = 0x1400;
static const WORD V1190_OPCODE_READ_TRIGGER_CONFIG = 0x1600;
static const WORD V1190_OPCODE_SET_EDGE_MODE = 0x2200;
static const WORD V1190_OPCODE_READ_EDGE_MODE = 0x2300;
static const WORD V1190_OPCODE_SET_RESOLUTION = 0x2400;
static const WORD V1190_OPCODE_READ_RESOLUTION = 0x2600;
static const WORD V1190_OPCODE_SET_DEAD_TIME = 0x2800;
static const WORD V1190_OPCODE_READ_DEAD_TIME = 0x2900;
static const WORD V1190_OPCODE_ENABLE_TDC_HEADER = 0x3000;
static const WORD V1190_OPCODE_DISABLE_TDC_HEADER = 0x3100;
static const WORD V1190_OPCODE_READ_TDC_HEADER = 0x3200;
static const WORD V1190_OPCODE_SET_MAX_HITS = 0x3300;
static const WORD V1190_OPCODE_READ_MAX_HITS = 0x3400;
static const WORD V1190_OPCODE_READ_ERROR_MASK = 0x3A00;
static const WORD V1190_OPCODE_READ_FIFO_SIZE = 0x3C00;
static const WORD V1190_OPCODE_ENABLE_ALL_CHANNELS = 0x4200;
static const WORD V1190_OPCODE_WRITE_CHANNEL_MASK = 0x4400;
static const WORD V1190_OPCODE_READ_CHANNEL_MASK = 0x4500;
static const DWORD V1190_CONFIGURATION_ROM_VERSION = 0x4030;
static const WORD V1190_RUN_WINDOW_WIDTH = 12;
static const WORD V1190_RUN_WINDOW_OFFSET = 0xFFF4; // signed -12 counts
static const WORD V1190_RUN_EXTRA_MARGIN = 8;
static const WORD V1190_RUN_REJECT_MARGIN = 4;
static const WORD V1190_RUN_EDGE_MODE = 3;
static const WORD V1190_RUN_RESOLUTION = 2; // 100 ps
static const WORD V1190_RUN_DEAD_TIME = 0;  // approximately 5 ns
static const WORD V1190_RUN_MAX_HITS = 9;   // unlimited
static const size_t V1190_CHANNEL_MASK_WORDS = 8;

const char *frontend_name = "fevme";            // MIDAS frontend/client name
const char *frontend_file_name = __FILE__;      // Frontend source file name

BOOL frontend_call_loop = TRUE;                 // Enable periodic read-only status monitoring
BOOL equipment_common_overwrite = TRUE;         // Apply polled/run-only settings to existing ODB equipment
INT display_period = 1000;                      // MIDAS status display update period [ms]
INT max_event_size = 1024 * 1024;               // Maximum event size [bytes]
INT max_event_size_frag = 5 * 1024 * 1024;      // Maximum fragmented event size [bytes]
INT event_buffer_size = 10 * 1024 * 1024;       // MIDAS event buffer size [bytes]

static MVME_INTERFACE *gVme = NULL;              // MIDAS VME interface handle
static bool gReadoutFailed = false;              // Inhibit reads after a partial/malformed event
static std::atomic<bool> gBltStopRequested(false);
static std::atomic<unsigned> gV1190BltDiagnosticCount(0);
static std::atomic<unsigned> gV1720BltDiagnosticCount(0);
static V1190_FIFO_BLT_STATE gV1190FifoBltState = {};
static const DWORD RPV130_POLL_PERIOD_MS = 5000;
static const DWORD FRONTEND_IDLE_SLEEP_MS = 10;
static DWORD gRpv130LastPoll = 0;
static const char *RPV130_SETTINGS_PATH = "/Equipment/VME/Settings/RPV130";
static const char *RPV130_INFO_PATH = "/Equipment/VME/Info/RPV130";
static const char *RPV130_VARIABLES_PATH = "/Equipment/VME/Variables/RPV130";
static const char *RPV130_STATUS_PATH = "/Equipment/VME/Status/RPV130";
static const char *RUN_COUNTERS_PATH = "/Equipment/VME/Variables/RunCounters";
static const char *FRONTEND_VARIABLES_PATH =
    "/Equipment/VME/Variables/Frontend";
static const char *VME_RUN_SNAPSHOT_PATH = "/Equipment/VME/RunSnapshot";
static const DWORD RUN_SNAPSHOT_SCHEMA_VERSION = 1;
static bool gRpv130EnabledForRun = true;
static bool gSingleEventBusyEnabledForRun = false;
static bool gRpv130BusyConfigured = false;

/* Retain first-ten-event timing probes to preserve the readout sequence. */
static const unsigned RPV130_TIMING_EVENT_LIMIT = 10;
static std::atomic<unsigned> gRpv130TimingEventCount(0);
static std::atomic<uint64_t> gRpv130PollReadyNs(0);
static std::atomic<uint64_t> gRpv130LastPollMissNs(0);
static std::atomic<uint64_t> gRpv130PollPreviousMissNs(0);

static uint64_t monotonic_ns()
{
    struct timespec ts = {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
}

struct Rpv130EventTiming {
    bool active = false;
    unsigned index = 0;
    DWORD serial = 0;
    const char *outcome = "incomplete";
    uint64_t poll_ready_ns = 0;
    uint64_t poll_previous_miss_ns = 0;
    uint64_t read_start_ns = 0;
    uint64_t csr_confirm_ns = 0;
    uint64_t peers_start_ns = 0;
    uint64_t peers_end_ns = 0;
    uint64_t v1190_ready_start_ns = 0, v1190_ready_end_ns = 0;
    uint64_t v775_ready_start_ns = 0, v775_ready_end_ns = 0;
    uint64_t v1720_ready_start_ns = 0, v1720_ready_end_ns = 0;
    uint64_t v792_start_ns = 0, v792_end_ns = 0;
    uint64_t v1190_start_ns = 0, v1190_end_ns = 0;
    uint64_t v775_start_ns = 0, v775_end_ns = 0;
    uint64_t v1720_start_ns = 0, v1720_end_ns = 0;
    uint64_t consistency_end_ns = 0;
    uint64_t build_start_ns = 0, build_end_ns = 0;
    uint64_t clear_call_ns = 0, clear_return_ns = 0;
    RPV130_BUSY_TIMING writes = {};


};

static const char *V1720E_SETTINGS_PATH = "/Equipment/VME/Settings/V1720E";
static const char *V1720E_INFO_PATH = "/Equipment/VME/Info/V1720E";
static const char *V1720E_READBACK_PATH = "/Equipment/VME/Readback/V1720E";
static const char *V1720E_VARIABLES_PATH = "/Equipment/VME/Variables/V1720E";
static const char *BUFFER_CLEAR_COMMAND_PATH =
    "/Equipment/VME/Commands/BufferClearRequestId";
static const char *BUFFER_CLEAR_STATUS_PATH =
    "/Equipment/VME/Variables/BufferClear";

static const DWORD V1720E_TRIGGER_SOFTWARE = 0x80000000u;
static const DWORD V1720E_TRIGGER_EXTERNAL = 0x40000000u;
static const DWORD V1720E_TRIGGER_CHANNEL_MASK = 0x000000FFu;
static const DWORD V1720E_CHANNEL_CONFIG_ZS_MASK = 0x000F0000u;
static const DWORD V1720E_CHANNEL_CONFIG_PACK25 = 0x00000800u;
static const DWORD V1720E_ACQ_RUN = 0x00000004u;
static const DWORD V1720E_STATUS_EVENT_READY = 0x00000008u;
static const DWORD V1720E_STATUS_EXTERNAL_CLOCK = 0x00000020u;
static const DWORD V1720E_STATUS_PLL_OK = 0x00000080u;
static const DWORD V1720E_STATUS_BOARD_READY = 0x00000100u;

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

static V1720ESettings default_v1720e_settings()
{
    V1720ESettings settings = {};
    settings.enabled = TRUE;
    settings.buffer_organization = 0x0Au;
    settings.record_length_samples = V1720E_DEFAULT_RECORD_SAMPLES;
    settings.post_trigger = 0x30u;
    settings.software_trigger_enabled = TRUE;
    settings.external_trigger_enabled = TRUE;
    for (unsigned channel = 0; channel < V1720E_CHANNEL_COUNT; ++channel) {
        settings.channel_self_trigger_enabled[channel] = FALSE;
        settings.channel_enabled[channel] = TRUE;
        settings.dc_offset[channel] = 0x8000u;
    }
    return settings;
}

static V1720ESettings gV1720RunSettings = default_v1720e_settings();
static V1720E_CONFIG gV1720RunConfig = {};
static bool gV1720StartupEnabled = true;
static INT gV1720StartupRunState = -1;
static bool gV1720VariablesEnabled = true;
static DWORD gV1720ExpectedEventWords = V1720E_DEFAULT_EVENT_WORDS;
static DWORD gV1720ExpectedChannelMask = V1720E_DEFAULT_CHANNEL_MASK;
/* Change this source constant to BLT32 for the hardware comparison run. */
static const V1720E_READOUT_MODE V1720E_READOUT_MODE_SELECT = BLT32;
static const DWORD V1720E_VARIABLES_MIN_PUBLISH_INTERVAL_MS = 200;
static const DWORD V1720E_VARIABLES_HEARTBEAT_INTERVAL_MS = 1000;

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

static V1720ERuntimeState gV1720Runtime = {};
static DWORD gV1720LastVariablesPublish = 0;

static bool make_odb_path(char *path, size_t capacity, const char *base,
                          const char *name)
{
    const int length = snprintf(path, capacity, "%s/%s", base, name);
    return length >= 0 && static_cast<size_t>(length) < capacity;
}

static bool set_absolute_odb_value(const char *path, const void *value,
                                   INT size, INT count, DWORD type)
{
    HNDLE hDB = 0;
    cm_get_experiment_database(&hDB, NULL);
    const INT status = db_set_value(hDB, 0, path, value, size, count, type);
    if (status != DB_SUCCESS)
        cm_msg(MERROR, frontend_name, "Cannot update ODB %s: status %d",
               path, status);
    return status == DB_SUCCESS;
}

static bool ensure_odb_value(const char *path, const void *default_value,
                             INT size, INT count, DWORD type)
{
    HNDLE hDB = 0;
    HNDLE hKey = 0;
    cm_get_experiment_database(&hDB, NULL);
    const INT find_status = db_find_key(hDB, 0, path, &hKey);
    if (find_status == DB_SUCCESS)
        return true;
    if (find_status != DB_NO_KEY) {
        cm_msg(MERROR, frontend_name, "Cannot inspect ODB %s: status %d",
               path, find_status);
        return false;
    }
    return set_absolute_odb_value(path, default_value, size, count, type);
}

static bool get_absolute_odb_value(const char *path, void *value, INT size,
                                   DWORD type)
{
    HNDLE hDB = 0;
    cm_get_experiment_database(&hDB, NULL);
    INT actual_size = size;
    const INT status = db_get_value(hDB, 0, path, value, &actual_size,
                                    type, FALSE);
    if (status != DB_SUCCESS || actual_size != size) {
        cm_msg(MERROR, frontend_name,
               "Cannot read ODB %s: status %d size %d expected %d",
               path, status, actual_size, size);
        return false;
    }
    return true;
}

static bool publish_configuration_status(bool configuration_ok,
                                         INT run_number)
{
    const BOOL value = configuration_ok ? TRUE : FALSE;
    const time_t now = time(NULL);
    const uint64_t checked_unix =
        now < 0 ? 0 : static_cast<uint64_t>(now);
    char path[256];
    bool ok = true;

    /* Clear the gate first on negative updates. On positive updates, publish
     * the identifying metadata before opening the gate. */
    if (!configuration_ok) {
        ok = make_odb_path(path, sizeof(path), FRONTEND_VARIABLES_PATH,
                           "ConfigurationOK") &&
             set_absolute_odb_value(path, &value, sizeof(value), 1,
                                    TID_BOOL) && ok;
    }
    ok = make_odb_path(path, sizeof(path), FRONTEND_VARIABLES_PATH,
                       "ConfigurationCheckedUnix") &&
         set_absolute_odb_value(path, &checked_unix, sizeof(checked_unix), 1,
                                TID_QWORD) && ok;
    ok = make_odb_path(path, sizeof(path), FRONTEND_VARIABLES_PATH,
                       "ConfigurationRunNumber") &&
         set_absolute_odb_value(path, &run_number, sizeof(run_number), 1,
                                TID_INT) && ok;
    if (configuration_ok) {
        ok = make_odb_path(path, sizeof(path), FRONTEND_VARIABLES_PATH,
                           "ConfigurationOK") &&
             set_absolute_odb_value(path, &value, sizeof(value), 1,
                                    TID_BOOL) && ok;
    }
    return ok;
}

static void mark_configuration_failed(INT run_number)
{
    if (!publish_configuration_status(false, run_number))
        cm_msg(MERROR, frontend_name,
               "Cannot publish failed VME configuration status for run %d",
               run_number);
}

static const char *V792_SETTINGS_PATH = "/Equipment/VME/Settings/V792";
static const char *V792_INFO_PATH = "/Equipment/VME/Info/V792";
static const char *V792_READBACK_PATH = "/Equipment/VME/Readback/V792";
static const char *V792_VARIABLES_PATH = "/Equipment/VME/Variables/V792";
static const char *V1190_SETTINGS_PATH = "/Equipment/VME/Settings/V1190";
static const char *V1190_INFO_PATH = "/Equipment/VME/Info/V1190";
static const char *V1190_READBACK_PATH = "/Equipment/VME/Readback/V1190";
static const char *V1190_VARIABLES_PATH = "/Equipment/VME/Variables/V1190";
static const char *V775_SETTINGS_PATH = "/Equipment/VME/Settings/V775";
static const char *V775_INFO_PATH = "/Equipment/VME/Info/V775";
static const char *V775_READBACK_PATH = "/Equipment/VME/Readback/V775";
static const char *V775_VARIABLES_PATH = "/Equipment/VME/Variables/V775";
static const DWORD MODULE_VARIABLES_MIN_PUBLISH_INTERVAL_MS = 200;
static const DWORD MODULE_VARIABLES_HEARTBEAT_INTERVAL_MS = 1000;

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

static V792Settings default_v792_settings()
{
    V792Settings s = {};
    s.enabled = TRUE;
    s.iped = V792_RUN_IPED;
    s.zero_suppression_enabled = FALSE;
    s.all_trigger_enabled = FALSE;
    return s;
}

static V1190Settings default_v1190_settings()
{
    V1190Settings s = {};
    s.enabled = TRUE;
    s.trigger_matching_enabled = TRUE;
    s.window_width = V1190_RUN_WINDOW_WIDTH;
    s.window_offset = -12;
    s.extra_search_margin = V1190_RUN_EXTRA_MARGIN;
    s.reject_margin = V1190_RUN_REJECT_MARGIN;
    s.trigger_subtraction_enabled = FALSE;
    s.edge_mode = V1190_RUN_EDGE_MODE;
    s.resolution_ps = 100;
    s.dead_time_ns = 5;
    s.max_hits_per_event = -1;
    s.tdc_header_enabled = TRUE;
    s.empty_event_enabled = TRUE;
    s.event_fifo_enabled = FALSE;
    s.extended_trigger_time_enabled = FALSE;
    for (unsigned i = 0; i < 128; ++i)
        s.channel_enabled[i] = TRUE;
    return s;
}

static V775Settings default_v775_settings()
{
    V775Settings s = {};
    s.enabled = FALSE;
    s.full_scale_range = V775_RUN_FULL_SCALE;
    s.over_range_enabled = TRUE;
    s.low_threshold_enabled = TRUE;
    s.common_stop = TRUE;
    s.empty_program_enabled = TRUE;
    s.valid_control_enabled = FALSE;
    s.sliding_scale_enabled = FALSE;
    s.all_trigger_enabled = FALSE;
    return s;
}

static V792Settings gV792RunSettings = default_v792_settings();
static V1190Settings gV1190RunSettings = default_v1190_settings();
static V775Settings gV775RunSettings = default_v775_settings();
static VmeRunSnapshot gVmeRunSnapshot = {};
static V7xxRuntimeState gV792Runtime = {};
static V1190RuntimeState gV1190Runtime = {};
static V7xxRuntimeState gV775Runtime = {};
static DWORD gV792LastVariablesPublish = 0;
static DWORD gV1190LastVariablesPublish = 0;
static DWORD gV775LastVariablesPublish = 0;

struct VmeBufferClearResults {
    std::string v792 = "Not requested";
    std::string v1190 = "Not requested";
    std::string v775 = "Not requested";
    std::string v1720e = "Not requested";
};

static daq::BufferClearStatus gBufferClearStatus;
static VmeBufferClearResults gBufferClearResults;

static bool set_buffer_clear_string(const char *name, const std::string &value)
{
    char path[256];
    if (!make_odb_path(path, sizeof(path), BUFFER_CLEAR_STATUS_PATH, name))
        return false;
    char buffer[256] = {};
    snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    return set_absolute_odb_value(path, buffer, sizeof(buffer), 1, TID_STRING);
}

static bool publish_buffer_clear_status()
{
    char path[256];
    bool ok = true;
    const DWORD active = gBufferClearStatus.active_request_id;
    const DWORD handled = gBufferClearStatus.last_handled_request_id;
    const DWORD successful = gBufferClearStatus.last_successful_request_id;
    const BOOL in_progress = gBufferClearStatus.in_progress ? TRUE : FALSE;
    const BOOL last_succeeded =
        gBufferClearStatus.last_attempt_succeeded ? TRUE : FALSE;
#define PUBLISH_CLEAR_VALUE(name, value, type) \
    do { \
        ok = make_odb_path(path, sizeof(path), BUFFER_CLEAR_STATUS_PATH, name) && \
             set_absolute_odb_value(path, &(value), sizeof(value), 1, type) && ok; \
    } while (0)
    PUBLISH_CLEAR_VALUE("ActiveRequestId", active, TID_DWORD);
    PUBLISH_CLEAR_VALUE("LastHandledRequestId", handled, TID_DWORD);
    PUBLISH_CLEAR_VALUE("LastSuccessfulRequestId", successful, TID_DWORD);
    PUBLISH_CLEAR_VALUE("InProgress", in_progress, TID_BOOL);
    PUBLISH_CLEAR_VALUE("LastAttemptSucceeded", last_succeeded, TID_BOOL);
    PUBLISH_CLEAR_VALUE("LastClearUnixTime",
                        gBufferClearStatus.last_clear_unix_time, TID_QWORD);
#undef PUBLISH_CLEAR_VALUE
    ok = set_buffer_clear_string(
             "State", daq::bufferClearStateName(gBufferClearStatus.state)) && ok;
    ok = set_buffer_clear_string("LastError", gBufferClearStatus.last_error) && ok;
    ok = set_buffer_clear_string("V792Result", gBufferClearResults.v792) && ok;
    ok = set_buffer_clear_string("V1190Result", gBufferClearResults.v1190) && ok;
    ok = set_buffer_clear_string("V775Result", gBufferClearResults.v775) && ok;
    ok = set_buffer_clear_string("V1720EResult", gBufferClearResults.v1720e) && ok;
    return ok;
}

static bool initialize_buffer_clear_mailbox()
{
    const DWORD zero = 0;
    const BOOL no = FALSE;
    const uint64_t zero_time = 0;
    char path[256];
    char idle[32] = "Idle";
    char empty[256] = {};
#define ENSURE_CLEAR_VALUE(name, value, count, type) \
    do { \
        if (!make_odb_path(path, sizeof(path), BUFFER_CLEAR_STATUS_PATH, name) || \
            !ensure_odb_value(path, &(value), sizeof(value), count, type)) \
            return false; \
    } while (0)
    if (!ensure_odb_value(BUFFER_CLEAR_COMMAND_PATH, &zero, sizeof(zero), 1,
                          TID_DWORD))
        return false;
    ENSURE_CLEAR_VALUE("ActiveRequestId", zero, 1, TID_DWORD);
    ENSURE_CLEAR_VALUE("LastHandledRequestId", zero, 1, TID_DWORD);
    ENSURE_CLEAR_VALUE("LastSuccessfulRequestId", zero, 1, TID_DWORD);
    ENSURE_CLEAR_VALUE("InProgress", no, 1, TID_BOOL);
    ENSURE_CLEAR_VALUE("LastAttemptSucceeded", no, 1, TID_BOOL);
    ENSURE_CLEAR_VALUE("LastClearUnixTime", zero_time, 1, TID_QWORD);
    ENSURE_CLEAR_VALUE("State", idle, 1, TID_STRING);
    ENSURE_CLEAR_VALUE("LastError", empty, 1, TID_STRING);
    ENSURE_CLEAR_VALUE("V792Result", empty, 1, TID_STRING);
    ENSURE_CLEAR_VALUE("V1190Result", empty, 1, TID_STRING);
    ENSURE_CLEAR_VALUE("V775Result", empty, 1, TID_STRING);
    ENSURE_CLEAR_VALUE("V1720EResult", empty, 1, TID_STRING);
#undef ENSURE_CLEAR_VALUE

    DWORD request_id = 0, handled = 0, successful = 0;
    if (!get_absolute_odb_value(BUFFER_CLEAR_COMMAND_PATH, &request_id,
                                sizeof(request_id), TID_DWORD) ||
        !make_odb_path(path, sizeof(path), BUFFER_CLEAR_STATUS_PATH,
                       "LastHandledRequestId") ||
        !get_absolute_odb_value(path, &handled, sizeof(handled), TID_DWORD) ||
        !make_odb_path(path, sizeof(path), BUFFER_CLEAR_STATUS_PATH,
                       "LastSuccessfulRequestId") ||
        !get_absolute_odb_value(path, &successful, sizeof(successful), TID_DWORD))
        return false;
    gBufferClearStatus = {};
    gBufferClearStatus.last_handled_request_id = handled;
    gBufferClearStatus.last_successful_request_id = successful;
    if (request_id > handled) {
        const time_t now = time(NULL);
        gBufferClearStatus = daq::acknowledgeStaleBufferClearRequest(
            gBufferClearStatus, request_id,
            now < 0 ? 0 : static_cast<uint64_t>(now));
        gBufferClearResults = {};
        gBufferClearResults.v792 = gBufferClearResults.v1190 =
            gBufferClearResults.v775 = gBufferClearResults.v1720e =
                "Not executed: stale startup request";
        cm_msg(MINFO, frontend_name, "WARNING: %s (request %u)",
               gBufferClearStatus.last_error.c_str(), request_id);
    }
    return publish_buffer_clear_status();
}

static bool set_module_output(const char *base, const char *name,
                              const void *value, INT size, INT count,
                              DWORD type)
{
    char path[256];
    return make_odb_path(path, sizeof(path), base, name) &&
           set_absolute_odb_value(path, value, size, count, type);
}

static bool set_vme_snapshot_value(const char *relative_path,
                                   const void *value, INT size, INT count,
                                   DWORD type)
{
    char path[320];
    return make_odb_path(path, sizeof(path), VME_RUN_SNAPSHOT_PATH,
                         relative_path) &&
           set_absolute_odb_value(path, value, size, count, type);
}

static void format_iso8601_utc(time_t value, char *buffer, size_t capacity)
{
    struct tm utc = {};
    if (gmtime_r(&value, &utc) == NULL ||
        strftime(buffer, capacity, "%Y-%m-%dT%H:%M:%SZ", &utc) == 0)
        buffer[0] = '\0';
}

static void reset_vme_run_snapshot(INT run_number)
{
    gVmeRunSnapshot = {};
    gVmeRunSnapshot.schema_version = RUN_SNAPSHOT_SCHEMA_VERSION;
    gVmeRunSnapshot.run_number = run_number;
    const time_t now = time(NULL);
    gVmeRunSnapshot.bor_unix_time =
        now < 0 ? 0 : static_cast<uint64_t>(now);
    format_iso8601_utc(now, gVmeRunSnapshot.bor_time_iso8601,
                       sizeof(gVmeRunSnapshot.bor_time_iso8601));
    snprintf(gVmeRunSnapshot.frontend_name,
             sizeof(gVmeRunSnapshot.frontend_name), "%s", frontend_name);
    snprintf(gVmeRunSnapshot.snapshot_id,
             sizeof(gVmeRunSnapshot.snapshot_id), "run-%d_%llu_%s",
             run_number,
             static_cast<unsigned long long>(gVmeRunSnapshot.bor_unix_time),
             frontend_name);
    snprintf(gVmeRunSnapshot.v1190_readback.board_type,
             sizeof(gVmeRunSnapshot.v1190_readback.board_type), "Unknown");
}

static bool publish_vme_run_snapshot()
{
    bool ok = true;
#define SNAP(path, value, count, type) \
    do { \
        ok = set_vme_snapshot_value(path, &(value), sizeof(value), count, type) \
             && ok; \
    } while (0)
#define SNAP_STRING(path, value) \
    do { \
        ok = set_vme_snapshot_value(path, value, sizeof(value), 1, TID_STRING) \
             && ok; \
    } while (0)
    SNAP("Metadata/SchemaVersion", gVmeRunSnapshot.schema_version, 1,
         TID_DWORD);
    SNAP_STRING("Metadata/SnapshotId", gVmeRunSnapshot.snapshot_id);
    SNAP("Metadata/RunNumber", gVmeRunSnapshot.run_number, 1, TID_INT);
    SNAP("Metadata/BORUnixTime", gVmeRunSnapshot.bor_unix_time, 1,
         TID_QWORD);
    SNAP_STRING("Metadata/BORTimeISO8601",
                gVmeRunSnapshot.bor_time_iso8601);
    SNAP_STRING("Metadata/FrontendName", gVmeRunSnapshot.frontend_name);
    SNAP("Metadata/EnabledForRun", gVmeRunSnapshot.enabled_for_run, 1,
         TID_BOOL);

#define SNAP_V792(base, object) \
    SNAP(base "/Enabled", object.enabled, 1, TID_BOOL); \
    SNAP(base "/Iped", object.iped, 1, TID_WORD); \
    SNAP(base "/ZeroSuppressionEnabled", object.zero_suppression_enabled, 1, TID_BOOL); \
    SNAP(base "/AllTriggerEnabled", object.all_trigger_enabled, 1, TID_BOOL)
    SNAP_V792("Requested/V792", gVmeRunSnapshot.v792_requested);
#undef SNAP_V792

#define SNAP_V1190(base, object) \
    SNAP(base "/Enabled", object.enabled, 1, TID_BOOL); \
    SNAP(base "/TriggerMatchingEnabled", object.trigger_matching_enabled, 1, TID_BOOL); \
    SNAP(base "/WindowWidth", object.window_width, 1, TID_DWORD); \
    SNAP(base "/WindowOffset", object.window_offset, 1, TID_INT); \
    SNAP(base "/ExtraSearchMargin", object.extra_search_margin, 1, TID_DWORD); \
    SNAP(base "/RejectMargin", object.reject_margin, 1, TID_DWORD); \
    SNAP(base "/TriggerSubtractionEnabled", object.trigger_subtraction_enabled, 1, TID_BOOL); \
    SNAP(base "/EdgeMode", object.edge_mode, 1, TID_DWORD); \
    SNAP(base "/ResolutionPs", object.resolution_ps, 1, TID_DWORD); \
    SNAP(base "/DeadTimeNs", object.dead_time_ns, 1, TID_DWORD); \
    SNAP(base "/MaxHitsPerEvent", object.max_hits_per_event, 1, TID_INT); \
    SNAP(base "/TdcHeaderEnabled", object.tdc_header_enabled, 1, TID_BOOL); \
    SNAP(base "/EmptyEventEnabled", object.empty_event_enabled, 1, TID_BOOL); \
    SNAP(base "/EventFifoEnabled", object.event_fifo_enabled, 1, TID_BOOL); \
    SNAP(base "/ExtendedTriggerTimeEnabled", object.extended_trigger_time_enabled, 1, TID_BOOL); \
    SNAP(base "/ChannelEnabled", object.channel_enabled, 128, TID_BOOL)
    SNAP_V1190("Requested/V1190", gVmeRunSnapshot.v1190_requested);
#undef SNAP_V1190

#define SNAP_V775(base, object) \
    SNAP(base "/Enabled", object.enabled, 1, TID_BOOL); \
    SNAP(base "/FullScaleRange", object.full_scale_range, 1, TID_WORD); \
    SNAP(base "/OverRangeEnabled", object.over_range_enabled, 1, TID_BOOL); \
    SNAP(base "/LowThresholdEnabled", object.low_threshold_enabled, 1, TID_BOOL); \
    SNAP(base "/CommonStop", object.common_stop, 1, TID_BOOL); \
    SNAP(base "/EmptyProgramEnabled", object.empty_program_enabled, 1, TID_BOOL); \
    SNAP(base "/ValidControlEnabled", object.valid_control_enabled, 1, TID_BOOL); \
    SNAP(base "/SlidingScaleEnabled", object.sliding_scale_enabled, 1, TID_BOOL); \
    SNAP(base "/AllTriggerEnabled", object.all_trigger_enabled, 1, TID_BOOL)
    SNAP_V775("Requested/V775", gVmeRunSnapshot.v775_requested);
#undef SNAP_V775

    SNAP("Requested/V1720E/Enabled", gVmeRunSnapshot.v1720e_requested.enabled,
         1, TID_BOOL);
    SNAP("Requested/V1720E/BufferOrganization",
         gVmeRunSnapshot.v1720e_requested.buffer_organization, 1, TID_DWORD);
    SNAP("Requested/V1720E/RecordLengthSamples",
         gVmeRunSnapshot.v1720e_requested.record_length_samples, 1,
         TID_DWORD);
    SNAP("Requested/V1720E/PostTrigger",
         gVmeRunSnapshot.v1720e_requested.post_trigger, 1, TID_DWORD);
    SNAP("Requested/V1720E/SoftwareTriggerEnabled",
         gVmeRunSnapshot.v1720e_requested.software_trigger_enabled, 1,
         TID_BOOL);
    SNAP("Requested/V1720E/ExternalTriggerEnabled",
         gVmeRunSnapshot.v1720e_requested.external_trigger_enabled, 1,
         TID_BOOL);
    SNAP("Requested/V1720E/ChannelSelfTriggerEnabled",
         gVmeRunSnapshot.v1720e_requested.channel_self_trigger_enabled,
         V1720E_CHANNEL_COUNT, TID_BOOL);
    SNAP("Requested/V1720E/ChannelEnabled",
         gVmeRunSnapshot.v1720e_requested.channel_enabled,
         V1720E_CHANNEL_COUNT, TID_BOOL);
    SNAP("Requested/V1720E/DCOffset",
         gVmeRunSnapshot.v1720e_requested.dc_offset, V1720E_CHANNEL_COUNT,
         TID_WORD);
    SNAP("Requested/RPV130/Enabled", gVmeRunSnapshot.rpv130_enabled, 1,
         TID_BOOL);
    SNAP("Requested/RPV130/SingleEventBusyEnabled",
         gVmeRunSnapshot.rpv130_single_event_busy_enabled, 1, TID_BOOL);

    SNAP("Readback/V792/Valid", gVmeRunSnapshot.v792_readback.valid, 1,
         TID_BOOL);
    SNAP("Readback/V792/FirmwareRevision",
         gVmeRunSnapshot.v792_readback.firmware_revision, 1, TID_WORD);
    SNAP("Readback/V792/Iped", gVmeRunSnapshot.v792_readback.iped, 1,
         TID_WORD);
    SNAP("Readback/V792/ZeroSuppressionEnabled",
         gVmeRunSnapshot.v792_readback.zero_suppression_enabled, 1,
         TID_BOOL);
    SNAP("Readback/V792/AllTriggerEnabled",
         gVmeRunSnapshot.v792_readback.all_trigger_enabled, 1, TID_BOOL);
    SNAP("Readback/V792/BitSet2Raw",
         gVmeRunSnapshot.v792_readback.bit_set2_raw, 1, TID_WORD);
    SNAP("Readback/V792/Threshold",
         gVmeRunSnapshot.v792_readback.threshold, 32, TID_WORD);

    const V1190ReadbackSnapshot &r1190 = gVmeRunSnapshot.v1190_readback;
    SNAP("Readback/V1190/Valid", r1190.valid, 1, TID_BOOL);
    SNAP("Readback/V1190/FirmwareRevision", r1190.firmware_revision, 1,
         TID_WORD);
    SNAP("Readback/V1190/ConfigurationRomVersion",
         r1190.configuration_rom_version, 1, TID_WORD);
    SNAP_STRING("Readback/V1190/BoardType", r1190.board_type);
#define SNAP_R1190(name, member, count, type) \
    SNAP("Readback/V1190/" name, r1190.settings.member, count, type)
    SNAP_R1190("TriggerMatchingEnabled", trigger_matching_enabled, 1, TID_BOOL);
    SNAP_R1190("WindowWidth", window_width, 1, TID_DWORD);
    SNAP_R1190("WindowOffset", window_offset, 1, TID_INT);
    SNAP_R1190("ExtraSearchMargin", extra_search_margin, 1, TID_DWORD);
    SNAP_R1190("RejectMargin", reject_margin, 1, TID_DWORD);
    SNAP_R1190("TriggerSubtractionEnabled", trigger_subtraction_enabled, 1, TID_BOOL);
    SNAP_R1190("EdgeMode", edge_mode, 1, TID_DWORD);
    SNAP_R1190("ResolutionPs", resolution_ps, 1, TID_DWORD);
    SNAP_R1190("DeadTimeNs", dead_time_ns, 1, TID_DWORD);
    SNAP_R1190("MaxHitsPerEvent", max_hits_per_event, 1, TID_INT);
    SNAP_R1190("TdcHeaderEnabled", tdc_header_enabled, 1, TID_BOOL);
    SNAP_R1190("EmptyEventEnabled", empty_event_enabled, 1, TID_BOOL);
    SNAP_R1190("EventFifoEnabled", event_fifo_enabled, 1, TID_BOOL);
    SNAP_R1190("ExtendedTriggerTimeEnabled", extended_trigger_time_enabled, 1, TID_BOOL);
    SNAP_R1190("ChannelEnabled", channel_enabled, 128, TID_BOOL);
#undef SNAP_R1190
    SNAP("Readback/V1190/ErrorMask", r1190.error_mask, 1, TID_WORD);
    SNAP("Readback/V1190/EffectiveFifoSizeWords",
         r1190.effective_fifo_size_words, 1, TID_DWORD);
    SNAP("Readback/V1190/ControlRaw", r1190.control_raw, 1, TID_WORD);
    SNAP("Readback/V1190/POUTSelection", r1190.pout_selection, 1, TID_WORD);
    SNAP_STRING("Readback/V1190/POUTFunction", r1190.pout_function);
    SNAP("Readback/V1190/AlmostFullLevelWords",
         r1190.almost_full_level_words, 1, TID_WORD);

    const V775ReadbackSnapshot &r775 = gVmeRunSnapshot.v775_readback;
    SNAP("Readback/V775/Valid", r775.valid, 1, TID_BOOL);
    SNAP("Readback/V775/FirmwareRevision", r775.firmware_revision, 1,
         TID_WORD);
    SNAP("Readback/V775/FullScaleRange", r775.full_scale_range, 1,
         TID_WORD);
    SNAP("Readback/V775/FastClearWindow", r775.fast_clear_window, 1,
         TID_WORD);
#define SNAP_R775(name, member, type) \
    SNAP("Readback/V775/" name, r775.member, 1, type)
    SNAP_R775("OverRangeEnabled", over_range_enabled, TID_BOOL);
    SNAP_R775("LowThresholdEnabled", low_threshold_enabled, TID_BOOL);
    SNAP_R775("CommonStop", common_stop, TID_BOOL);
    SNAP_R775("EmptyProgramEnabled", empty_program_enabled, TID_BOOL);
    SNAP_R775("ValidControlEnabled", valid_control_enabled, TID_BOOL);
    SNAP_R775("SlidingScaleEnabled", sliding_scale_enabled, TID_BOOL);
    SNAP_R775("AllTriggerEnabled", all_trigger_enabled, TID_BOOL);
#undef SNAP_R775
    SNAP("Readback/V775/BitSet2Raw", r775.bit_set2_raw, 1, TID_WORD);
    SNAP("Readback/V775/Threshold", r775.threshold, 32, TID_WORD);

    const V1720EReadbackSnapshot &r1720 = gVmeRunSnapshot.v1720e_readback;
    SNAP("Readback/V1720E/Valid", r1720.valid, 1, TID_BOOL);
    SNAP("Readback/V1720E/BoardInfo", r1720.board_info, 1, TID_DWORD);
    SNAP("Readback/V1720E/RocFirmwareRevision",
         r1720.roc_firmware_revision, 1, TID_DWORD);
    SNAP("Readback/V1720E/BufferOrganization",
         r1720.buffer_organization, 1, TID_DWORD);
    SNAP("Readback/V1720E/CustomSizeRaw", r1720.custom_size_raw, 1,
         TID_DWORD);
    SNAP("Readback/V1720E/RecordLengthSamples",
         r1720.record_length_samples, 1, TID_DWORD);
    SNAP("Readback/V1720E/PostTrigger", r1720.post_trigger, 1, TID_DWORD);
    SNAP("Readback/V1720E/SoftwareTriggerEnabled",
         r1720.software_trigger_enabled, 1, TID_BOOL);
    SNAP("Readback/V1720E/ExternalTriggerEnabled",
         r1720.external_trigger_enabled, 1, TID_BOOL);
    SNAP("Readback/V1720E/ChannelSelfTriggerEnabled",
         r1720.channel_self_trigger_enabled, V1720E_CHANNEL_COUNT, TID_BOOL);
    SNAP("Readback/V1720E/ChannelEnabled", r1720.channel_enabled,
         V1720E_CHANNEL_COUNT, TID_BOOL);
    SNAP("Readback/V1720E/DCOffset", r1720.dc_offset,
         V1720E_CHANNEL_COUNT, TID_WORD);
    SNAP("Readback/V1720E/ZeroSuppressionEnabled",
         r1720.zero_suppression_enabled, 1, TID_BOOL);
    SNAP("Readback/V1720E/Pack25Enabled", r1720.pack25_enabled, 1,
         TID_BOOL);
    SNAP("Readback/V1720E/TriggerSourceRaw", r1720.trigger_source_raw, 1,
         TID_DWORD);
    SNAP("Readback/V1720E/ChannelEnableRaw", r1720.channel_enable_raw, 1,
         TID_DWORD);
    SNAP("Readback/V1720E/ChannelConfigRaw", r1720.channel_config_raw, 1,
         TID_DWORD);

    /* Publish the completion marker last. If any preceding write failed,
     * leave the fixed subtree explicitly incomplete. */
    {
        BOOL published_complete =
            (gVmeRunSnapshot.frontend_bor_complete && ok) ? TRUE : FALSE;
        const bool complete_ok = set_vme_snapshot_value(
            "Metadata/FrontendBORComplete", &published_complete,
            sizeof(published_complete), 1, TID_BOOL);
        ok = complete_ok && ok;
    }
#undef SNAP_STRING
#undef SNAP
    return ok;
}

static bool initialize_rpv130_odb()
{
    const BOOL default_enabled = TRUE;
    const BOOL default_single_event_busy = FALSE;
    const DWORD base_address = RPV130_BASE_ADDRESS;
    const char address_modifier[] = "A16_ND";
    const char register_width[] = "D16";
    const BOOL default_communication_ok = FALSE;
    const BYTE default_status = 0;
    char path[256];

    if (!make_odb_path(path, sizeof(path), RPV130_SETTINGS_PATH, "Enabled") ||
        !ensure_odb_value(path, &default_enabled, sizeof(default_enabled), 1,
                          TID_BOOL))
        return false;
    BOOL startup_enabled = FALSE;
    if (!get_absolute_odb_value(path, &startup_enabled,
                                sizeof(startup_enabled), TID_BOOL))
        return false;
    gRpv130EnabledForRun = startup_enabled != FALSE;
    if (!make_odb_path(path, sizeof(path), RPV130_SETTINGS_PATH,
                       "SingleEventBusyEnabled") ||
        !ensure_odb_value(path, &default_single_event_busy,
                          sizeof(default_single_event_busy), 1, TID_BOOL))
        return false;
    BOOL startup_busy_enabled = FALSE;
    if (!get_absolute_odb_value(path, &startup_busy_enabled,
                                sizeof(startup_busy_enabled), TID_BOOL))
        return false;
    gSingleEventBusyEnabledForRun = startup_busy_enabled != FALSE;
    if (!set_module_output(RPV130_STATUS_PATH, "Busy1",
                           &default_single_event_busy,
                           sizeof(default_single_event_busy), 1, TID_BOOL) ||
        !set_module_output(RPV130_STATUS_PATH, "SingleEventBusyArmed",
                           &default_single_event_busy,
                           sizeof(default_single_event_busy), 1, TID_BOOL))
        return false;

#define SET_RPV130_INFO(name, value, size, type) \
    do { \
        if (!make_odb_path(path, sizeof(path), RPV130_INFO_PATH, name) || \
            !set_absolute_odb_value(path, value, size, 1, type)) \
            return false; \
    } while (0)
    SET_RPV130_INFO("BaseAddress", &base_address, sizeof(base_address), TID_DWORD);
    SET_RPV130_INFO("AddressModifier", address_modifier,
                    sizeof(address_modifier), TID_STRING);
    SET_RPV130_INFO("RegisterDataWidth", register_width,
                    sizeof(register_width), TID_STRING);
#undef SET_RPV130_INFO

#define SET_RPV130_VARIABLE_DEFAULT(name, value, size, type) \
    do { \
        if (!make_odb_path(path, sizeof(path), RPV130_VARIABLES_PATH, name) || \
            !set_absolute_odb_value(path, value, size, 1, type)) \
            return false; \
    } while (0)
    SET_RPV130_VARIABLE_DEFAULT("CommunicationOK", &default_communication_ok,
                                sizeof(default_communication_ok), TID_BOOL);
    SET_RPV130_VARIABLE_DEFAULT("EnabledForRun", &default_communication_ok,
                                sizeof(default_communication_ok), TID_BOOL);
    SET_RPV130_VARIABLE_DEFAULT("Latch1", &default_status,
                                sizeof(default_status), TID_BYTE);
    SET_RPV130_VARIABLE_DEFAULT("Latch2", &default_status,
                                sizeof(default_status), TID_BYTE);
    SET_RPV130_VARIABLE_DEFAULT("RSFF", &default_status,
                                sizeof(default_status), TID_BYTE);
    SET_RPV130_VARIABLE_DEFAULT("Through", &default_status,
                                sizeof(default_status), TID_BYTE);
    SET_RPV130_VARIABLE_DEFAULT("CSR1", &default_status,
                                sizeof(default_status), TID_BYTE);
    SET_RPV130_VARIABLE_DEFAULT("CSR2", &default_status,
                                sizeof(default_status), TID_BYTE);
#undef SET_RPV130_VARIABLE_DEFAULT
    const BOOL enabled_for_run = gRpv130EnabledForRun ? TRUE : FALSE;
    if (!make_odb_path(path, sizeof(path), RPV130_VARIABLES_PATH,
                       "EnabledForRun") ||
        !set_absolute_odb_value(path, &enabled_for_run,
                                sizeof(enabled_for_run), 1, TID_BOOL))
        return false;
    return true;
}

static void publish_rpv130_disabled_state()
{
    const BOOL no = FALSE;
    const BYTE zero = 0;
    set_module_output(RPV130_VARIABLES_PATH, "EnabledForRun", &no,
                      sizeof(no), 1, TID_BOOL);
    set_module_output(RPV130_VARIABLES_PATH, "CommunicationOK", &no,
                      sizeof(no), 1, TID_BOOL);
    set_module_output(RPV130_VARIABLES_PATH, "Latch1", &zero,
                      sizeof(zero), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "Latch2", &zero,
                      sizeof(zero), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "RSFF", &zero,
                      sizeof(zero), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "Through", &zero,
                      sizeof(zero), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "CSR1", &zero,
                      sizeof(zero), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "CSR2", &zero,
                      sizeof(zero), 1, TID_BYTE);
}

static void fail_single_event_busy(const char *reason);

static void publish_rpv130_status(bool force)
{
    if ((!gRpv130EnabledForRun && !gSingleEventBusyEnabledForRun) || !gVme)
        return;
    const DWORD now = ss_millitime();
    if (!force &&
        static_cast<DWORD>(now - gRpv130LastPoll) < RPV130_POLL_PERIOD_MS)
        return;
    gRpv130LastPoll = now;

    RPV130_STATUS status = {};
    const INT read_result =
        rpv130_read_status(gVme, RPV130_BASE_ADDRESS, &status);
    const BOOL communication_ok = read_result == MVME_SUCCESS;
    if (!communication_ok) {
        status = {};
        cm_msg(MERROR, frontend_name,
               "RPV130 read-only status poll failed at base 0x%04X: status %d",
               RPV130_BASE_ADDRESS, read_result);
        if (gSingleEventBusyEnabledForRun &&
            global_busy::readout_allowed())
            fail_single_event_busy("RPV130 status/CSR1 poll failed");
    }

    set_module_output(RPV130_VARIABLES_PATH, "Latch1", &status.latch1,
                      sizeof(status.latch1), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "Latch2", &status.latch2,
                      sizeof(status.latch2), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "RSFF", &status.rsff,
                      sizeof(status.rsff), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "Through", &status.through,
                      sizeof(status.through), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "CSR1", &status.csr1,
                      sizeof(status.csr1), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "CSR2", &status.csr2,
                      sizeof(status.csr2), 1, TID_BYTE);
    const BOOL busy1 = (status.csr1 & RPV130_CSR1_BUSY1) ? TRUE : FALSE;
    const BOOL armed = communication_ok && gRpv130BusyConfigured &&
        (status.csr1 & RPV130_CSR1_CHANNEL1_ARMED) ==
            RPV130_CSR1_CHANNEL1_ARMED ? TRUE : FALSE;
    // Keep the last hardware-derived BUSY1 value when CSR1 cannot be read.
    if (communication_ok)
        set_module_output(RPV130_STATUS_PATH, "Busy1", &busy1,
                          sizeof(busy1), 1, TID_BOOL);
    set_module_output(RPV130_STATUS_PATH, "SingleEventBusyArmed", &armed,
                      sizeof(armed), 1, TID_BOOL);
    set_module_output(RPV130_VARIABLES_PATH, "CommunicationOK",
                      &communication_ok, sizeof(communication_ok), 1,
                      TID_BOOL);
    const BOOL enabled_for_run = TRUE;
    set_module_output(RPV130_VARIABLES_PATH, "EnabledForRun",
                      &enabled_for_run, sizeof(enabled_for_run), 1,
                      TID_BOOL);
}

static bool publish_rpv130_busy_state(bool busy, bool armed)
{
    const BOOL b = busy ? TRUE : FALSE;
    const BOOL a = armed ? TRUE : FALSE;
    return set_module_output(RPV130_STATUS_PATH, "Busy1", &b,
                             sizeof(b), 1, TID_BOOL) &&
           set_module_output(RPV130_STATUS_PATH, "SingleEventBusyArmed", &a,
                             sizeof(a), 1, TID_BOOL);
}

static void fail_single_event_busy(const char *reason)
{
    if (!gSingleEventBusyEnabledForRun) return;
    gReadoutFailed = true;
    global_busy::disable_readout();
    cm_msg(MERROR, frontend_name, "RPV130 Single Event BUSY held: %s", reason);
    if (!global_busy::set_global_busy(true))
        cm_msg(MERROR, frontend_name,
               "Cannot assert V3718 Global BUSY after RPV130/readout failure");
    const BOOL no = FALSE;
    set_module_output(RPV130_STATUS_PATH, "SingleEventBusyArmed", &no,
                      sizeof(no), 1, TID_BOOL);
}

static bool arm_rpv130_single_event_busy()
{
    if (!gSingleEventBusyEnabledForRun) return true;
    // A failed first write still needs a later cleanup attempt under Global BUSY.
    gRpv130BusyConfigured = true;
    if (!global_busy::set_global_busy(true)) {
        cm_msg(MERROR, frontend_name,
               "Cannot verify Global BUSY before arming RPV130 FIN1");
        return false;
    }
    uint8_t csr1 = 0;
    const int rc = rpv130_clear_busy1_and_rearm(
        gVme, RPV130_BASE_ADDRESS, &csr1);
    if (rc != MVME_SUCCESS || (csr1 & RPV130_CSR1_BUSY1)) {
        cm_msg(MERROR, frontend_name,
               "RPV130 FIN1 arm/CSR1 readback failed: status %d CSR1=0x%02X",
               rc, csr1);
        const BOOL no = FALSE;
        set_module_output(RPV130_STATUS_PATH, "SingleEventBusyArmed",
                          &no, sizeof(no), 1, TID_BOOL);
        return false;
    }
    if (!publish_rpv130_busy_state(false, true)) return false;
    cm_msg(MINFO, frontend_name,
           "RPV130 Single Event BUSY armed: FIN1->BOUT1, CSR1=0x%02X",
           csr1);
    return true;
}

static bool quiesce_rpv130_single_event_busy(const char *context)
{
    if (!gSingleEventBusyEnabledForRun || !gVme) return true;
    // Never release BUSY1 before OUT0 has been asserted and verified.
    if (!global_busy::set_global_busy(true)) {
        cm_msg(MERROR, frontend_name,
               "RPV130 %s: Global BUSY assertion failed; BUSY1 left untouched",
               context);
        return false;
    }
    uint8_t csr1 = 0;
    const int rc = rpv130_clear_busy1_and_disable(
        gVme, RPV130_BASE_ADDRESS, &csr1);
    if (rc != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name,
               "RPV130 %s: CLR1/disable/readback failed: status %d CSR1=0x%02X",
               context, rc, csr1);
        const BOOL no = FALSE;
        set_module_output(RPV130_STATUS_PATH, "SingleEventBusyArmed",
                          &no, sizeof(no), 1, TID_BOOL);
        return false;
    }
    gRpv130BusyConfigured = false;
    return publish_rpv130_busy_state(false, false);
}

static bool publish_module_info(const char *path, DWORD base_address,
                                const char *address_modifier,
                                const char *register_width,
                                const char *event_width)
{
    return set_module_output(path, "BaseAddress", &base_address,
                             sizeof(base_address), 1, TID_DWORD) &&
           set_module_output(path, "AddressModifier", address_modifier,
                             strlen(address_modifier) + 1, 1, TID_STRING) &&
           set_module_output(path, "RegisterDataWidth", register_width,
                             strlen(register_width) + 1, 1, TID_STRING) &&
           set_module_output(path, "EventDataWidth", event_width,
                             strlen(event_width) + 1, 1, TID_STRING);
}

static bool ensure_v792_settings_schema()
{
    const V792Settings d = default_v792_settings();
    char p[256];
#define E792(n, m, t) do { if (!make_odb_path(p,sizeof(p),V792_SETTINGS_PATH,n) || !ensure_odb_value(p,&d.m,sizeof(d.m),1,t)) return false; } while (0)
    E792("Enabled", enabled, TID_BOOL);
    E792("Iped", iped, TID_WORD);
    E792("ZeroSuppressionEnabled", zero_suppression_enabled, TID_BOOL);
    E792("AllTriggerEnabled", all_trigger_enabled, TID_BOOL);
#undef E792
    return true;
}

static bool ensure_v1190_settings_schema()
{
    const V1190Settings d = default_v1190_settings();
    char p[256];
#define E1190(n, m, c, t) do { if (!make_odb_path(p,sizeof(p),V1190_SETTINGS_PATH,n) || !ensure_odb_value(p,&d.m,sizeof(d.m),c,t)) return false; } while (0)
    E1190("Enabled", enabled, 1, TID_BOOL);
    E1190("TriggerMatchingEnabled", trigger_matching_enabled, 1, TID_BOOL);
    E1190("WindowWidth", window_width, 1, TID_DWORD);
    E1190("WindowOffset", window_offset, 1, TID_INT);
    E1190("ExtraSearchMargin", extra_search_margin, 1, TID_DWORD);
    E1190("RejectMargin", reject_margin, 1, TID_DWORD);
    E1190("TriggerSubtractionEnabled", trigger_subtraction_enabled, 1, TID_BOOL);
    E1190("EdgeMode", edge_mode, 1, TID_DWORD);
    E1190("ResolutionPs", resolution_ps, 1, TID_DWORD);
    E1190("DeadTimeNs", dead_time_ns, 1, TID_DWORD);
    E1190("MaxHitsPerEvent", max_hits_per_event, 1, TID_INT);
    E1190("TdcHeaderEnabled", tdc_header_enabled, 1, TID_BOOL);
    E1190("EmptyEventEnabled", empty_event_enabled, 1, TID_BOOL);
    E1190("EventFifoEnabled", event_fifo_enabled, 1, TID_BOOL);
    E1190("ExtendedTriggerTimeEnabled", extended_trigger_time_enabled, 1, TID_BOOL);
    E1190("ChannelEnabled", channel_enabled, 128, TID_BOOL);
#undef E1190
    return true;
}

static bool ensure_v775_settings_schema()
{
    const V775Settings d = default_v775_settings();
    char p[256];
#define E775(n, m, t) do { if (!make_odb_path(p,sizeof(p),V775_SETTINGS_PATH,n) || !ensure_odb_value(p,&d.m,sizeof(d.m),1,t)) return false; } while (0)
    E775("Enabled", enabled, TID_BOOL);
    E775("FullScaleRange", full_scale_range, TID_WORD);
    E775("OverRangeEnabled", over_range_enabled, TID_BOOL);
    E775("LowThresholdEnabled", low_threshold_enabled, TID_BOOL);
    E775("CommonStop", common_stop, TID_BOOL);
    E775("EmptyProgramEnabled", empty_program_enabled, TID_BOOL);
    E775("ValidControlEnabled", valid_control_enabled, TID_BOOL);
    E775("SlidingScaleEnabled", sliding_scale_enabled, TID_BOOL);
    E775("AllTriggerEnabled", all_trigger_enabled, TID_BOOL);
#undef E775
    return true;
}

static bool read_v792_settings(V792Settings &s)
{
    V792Settings n = {}; char p[256];
#define R792(k,m,t) do { if (!make_odb_path(p,sizeof(p),V792_SETTINGS_PATH,k) || !get_absolute_odb_value(p,&n.m,sizeof(n.m),t)) return false; } while (0)
    R792("Enabled",enabled,TID_BOOL); R792("Iped",iped,TID_WORD);
    R792("ZeroSuppressionEnabled",zero_suppression_enabled,TID_BOOL);
    R792("AllTriggerEnabled",all_trigger_enabled,TID_BOOL);
#undef R792
    s=n; return true;
}

static bool read_v1190_settings(V1190Settings &s)
{
    V1190Settings n = {}; char p[256];
#define R1190(k,m,t) do { if (!make_odb_path(p,sizeof(p),V1190_SETTINGS_PATH,k) || !get_absolute_odb_value(p,&n.m,sizeof(n.m),t)) return false; } while (0)
    R1190("Enabled",enabled,TID_BOOL); R1190("TriggerMatchingEnabled",trigger_matching_enabled,TID_BOOL);
    R1190("WindowWidth",window_width,TID_DWORD); R1190("WindowOffset",window_offset,TID_INT);
    R1190("ExtraSearchMargin",extra_search_margin,TID_DWORD); R1190("RejectMargin",reject_margin,TID_DWORD);
    R1190("TriggerSubtractionEnabled",trigger_subtraction_enabled,TID_BOOL); R1190("EdgeMode",edge_mode,TID_DWORD);
    R1190("ResolutionPs",resolution_ps,TID_DWORD); R1190("DeadTimeNs",dead_time_ns,TID_DWORD);
    R1190("MaxHitsPerEvent",max_hits_per_event,TID_INT); R1190("TdcHeaderEnabled",tdc_header_enabled,TID_BOOL);
    R1190("EmptyEventEnabled",empty_event_enabled,TID_BOOL); R1190("EventFifoEnabled",event_fifo_enabled,TID_BOOL);
    R1190("ExtendedTriggerTimeEnabled",extended_trigger_time_enabled,TID_BOOL); R1190("ChannelEnabled",channel_enabled,TID_BOOL);
#undef R1190
    s=n; return true;
}

static bool read_v775_settings(V775Settings &s)
{
    V775Settings n = {}; char p[256];
#define R775(k,m,t) do { if (!make_odb_path(p,sizeof(p),V775_SETTINGS_PATH,k) || !get_absolute_odb_value(p,&n.m,sizeof(n.m),t)) return false; } while (0)
    R775("Enabled",enabled,TID_BOOL); R775("FullScaleRange",full_scale_range,TID_WORD);
    R775("OverRangeEnabled",over_range_enabled,TID_BOOL); R775("LowThresholdEnabled",low_threshold_enabled,TID_BOOL);
    R775("CommonStop",common_stop,TID_BOOL); R775("EmptyProgramEnabled",empty_program_enabled,TID_BOOL);
    R775("ValidControlEnabled",valid_control_enabled,TID_BOOL); R775("SlidingScaleEnabled",sliding_scale_enabled,TID_BOOL);
    R775("AllTriggerEnabled",all_trigger_enabled,TID_BOOL);
#undef R775
    s=n; return true;
}

static void decode_v7xx_runtime(V7xxRuntimeState &r, WORD s1, WORD s2,
                                DWORD counter)
{
    r.status1=s1; r.status2=s2; r.data_ready=!!(s1&0x1);
    r.busy=!!(s1&0x4); r.buffer_empty=!!(s2&0x2);
    r.buffer_full=!!(s2&0x4); r.event_counter=counter; r.dirty=true;
}

static void decode_v1190_runtime(WORD status, DWORD stored, DWORD counter)
{
    gV1190Runtime.status=status;
    gV1190Runtime.data_ready=!!(status&V1190_STATUS_DATA_READY);
    gV1190Runtime.almost_full=!!(status&V1190_STATUS_ALMOST_FULL);
    gV1190Runtime.full=!!(status&V1190_STATUS_FULL);
    gV1190Runtime.trigger_matching=!!(status&V1190_STATUS_TRIGGER_MATCH);
    gV1190Runtime.event_stored=stored;
    gV1190Runtime.event_counter=counter;
    gV1190Runtime.dirty=true;
}

static bool publish_v7xx_variables(const char *path, V7xxRuntimeState &r,
                                   DWORD &last)
{
    bool ok=true;
#define PV7(k,m,t) do { ok=set_module_output(path,k,&r.m,sizeof(r.m),1,t)&&ok; } while(0)
    PV7("EnabledForRun",enabled_for_run,TID_BOOL);
    PV7("CommunicationOK",communication_ok,TID_BOOL); PV7("Status1",status1,TID_WORD);
    PV7("Status2",status2,TID_WORD); PV7("DataReady",data_ready,TID_BOOL);
    PV7("Busy",busy,TID_BOOL); PV7("BufferEmpty",buffer_empty,TID_BOOL);
    PV7("BufferFull",buffer_full,TID_BOOL); PV7("EventCounter",event_counter,TID_DWORD);
#undef PV7
    r.dirty=!ok; last=ss_millitime(); return ok;
}

static bool publish_v1190_variables()
{
    bool ok=true;
#define PV1190(k,m,t) do { ok=set_module_output(V1190_VARIABLES_PATH,k,&gV1190Runtime.m,sizeof(gV1190Runtime.m),1,t)&&ok; } while(0)
    PV1190("EnabledForRun",enabled_for_run,TID_BOOL);
    PV1190("CommunicationOK",communication_ok,TID_BOOL); PV1190("Status",status,TID_WORD);
    PV1190("DataReady",data_ready,TID_BOOL); PV1190("AlmostFull",almost_full,TID_BOOL);
    PV1190("Full",full,TID_BOOL); PV1190("TriggerMatching",trigger_matching,TID_BOOL);
    PV1190("EventStored",event_stored,TID_DWORD); PV1190("EventCounter",event_counter,TID_DWORD);
#undef PV1190
    gV1190Runtime.dirty=!ok; gV1190LastVariablesPublish=ss_millitime(); return ok;
}

static void set_module_readback_valid(const char *path, bool valid)
{
    const BOOL v=valid?TRUE:FALSE;
    set_module_output(path,"Valid",&v,sizeof(v),1,TID_BOOL);
}

static void initialize_module_output_schema()
{
    WORD zword=0, thresholds[32]={}; DWORD zdword=0; BOOL zbool=FALSE;
    set_module_readback_valid(V792_READBACK_PATH,false);
    set_module_output(V792_READBACK_PATH,"FirmwareRevision",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V792_READBACK_PATH,"Iped",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V792_READBACK_PATH,"ZeroSuppressionEnabled",&zbool,sizeof(zbool),1,TID_BOOL);
    set_module_output(V792_READBACK_PATH,"AllTriggerEnabled",&zbool,sizeof(zbool),1,TID_BOOL);
    set_module_output(V792_READBACK_PATH,"BitSet2Raw",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V792_READBACK_PATH,"Threshold",thresholds,sizeof(thresholds),32,TID_WORD);

    set_module_readback_valid(V1190_READBACK_PATH,false);
    const char empty_board[]="Unknown";
    set_module_output(V1190_READBACK_PATH,"FirmwareRevision",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V1190_READBACK_PATH,"ConfigurationRomVersion",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V1190_READBACK_PATH,"BoardType",empty_board,sizeof(empty_board),1,TID_STRING);
    const V1190Settings d1190={};
#define Z1190(k,m,c,t) set_module_output(V1190_READBACK_PATH,k,&d1190.m,sizeof(d1190.m),c,t)
    Z1190("TriggerMatchingEnabled",trigger_matching_enabled,1,TID_BOOL);
    Z1190("WindowWidth",window_width,1,TID_DWORD); Z1190("WindowOffset",window_offset,1,TID_INT);
    Z1190("ExtraSearchMargin",extra_search_margin,1,TID_DWORD); Z1190("RejectMargin",reject_margin,1,TID_DWORD);
    Z1190("TriggerSubtractionEnabled",trigger_subtraction_enabled,1,TID_BOOL); Z1190("EdgeMode",edge_mode,1,TID_DWORD);
    Z1190("ResolutionPs",resolution_ps,1,TID_DWORD); Z1190("DeadTimeNs",dead_time_ns,1,TID_DWORD);
    Z1190("MaxHitsPerEvent",max_hits_per_event,1,TID_INT); Z1190("TdcHeaderEnabled",tdc_header_enabled,1,TID_BOOL);
    Z1190("EmptyEventEnabled",empty_event_enabled,1,TID_BOOL); Z1190("EventFifoEnabled",event_fifo_enabled,1,TID_BOOL);
    Z1190("ExtendedTriggerTimeEnabled",extended_trigger_time_enabled,1,TID_BOOL); Z1190("ChannelEnabled",channel_enabled,128,TID_BOOL);
#undef Z1190
    set_module_output(V1190_READBACK_PATH,"ErrorMask",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V1190_READBACK_PATH,"EffectiveFifoSizeWords",&zdword,sizeof(zdword),1,TID_DWORD);
    set_module_output(V1190_READBACK_PATH,"ControlRaw",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V1190_READBACK_PATH,"POUTSelection",&zword,sizeof(zword),1,TID_WORD);
    const char unknown_pout[]="Unknown";
    set_module_output(V1190_READBACK_PATH,"POUTFunction",unknown_pout,sizeof(unknown_pout),1,TID_STRING);
    set_module_output(V1190_READBACK_PATH,"AlmostFullLevelWords",&zword,sizeof(zword),1,TID_WORD);

    set_module_readback_valid(V775_READBACK_PATH,false);
    set_module_output(V775_READBACK_PATH,"FirmwareRevision",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V775_READBACK_PATH,"FullScaleRange",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V775_READBACK_PATH,"FastClearWindow",&zword,sizeof(zword),1,TID_WORD);
    const V775Settings d775={};
#define Z775(k,m,t) set_module_output(V775_READBACK_PATH,k,&d775.m,sizeof(d775.m),1,t)
    Z775("OverRangeEnabled",over_range_enabled,TID_BOOL); Z775("LowThresholdEnabled",low_threshold_enabled,TID_BOOL);
    Z775("CommonStop",common_stop,TID_BOOL); Z775("EmptyProgramEnabled",empty_program_enabled,TID_BOOL);
    Z775("ValidControlEnabled",valid_control_enabled,TID_BOOL); Z775("SlidingScaleEnabled",sliding_scale_enabled,TID_BOOL);
    Z775("AllTriggerEnabled",all_trigger_enabled,TID_BOOL);
#undef Z775
    set_module_output(V775_READBACK_PATH,"BitSet2Raw",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V775_READBACK_PATH,"Threshold",thresholds,sizeof(thresholds),32,TID_WORD);
    gV792Runtime={}; gV1190Runtime={}; gV775Runtime={};
    gV792Runtime.dirty=gV1190Runtime.dirty=gV775Runtime.dirty=true;
    publish_v7xx_variables(V792_VARIABLES_PATH,gV792Runtime,gV792LastVariablesPublish);
    publish_v1190_variables();
    publish_v7xx_variables(V775_VARIABLES_PATH,gV775Runtime,gV775LastVariablesPublish);
}

static bool initialize_other_module_odb()
{
    if (!ensure_v792_settings_schema() || !ensure_v1190_settings_schema() ||
        !ensure_v775_settings_schema()) return false;
    if (!publish_module_info(V792_INFO_PATH,V792_BASE,"A24_ND","D16","D32") ||
        !publish_module_info(V1190_INFO_PATH,V1190_BASE,"A24_ND","D16","D32") ||
        !publish_module_info(V775_INFO_PATH,V775_BASE,"A24_ND","D16","D32")) return false;
    initialize_module_output_schema();
    V792Settings a={}; V1190Settings b={}; V775Settings c={};
    if (!read_v792_settings(a)||!read_v1190_settings(b)||!read_v775_settings(c)) return false;
    gV792RunSettings=a; gV1190RunSettings=b; gV775RunSettings=c;
    return true;
}

static bool ensure_v1720e_settings_schema()
{
    const V1720ESettings defaults = default_v1720e_settings();
    char path[256];
#define ENSURE_SETTING(name, member, count, type) \
    do { \
        if (!make_odb_path(path, sizeof(path), V1720E_SETTINGS_PATH, name) || \
            !ensure_odb_value(path, &defaults.member, sizeof(defaults.member), \
                              count, type)) return false; \
    } while (0)
    ENSURE_SETTING("Enabled", enabled, 1, TID_BOOL);
    ENSURE_SETTING("BufferOrganization", buffer_organization, 1, TID_DWORD);
    ENSURE_SETTING("RecordLengthSamples", record_length_samples, 1, TID_DWORD);
    ENSURE_SETTING("PostTrigger", post_trigger, 1, TID_DWORD);
    ENSURE_SETTING("SoftwareTriggerEnabled", software_trigger_enabled, 1, TID_BOOL);
    ENSURE_SETTING("ExternalTriggerEnabled", external_trigger_enabled, 1, TID_BOOL);
    ENSURE_SETTING("ChannelSelfTriggerEnabled", channel_self_trigger_enabled,
                   V1720E_CHANNEL_COUNT, TID_BOOL);
    ENSURE_SETTING("ChannelEnabled", channel_enabled,
                   V1720E_CHANNEL_COUNT, TID_BOOL);
    ENSURE_SETTING("DCOffset", dc_offset, V1720E_CHANNEL_COUNT, TID_WORD);
#undef ENSURE_SETTING
    return true;
}

static bool read_v1720e_settings(V1720ESettings &settings)
{
    V1720ESettings next = {};
    char path[256];
#define READ_SETTING(name, member, type) \
    do { \
        if (!make_odb_path(path, sizeof(path), V1720E_SETTINGS_PATH, name) || \
            !get_absolute_odb_value(path, &next.member, sizeof(next.member), \
                                    type)) return false; \
    } while (0)
    READ_SETTING("Enabled", enabled, TID_BOOL);
    READ_SETTING("BufferOrganization", buffer_organization, TID_DWORD);
    READ_SETTING("RecordLengthSamples", record_length_samples, TID_DWORD);
    READ_SETTING("PostTrigger", post_trigger, TID_DWORD);
    READ_SETTING("SoftwareTriggerEnabled", software_trigger_enabled, TID_BOOL);
    READ_SETTING("ExternalTriggerEnabled", external_trigger_enabled, TID_BOOL);
    READ_SETTING("ChannelSelfTriggerEnabled", channel_self_trigger_enabled, TID_BOOL);
    READ_SETTING("ChannelEnabled", channel_enabled, TID_BOOL);
    READ_SETTING("DCOffset", dc_offset, TID_WORD);
#undef READ_SETTING
    settings = next;
    return true;
}

static bool publish_v1720e_info()
{
    const DWORD base_address = V1720E_BASE_ADDRESS;
    const char address_modifier[] = "A32_ND";
    const char register_width[] = "D32";
    const char event_width[] = "D32";
    char path[256];
#define SET_INFO(name, value, size, count, type) \
    do { \
        if (!make_odb_path(path, sizeof(path), V1720E_INFO_PATH, name) || \
            !set_absolute_odb_value(path, value, size, count, type)) \
            return false; \
    } while (0)
    SET_INFO("BaseAddress", &base_address, sizeof(base_address), 1, TID_DWORD);
    SET_INFO("AddressModifier", address_modifier, sizeof(address_modifier), 1, TID_STRING);
    SET_INFO("RegisterDataWidth", register_width, sizeof(register_width), 1, TID_STRING);
    SET_INFO("EventDataWidth", event_width, sizeof(event_width), 1, TID_STRING);
#undef SET_INFO
    return true;
}

static bool set_v1720e_output(const char *base, const char *name,
                              const void *value, INT size, INT count,
                              DWORD type)
{
    char path[256];
    return make_odb_path(path, sizeof(path), base, name) &&
           set_absolute_odb_value(path, value, size, count, type);
}

static void set_v1720e_readback_valid(bool valid)
{
    const BOOL value = valid ? TRUE : FALSE;
    set_v1720e_output(V1720E_READBACK_PATH, "Valid", &value, sizeof(value),
                      1, TID_BOOL);
}

static void update_v1720e_acquisition_status(DWORD status)
{
    gV1720Runtime.acquisition_status = status;
    gV1720Runtime.running = (status & V1720E_ACQ_RUN) != 0;
    gV1720Runtime.event_ready =
        (status & V1720E_STATUS_EVENT_READY) != 0;
    gV1720Runtime.external_clock =
        (status & V1720E_STATUS_EXTERNAL_CLOCK) != 0;
    gV1720Runtime.pll_locked = (status & V1720E_STATUS_PLL_OK) != 0;
    gV1720Runtime.board_ready = (status & V1720E_STATUS_BOARD_READY) != 0;
    gV1720Runtime.dirty = true;
}

static void update_v1720e_board_state(const V1720E_BOARD_INFO &info)
{
    gV1720Runtime.acquisition_control = info.acquisition_control;
    update_v1720e_acquisition_status(info.acquisition_status);
    gV1720Runtime.event_stored = info.event_stored;
    gV1720Runtime.dirty = true;
}

static bool publish_v1720e_variables()
{
    bool ok = true;
#define PUBLISH_VARIABLE(name, member, type) \
    do { \
        ok = set_v1720e_output(V1720E_VARIABLES_PATH, name, \
                               &gV1720Runtime.member, \
                               sizeof(gV1720Runtime.member), 1, type) && ok; \
    } while (0)
    PUBLISH_VARIABLE("EnabledForRun", enabled_for_run, TID_BOOL);
    PUBLISH_VARIABLE("CommunicationOK", communication_ok, TID_BOOL);
    PUBLISH_VARIABLE("AcquisitionControl", acquisition_control, TID_DWORD);
    PUBLISH_VARIABLE("AcquisitionStatus", acquisition_status, TID_DWORD);
    PUBLISH_VARIABLE("Running", running, TID_BOOL);
    PUBLISH_VARIABLE("EventReady", event_ready, TID_BOOL);
    PUBLISH_VARIABLE("ExternalClock", external_clock, TID_BOOL);
    PUBLISH_VARIABLE("PllLocked", pll_locked, TID_BOOL);
    PUBLISH_VARIABLE("BoardReady", board_ready, TID_BOOL);
    PUBLISH_VARIABLE("EventStored", event_stored, TID_DWORD);
    PUBLISH_VARIABLE("EventCounter", event_counter, TID_DWORD);
    PUBLISH_VARIABLE("TriggerTimeTag", trigger_time_tag, TID_DWORD);
#undef PUBLISH_VARIABLE
    gV1720Runtime.dirty = !ok;
    gV1720LastVariablesPublish = ss_millitime();
    return ok;
}

static void set_v1720e_communication_ok(bool ok)
{
    gV1720Runtime.communication_ok = ok ? TRUE : FALSE;
    gV1720Runtime.dirty = true;
    publish_v1720e_variables();
}

static void publish_v1720e_board_state(const V1720E_BOARD_INFO &info)
{
    update_v1720e_board_state(info);
    publish_v1720e_variables();
}

static void publish_v1720e_readback(const V1720E_CONFIG_READBACK &readback,
                                    bool valid);

static void initialize_v1720e_output_schema()
{
    const V1720E_CONFIG_READBACK empty_readback = {};
    publish_v1720e_readback(empty_readback, false);
    gV1720Runtime = {};
    gV1720Runtime.dirty = true;
    publish_v1720e_variables();
}

static bool initialize_v1720e_odb()
{
    if (!ensure_v1720e_settings_schema() || !publish_v1720e_info())
        return false;
    initialize_v1720e_output_schema();
    V1720ESettings startup_settings = {};
    if (!read_v1720e_settings(startup_settings))
        return false;
    gV1720StartupEnabled = startup_settings.enabled != FALSE;
    gV1720VariablesEnabled = gV1720StartupEnabled;
    return true;
}

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

static RunStatistics gRunStatistics = {};
static bool gRunCountersDirty = true;
static DWORD gRunCountersLastPublish = 0;

static bool publish_run_counters()
{
    bool ok = true;
#define PUBLISH_RUN_COUNTER(name, member, type) \
    do { \
        ok = set_module_output(RUN_COUNTERS_PATH, name, \
                               &gRunStatistics.member, \
                               sizeof(gRunStatistics.member), 1, type) && ok; \
    } while (0)
    PUBLISH_RUN_COUNTER("EventSlipCount", counter_mismatch_count, TID_QWORD);
    PUBLISH_RUN_COUNTER("V1720EMalformedEventCount", v1720_malformed_count,
                        TID_QWORD);
    PUBLISH_RUN_COUNTER("V1720ESizeErrorCount", v1720_size_error_count,
                        TID_QWORD);
    PUBLISH_RUN_COUNTER("V1720EChannelMaskErrorCount", v1720_mask_error_count,
                        TID_QWORD);
    PUBLISH_RUN_COUNTER("V1720EReadTimeoutCount", v1720_read_timeout_count,
                        TID_QWORD);
    PUBLISH_RUN_COUNTER("V1720ECounterDiscontinuityCount",
                        v1720_counter_discontinuity_count, TID_QWORD);
    PUBLISH_RUN_COUNTER("FirstEventSlipSerial", first_mismatch_serial,
                        TID_DWORD);
    PUBLISH_RUN_COUNTER("LastEventSlipSerial", last_mismatch_serial,
                        TID_DWORD);
#undef PUBLISH_RUN_COUNTER
    gRunCountersDirty = !ok;
    gRunCountersLastPublish = ss_millitime();
    return ok;
}

static void mark_run_counters_dirty()
{
    gRunCountersDirty = true;
}

static bool initialize_run_counters_odb()
{
    return publish_run_counters();
}

#if ENABLE_V1190_SOFT_TRIGGER_TEST
static WORD gV1190SavedControl = 0;
static bool gV1190DiagnosticSaved = false;
static bool gV1190ModeMayHaveChanged = false;
static bool gV1190EmptyEventMayHaveChanged = false;
#endif

#if ENABLE_V775_SW_TRIGGER_TEST
static WORD gV775SavedBitSet2 = 0;
static bool gV775DiagnosticSaved = false;
static bool gV775EmptyProgramMayHaveChanged = false;
#endif

struct V792EventInfo {
    size_t words;
    DWORD event_counter;
    unsigned expected_measurements;
    unsigned measurements;
    unsigned geo;
    bool valid;
};

struct V1190EventInfo {
    size_t words;
    DWORD event_counter;
    unsigned trailer_word_count;
    bool valid;
};

struct V775EventInfo {
    size_t words;
    DWORD event_counter;
    unsigned expected_measurements;
    unsigned measurements;
    unsigned geo;
    bool valid;
};

static bool gV1720Started = false;
/*
 * Set before issuing the V1720E RUN request. A failed start can mean that the
 * write reached the module but a subsequent verification read failed, so an
 * attempted start must be rolled back even when gV1720Started is still false.
 */
static bool gV1720StartAttempted = false;

enum class V1720StopOutcome { Disabled, AlreadyStopped, StopVerified };

static const char *v1720_stop_outcome_name(V1720StopOutcome outcome)
{
    switch (outcome) {
    case V1720StopOutcome::Disabled: return "disabled (not checked)";
    case V1720StopOutcome::AlreadyStopped: return "already stopped (no write)";
    case V1720StopOutcome::StopVerified: return "stop write and readback verified";
    }
    return "unknown";
}

static void invalidate_v1720e_current_state()
{
    gV1720Runtime.communication_ok = FALSE;
    gV1720Runtime.acquisition_control = 0;
    gV1720Runtime.acquisition_status = 0;
    gV1720Runtime.running = FALSE;
    gV1720Runtime.event_ready = FALSE;
    gV1720Runtime.external_clock = FALSE;
    gV1720Runtime.pll_locked = FALSE;
    gV1720Runtime.board_ready = FALSE;
    gV1720Runtime.event_stored = 0;
    gV1720Runtime.dirty = true;
}

static bool stop_v1720e_and_publish_state(
    const char *context, V1720StopOutcome *outcome = nullptr)
{
    if (!gV1720StartupEnabled && !gV1720StartAttempted && !gV1720Started) {
        if (outcome) *outcome = V1720StopOutcome::Disabled;
        return true;
    }

    if (!gVme) {
        cm_msg(MERROR, frontend_name,
               "Cannot stop V1720E during %s: VME interface is unavailable",
               context);
        invalidate_v1720e_current_state();
        publish_v1720e_variables();
        return false;
    }

    DWORD control = 0, acquisition_status = 0;
    int stop_attempted = 0;
    const int stop_status = v1720e_stop_if_running(
        gVme, V1720E_BASE, &control, &acquisition_status, &stop_attempted);
    if (stop_status != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name,
               "V1720E stop/verification failed during %s: status %d, "
               "stop_attempted=%d, AcqControl=0x%08X AcqStatus=0x%08X",
               context, stop_status, stop_attempted, control,
               acquisition_status);
        invalidate_v1720e_current_state();
        publish_v1720e_variables();
        return false;
    }

    V1720E_BOARD_INFO state = {};
    const int read_status = v1720e_probe(gVme, V1720E_BASE, &state);
    if (read_status != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name,
               "V1720E post-stop probe failed during %s: status %d",
               context, read_status);
        invalidate_v1720e_current_state();
        publish_v1720e_variables();
        return false;
    }
    if ((state.acquisition_control & V1720E_ACQ_RUN) ||
        (state.acquisition_status & V1720E_ACQ_RUN)) {
        cm_msg(MERROR, frontend_name,
               "V1720E still RUN during %s: AcqControl=0x%08X AcqStatus=0x%08X",
               context, state.acquisition_control, state.acquisition_status);
        invalidate_v1720e_current_state();
        publish_v1720e_variables();
        return false;
    }

    gV1720StartAttempted = false;
    gV1720Started = false;
    if (outcome) *outcome = stop_attempted
        ? V1720StopOutcome::StopVerified : V1720StopOutcome::AlreadyStopped;
    gV1720Runtime.communication_ok = TRUE;
    update_v1720e_board_state(state);
    publish_v1720e_variables();
    cm_msg(MINFO, frontend_name,
           "V1720E %s during %s: AcqControl=0x%08X AcqStatus=0x%08X",
           stop_attempted ? "stop verified" : "already stopped; no write",
           context, state.acquisition_control, state.acquisition_status);
    return true;
}

/* Low-level VME access helpers. */
static bool vme_read16(DWORD address, WORD &value, const char *description)
{
    int saved_mode;
    if (mvme_get_dmode(gVme, &saved_mode) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot get VME data mode for %s", description);
        return false;
    }
    bool ok = false;
    if (mvme_set_dmode(gVme, MVME_DMODE_D16) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot select D16 for %s", description);
    } else {
        const int status = mvme_read(gVme, &value, address, sizeof(value));
        if (status == MVME_SUCCESS)
            ok = true;
        else
            cm_msg(MERROR, frontend_name, "%s read failed at 0x%08X: status %d",
                   description, address, status);
    }
    if (mvme_set_dmode(gVme, saved_mode) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot restore VME data mode after %s", description);
        ok = false;
    }
    return ok;
}

static bool vme_read32(DWORD address, DWORD &value, const char *description)
{
    int saved_mode;
    if (mvme_get_dmode(gVme, &saved_mode) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot get VME data mode for %s", description);
        return false;
    }
    bool ok = false;
    if (mvme_set_dmode(gVme, MVME_DMODE_D32) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot select D32 for %s", description);
    } else {
        const int status = mvme_read(gVme, &value, address, sizeof(value));
        if (status == MVME_SUCCESS)
            ok = true;
        else
            cm_msg(MERROR, frontend_name, "%s read failed at 0x%08X: status %d",
                   description, address, status);
    }
    if (mvme_set_dmode(gVme, saved_mode) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot restore VME data mode after %s", description);
        ok = false;
    }
    return ok;
}

static bool vme_write16(DWORD address, WORD value, const char *description)
{
    int saved_mode;
    if (mvme_get_dmode(gVme, &saved_mode) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot get VME data mode for %s", description);
        return false;
    }
    bool ok = false;
    if (mvme_set_dmode(gVme, MVME_DMODE_D16) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot select D16 for %s", description);
    } else {
        const int status = mvme_write(gVme, address, &value, sizeof(value));
        if (status == MVME_SUCCESS)
            ok = true;
        else
            cm_msg(MERROR, frontend_name, "%s write failed at 0x%08X: status %d",
                   description, address, status);
    }
    if (mvme_set_dmode(gVme, saved_mode) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot restore VME data mode after %s", description);
        ok = false;
    }
    return ok;
}

static bool v1190_micro_wait(WORD ready_bit, const char *description)
{
    WORD handshake = 0;
    for (unsigned poll = 0; poll < V1190_MICRO_MAX_POLLS; ++poll) {
        if (!vme_read16(V1190_BASE + V1190_MICRO_HANDSHAKE, handshake,
                        "V1190 micro handshake"))
            return false;
        if (handshake & ready_bit)
            return true;
        ss_sleep(1);
    }
    cm_msg(MERROR, frontend_name, "V1190 micro %s timeout after %u polls (handshake 0x%04X)",
           description, V1190_MICRO_MAX_POLLS, handshake);
    return false;
}

static bool v1190_micro_write_opcode(WORD opcode)
{
    if (!v1190_micro_wait(V1190_MICRO_WRITE_OK, "write-ready"))
        return false;
    return vme_write16(V1190_BASE + V1190_MICRO_DATA, opcode,
                       "V1190 micro opcode");
}

static bool v1190_micro_write_command(WORD opcode,
                                      const WORD *operands,
                                      size_t operand_count)
{
    if (!v1190_micro_write_opcode(opcode))
        return false;
    for (size_t i = 0; i < operand_count; ++i) {
        if (!v1190_micro_wait(V1190_MICRO_WRITE_OK, "operand write-ready") ||
            !vme_write16(V1190_BASE + V1190_MICRO_DATA, operands[i],
                         "V1190 micro operand")) {
            cm_msg(MERROR, frontend_name,
                   "V1190 opcode 0x%04X operand %zu/%zu write failed",
                   opcode, i + 1, operand_count);
            return false;
        }
    }
    return true;
}

static bool v1190_micro_read_command(WORD opcode, WORD *words, size_t word_count)
{
    if (!v1190_micro_write_opcode(opcode))
        return false;
    for (size_t i = 0; i < word_count; ++i) {
        if (!v1190_micro_wait(V1190_MICRO_READ_OK, "response read-ready") ||
            !vme_read16(V1190_BASE + V1190_MICRO_DATA, words[i],
                        "V1190 micro response")) {
            cm_msg(MERROR, frontend_name,
                   "V1190 opcode 0x%04X response word %zu/%zu read failed",
                   opcode, i + 1, word_count);
            return false;
        }
    }
    return true;
}

static bool v1190_read_acquisition_mode(WORD &mode)
{
    return v1190_micro_read_command(V1190_OPCODE_READ_ACQ_MODE, &mode, 1);
}

/* Bounded module readers: read one hardware event and return raw words plus metadata. */
/* Return a complete raw event, or zero on error. Never scan into another event. */
static V792EventInfo read_v792_single_event(DWORD (&data)[V792_MAX_EVENT_WORDS])
{
    V792EventInfo event = {};
    int saved_mode;
    if (mvme_get_dmode(gVme, &saved_mode) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot get VME data mode");
        return event;
    }

    size_t result = 0;
    unsigned expected = 0;
    unsigned measurements = 0;
    unsigned geo = 0;
    if (mvme_set_dmode(gVme, MVME_DMODE_D32) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot select D32 for V792 readout");
    } else {
        for (size_t i = 0; i < V792_MAX_EVENT_WORDS; ++i) {
            DWORD word = 0;
            const int status = mvme_read(gVme, &word, V792_BASE, sizeof(word));
            if (status != MVME_SUCCESS) {
                cm_msg(MERROR, frontend_name, "V792 read failed at word %zu: status %d", i, status);
                break;
            }
            const unsigned type = (word >> 24) & 0x7;
            if (i == 0) {
                if (type != 2) {
                    cm_msg(MERROR, frontend_name, "V792 expected Header, got 0x%08X (type %u)", word, type);
                    break;
                }
                expected = (word >> 8) & 0x3f;
                geo = word >> 27;
                event.expected_measurements = expected;
                event.geo = geo;
                if (expected > V792_MAX_CHANNELS || expected + 2 > V792_MAX_EVENT_WORDS) {
                    cm_msg(MERROR, frontend_name, "V792 invalid Header count %u", expected);
                    break;
                }
            } else {
                if ((word >> 27) != geo || (type != 0 && type != 4)) {
                    cm_msg(MERROR, frontend_name, "V792 invalid word %zu: 0x%08X (type %u, GEO %u, expected GEO %u)",
                           i, word, type, word >> 27, geo);
                    break;
                }
                if (type == 4) {
                    if (measurements != expected) {
                        cm_msg(MERROR, frontend_name, "V792 Footer count mismatch: Header %u, received %u", expected, measurements);
                        break;
                    }
                    data[i] = word;
                    result = i + 1;
                    event.event_counter = word & V7XX_EVENT_COUNTER_MASK;
                    event.measurements = measurements;
                    event.valid = true;
                    gV792Runtime.event_counter=event.event_counter;
                    gV792Runtime.dirty=true;
                    break;
                }
                if (measurements >= expected) {
                    cm_msg(MERROR, frontend_name, "V792 expected Footer after %u measurements, got 0x%08X", measurements, word);
                    break;
                }
                ++measurements;
            }
            data[i] = word;
            if (i + 1 == V792_MAX_EVENT_WORDS)
                cm_msg(MERROR, frontend_name, "V792 readout reached limit of %zu words without Footer", V792_MAX_EVENT_WORDS);
        }
    }
    if (mvme_set_dmode(gVme, saved_mode) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot restore VME data mode");
        result = 0;
        event.valid = false;
    }
    event.words = result;
    return event;
}

static int v792_blt_header_read(void *context, uint32_t address, uint32_t *word)
{
    return mvme_read(static_cast<MVME_INTERFACE *>(context), word, address,
                     sizeof(*word)) == MVME_SUCCESS ? 0 : -1;
}

static int v792_blt_transfer(void *context, uint32_t address, void *destination,
                             int requested_bytes, int *actual_bytes)
{
    const MVME_INTERFACE *vme = static_cast<MVME_INTERFACE *>(context);
    return caenvme_a24_blt_read32(vme->handle, address, destination,
                                  requested_bytes, actual_bytes);
}

static V792EventInfo read_v792_blt32_event(DWORD (&data)[V792_MAX_EVENT_WORDS])
{
    V792EventInfo event = {};
    int saved_mode = 0;
    if (mvme_get_dmode(gVme, &saved_mode) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "V792 BLT32 cannot get VME data mode");
        return event;
    }
    if (mvme_set_dmode(gVme, MVME_DMODE_D32) != MVME_SUCCESS) {
        mvme_set_dmode(gVme, saved_mode);
        cm_msg(MERROR, frontend_name, "V792 BLT32 cannot select D32 header mode");
        return event;
    }
    const V792_BLT_IO io = {v792_blt_header_read, v792_blt_transfer, gVme};
    V792_BLT_RESULT result = {};
    const V792_BLT_STATUS status = v792_read_blt32(
        &io, V792_BASE, data, V792_MAX_EVENT_WORDS, &result);
    const int restore_status = mvme_set_dmode(gVme, saved_mode);
    static std::atomic<unsigned> diagnostic_count{0};
    if (result.requested_bytes != 0 &&
        diagnostic_count.fetch_add(1, std::memory_order_relaxed) < 10) {
        cm_msg(MINFO, frontend_name,
               "V792 BLT32: requested=%d actual=%d bytes CAEN status=%d validation=%d",
               result.requested_bytes, result.actual_bytes,
               result.caen_status, static_cast<int>(status));
    }
    if (status != V792_BLT_OK || restore_status != MVME_SUCCESS) {
        gBltStopRequested.store(true, std::memory_order_relaxed);
        cm_msg(MERROR, frontend_name,
               "V792 BLT32 readout failed: validation=%d CAEN status=%d requested=%d actual=%d restore=%d",
               static_cast<int>(status), result.caen_status,
               result.requested_bytes, result.actual_bytes, restore_status);
        return event;
    }
    event.words = result.words;
    event.event_counter = result.event_counter;
    event.expected_measurements = result.measurements;
    event.measurements = result.measurements;
    event.geo = result.geo;
    event.valid = true;
    gV792Runtime.event_counter = event.event_counter;
    gV792Runtime.dirty = true;
    return event;
}

/* Read exactly one V1190 event through its Global Trailer using D32 cycles. */
static V1190EventInfo read_v1190_single_event(DWORD (&data)[V1190_MAX_EVENT_WORDS])
{
    V1190EventInfo event = {};
    int saved_mode;
    if (mvme_get_dmode(gVme, &saved_mode) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot get VME data mode for V1190 readout");
        return event;
    }

    if (mvme_set_dmode(gVme, MVME_DMODE_D32) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot select D32 for V1190 readout");
    } else {
        for (size_t i = 0; i < V1190_MAX_EVENT_WORDS; ++i) {
            DWORD word = 0;
            const int status = mvme_read(gVme, &word, V1190_BASE, sizeof(word));
            if (status != MVME_SUCCESS) {
                cm_msg(MERROR, frontend_name, "V1190 read failed at word %zu: status %d", i, status);
                break;
            }

            data[i] = word;
            const unsigned type = (word >> 27) & 0x1F;
            if (i == 0) {
                if (type != 0x08) {
                    cm_msg(MERROR, frontend_name,
                           "V1190 expected Global Header, got 0x%08X (type 0x%02X)",
                           word, type);
                    break;
                }
                event.event_counter = (word >> 5) & V1190_EVENT_COUNTER_MASK;
                continue;
            }

            if (type == 0x10) {
                const unsigned trailer_words = (word >> 5) & 0xFFFF;
                const size_t actual_words = i + 1;
                if (trailer_words != actual_words) {
                    cm_msg(MERROR, frontend_name,
                           "V1190 Global Trailer count mismatch: trailer %u, read %zu",
                           trailer_words, actual_words);
                    break;
                }
                event.trailer_word_count = trailer_words;
                event.words = actual_words;
                event.valid = true;
                gV1190Runtime.event_counter=event.event_counter;
                gV1190Runtime.dirty=true;
                break; // Trailer consumed; never pre-read the next event.
            }

            bool invalid_type = false;
            switch (type) {
            case 0x00: // Measurement
            case 0x01: // TDC Header
            case 0x03: // TDC Trailer
            case 0x04: // Error
            case 0x11: // Extended Trigger Time Tag
                break;
            case 0x08:
                cm_msg(MERROR, frontend_name,
                       "V1190 unexpected Global Header at word %zu: 0x%08X", i, word);
                invalid_type = true;
                break;
            case 0x18:
                cm_msg(MERROR, frontend_name,
                       "V1190 unexpected Filler at word %zu: 0x%08X", i, word);
                invalid_type = true;
                break;
            default:
                cm_msg(MERROR, frontend_name,
                       "V1190 reserved word type 0x%02X at word %zu: 0x%08X",
                       type, i, word);
                invalid_type = true;
                break;
            }
            if (invalid_type)
                break;
            if (i + 1 == V1190_MAX_EVENT_WORDS && event.words == 0)
                cm_msg(MERROR, frontend_name,
                       "V1190 readout reached limit of %zu words without Global Trailer",
                       V1190_MAX_EVENT_WORDS);
        }
    }

    if (mvme_set_dmode(gVme, saved_mode) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot restore VME data mode after V1190 readout");
        event.words = 0;
        event.valid = false;
    }
    return event;
}

static int v1190_fifo_read16(void *, uint32_t address, uint16_t *value)
{
    WORD readback = 0;
    if (!vme_read16(address, readback, "V1190 Event FIFO D16 read")) return -1;
    *value = readback;
    return 0;
}

static int v1190_fifo_read32(void *, uint32_t address, uint32_t *value)
{
    DWORD readback = 0;
    if (!vme_read32(address, readback, "V1190 Event FIFO entry read")) return -1;
    *value = readback;
    return 0;
}

static V1190EventInfo read_v1190_fifo_blt32_event(
    DWORD (&data)[V1190_MAX_EVENT_WORDS], V1190_FIFO_BLT_TIMING &phases,
    bool &diagnostic)
{
    V1190EventInfo event = {};
    const V1190_FIFO_BLT_IO io = {v1190_fifo_read16, v1190_fifo_read32,
                                  v792_blt_transfer, gVme};
    V1190_FIFO_BLT_RESULT result = {};
    const V1190_FIFO_BLT_STATUS status = v1190_fifo_read_blt32(
        &io, V1190_BASE, data, V1190_MAX_EVENT_WORDS,
        V1190_FIFO_STRICT_SYNC_CHECK, &gV1190FifoBltState, &result);
    phases = result.timing;
    diagnostic = gV1190BltDiagnosticCount.fetch_add(
        1, std::memory_order_relaxed) < 10;
    if (diagnostic) {
        cm_msg(MINFO, frontend_name,
               "V1190 BLT FIFO=%u stored=%d->%d req=%d got=%d CAEN=%d",
               static_cast<unsigned>(result.fifo_word_count),
               result.timing.stored_before_checked ?
                   static_cast<int>(result.stored_before) : -1,
               result.timing.stored_after_checked ?
                   static_cast<int>(result.stored_after) : -1,
               result.requested_bytes, result.actual_bytes,
               result.caen_status);
        cm_msg(MINFO, frontend_name,
               "V1190 BLT trailer=%u ctr=%04X/%06X match=%d status=%d",
               static_cast<unsigned>(result.trailer_word_count),
               static_cast<unsigned>(result.fifo_event_counter),
               static_cast<unsigned>(result.event_counter),
               result.counter_consistent, static_cast<int>(status));
    }
    if (status != V1190_FIFO_BLT_OK) {
        /* The FIFO entry and/or Output Buffer may have advanced. Never retry. */
        gBltStopRequested.store(true, std::memory_order_relaxed);
        if (!V1190_FIFO_STRICT_SYNC_CHECK &&
            status == V1190_FIFO_BLT_STORED_MISMATCH)
            cm_msg(MERROR, frontend_name,
                   "V1190 FIFO periodic sync failed: stored_after=%u expected=0",
                   static_cast<unsigned>(result.stored_after));
        cm_msg(MERROR, frontend_name,
               "V1190 BLT error s=%d c=%d n=%u req=%d got=%d",
               static_cast<int>(status), result.caen_status,
               static_cast<unsigned>(result.fifo_word_count),
               result.requested_bytes,
               result.actual_bytes);
        return event;
    }
    if (!V1190_FIFO_STRICT_SYNC_CHECK && result.timing.stored_after_checked)
        cm_msg(MINFO, frontend_name,
               "V1190 FIFO check: event=%llu stored_after=%u OK",
               static_cast<unsigned long long>(
                   gV1190FifoBltState.successful_event_count),
               static_cast<unsigned>(result.stored_after));
    event.words = result.words;
    event.event_counter = result.event_counter;
    event.trailer_word_count = result.trailer_word_count;
    event.valid = true;
    gV1190Runtime.event_counter = event.event_counter;
    gV1190Runtime.dirty = true;
    return event;
}

static bool wait_for_v1190_data_ready()
{
    WORD status = 0;
    for (unsigned poll = 0; poll < V1190_READY_MAX_POLLS; ++poll) {
        if (!vme_read16(V1190_BASE + V1190_STATUS, status, "V1190 Status"))
            return false;
        decode_v1190_runtime(status,gV1190Runtime.event_stored,gV1190Runtime.event_counter);
        if (status & V1190_STATUS_DATA_READY)
            return true;
        if (poll + 1 < V1190_READY_MAX_POLLS)
            ss_sleep(1);
    }
    cm_msg(MERROR, frontend_name,
           "V1190 DataReady timeout after %u polls; V792 FIFO was not consumed",
           V1190_READY_MAX_POLLS);
    return false;
}

/* Read exactly one V775 event through its EOB using D32 single cycles. */
static V775EventInfo read_v775_single_event(DWORD (&data)[V775_MAX_EVENT_WORDS])
{
    V775EventInfo event = {};
    int saved_mode;
    if (mvme_get_dmode(gVme, &saved_mode) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot get VME data mode for V775 readout");
        return event;
    }

    if (mvme_set_dmode(gVme, MVME_DMODE_D32) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "Cannot select D32 for V775 readout");
    } else {
        unsigned expected_measurements = 0;
        unsigned measurements = 0;
        unsigned geo = 0;
        for (size_t i = 0; i < V775_MAX_EVENT_WORDS; ++i) {
            DWORD word = 0;
            const int status = mvme_read(gVme, &word, V775_BASE, sizeof(word));
            if (status != MVME_SUCCESS) {
                cm_msg(MERROR, frontend_name,
                       "V775 read failed at word %zu: status %d", i, status);
                break;
            }

            data[i] = word;
            const unsigned type = (word >> 24) & 0x7;
            if (i == 0) {
                if (type != V775_DATA_TYPE_HEADER) {
                    cm_msg(MERROR, frontend_name,
                           "V775 expected Header, got 0x%08X (type %u)",
                           word, type);
                    break;
                }
                geo = word >> 27;
                expected_measurements = (word >> 8) & 0x3F;
                event.expected_measurements = expected_measurements;
                event.geo = geo;
                if (expected_measurements > V775_MAX_CHANNELS) {
                    cm_msg(MERROR, frontend_name,
                           "V775 invalid Header channel count %u",
                           expected_measurements);
                    break;
                }
                continue;
            }

            if ((word >> 27) != geo) {
                cm_msg(MERROR, frontend_name,
                       "V775 GEO mismatch at word %zu: got %u, expected %u",
                       i, word >> 27, geo);
                break;
            }
            if (type == V775_DATA_TYPE_EOB) {
                if (measurements != expected_measurements) {
                    cm_msg(MERROR, frontend_name,
                           "V775 EOB count mismatch: Header %u, measurements %u",
                           expected_measurements, measurements);
                    break;
                }
                event.event_counter = word & V7XX_EVENT_COUNTER_MASK;
                event.measurements = measurements;
                event.words = i + 1;
                event.valid = true;
                gV775Runtime.event_counter=event.event_counter;
                gV775Runtime.dirty=true;
                break; // EOB consumed; never pre-read the next event.
            }

            switch (type) {
            case V775_DATA_TYPE_MEASUREMENT:
                ++measurements;
                if (measurements > expected_measurements) {
                    cm_msg(MERROR, frontend_name,
                           "V775 received more measurements than Header count %u",
                           expected_measurements);
                    i = V775_MAX_EVENT_WORDS;
                }
                break;
            case V775_DATA_TYPE_INVALID:
                cm_msg(MERROR, frontend_name,
                       "V775 invalid datum (type 6) at word %zu: 0x%08X", i, word);
                i = V775_MAX_EVENT_WORDS;
                break;
            default:
                cm_msg(MERROR, frontend_name,
                       "V775 reserved word type %u at word %zu: 0x%08X",
                       type, i, word);
                i = V775_MAX_EVENT_WORDS;
                break;
            }
            if (i + 1 == V775_MAX_EVENT_WORDS && event.words == 0)
                cm_msg(MERROR, frontend_name,
                       "V775 readout reached limit of %zu words without EOB",
                       V775_MAX_EVENT_WORDS);
        }
    }

    if (mvme_set_dmode(gVme, saved_mode) != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name,
               "Cannot restore VME data mode after V775 readout");
        event.words = 0;
        event.valid = false;
    }
    return event;
}

static bool wait_for_v775_data_ready()
{
    for (unsigned poll = 0; poll < V775_READY_MAX_POLLS; ++poll) {
        if (v775_DataReady(gVme, V775_BASE))
            return true;
        if (poll + 1 < V775_READY_MAX_POLLS)
            ss_sleep(1);
    }
    cm_msg(MERROR, frontend_name,
           "V775 DataReady timeout after %u polls; V792 FIFO was not consumed",
           V775_READY_MAX_POLLS);
    return false;
}
static bool wait_for_v1720e_data_ready()
{
    DWORD event_stored = 0;
    DWORD acquisition_status = 0;
    for (unsigned poll = 0; poll < V1720E_READY_MAX_POLLS; ++poll) {
        int ready = 0;
        const int status = v1720e_data_ready(gVme, V1720E_BASE, &ready,
                                            &event_stored,
                                            &acquisition_status);
        if (status != MVME_SUCCESS) {
            gV1720Runtime.communication_ok = FALSE;
            gV1720Runtime.dirty = true;
            ++gRunStatistics.v1720_read_timeout_count;
            mark_run_counters_dirty();
            cm_msg(MERROR, frontend_name,
                   "V1720E ready read failed: status %d", status);
            return false;
        }
        update_v1720e_acquisition_status(acquisition_status);
        gV1720Runtime.event_stored = event_stored;
        if (ready)
            return true;
        if (poll + 1 < V1720E_READY_MAX_POLLS)
            ss_sleep(1);
    }
    ++gRunStatistics.v1720_read_timeout_count;
    mark_run_counters_dirty();
    cm_msg(MERROR, frontend_name,
           "V1720E DataReady timeout after %u polls (Event Stored %u); "
           "V792 FIFO was not consumed",
           V1720E_READY_MAX_POLLS, event_stored);
    return false;
}

/* Diagnostic-only configuration and restoration. */
#if ENABLE_V1190_SOFT_TRIGGER_TEST
static bool restore_v1190_diagnostic_settings()
{
    if (!gV1190DiagnosticSaved)
        return true;

    bool ok = true;
    if (gV1190EmptyEventMayHaveChanged) {
        WORD control = 0;
        if (!vme_read16(V1190_BASE + V1190_CONTROL, control, "V1190 Control restore read")) {
            ok = false;
        } else {
            const WORD restored = static_cast<WORD>(
                (control & ~V1190_CONTROL_EMPTY_EVENT) |
                (gV1190SavedControl & V1190_CONTROL_EMPTY_EVENT));
            if (!vme_write16(V1190_BASE + V1190_CONTROL, restored,
                             "V1190 Empty Event restore")) {
                ok = false;
            } else if (!vme_read16(V1190_BASE + V1190_CONTROL, control,
                                   "V1190 Control restore verify") ||
                       (control & V1190_CONTROL_EMPTY_EVENT) !=
                           (gV1190SavedControl & V1190_CONTROL_EMPTY_EVENT)) {
                cm_msg(MERROR, frontend_name, "V1190 Empty Event restoration verification failed");
                ok = false;
            } else {
                cm_msg(MINFO, frontend_name, "V1190 Empty Event bit restored (Control 0x%04X)", control);
                gV1190EmptyEventMayHaveChanged = false;
            }
        }
    }

    // Mode restoration is attempted even if Control restoration failed.
    if (gV1190ModeMayHaveChanged) {
        WORD mode = 0;
        WORD status = 0;
        if (!v1190_micro_write_opcode(V1190_OPCODE_CONTINUOUS) ||
            !v1190_read_acquisition_mode(mode) ||
            !vme_read16(V1190_BASE + V1190_STATUS, status,
                        "V1190 Status restore verify") ||
            (mode & 1) != 0 || (status & V1190_STATUS_TRIGGER_MATCH) != 0) {
            cm_msg(MERROR, frontend_name, "V1190 Continuous Storage restoration verification failed");
            ok = false;
        } else {
            cm_msg(MINFO, frontend_name, "V1190 Continuous Storage restored and verified");
            gV1190ModeMayHaveChanged = false;
        }
    }

    if (!gV1190EmptyEventMayHaveChanged && !gV1190ModeMayHaveChanged)
        gV1190DiagnosticSaved = false;
    return ok;
}

static bool setup_v1190_soft_trigger_test()
{
    WORD mode = 0;
    WORD status = 0;
    WORD events_stored = 0;
    if (!v1190_read_acquisition_mode(mode) ||
        !vme_read16(V1190_BASE + V1190_CONTROL, gV1190SavedControl,
                    "V1190 Control save") ||
        !vme_read16(V1190_BASE + V1190_STATUS, status, "V1190 Status") ||
        !vme_read16(V1190_BASE + V1190_EVENT_STORED, events_stored,
                    "V1190 Event Stored"))
        return false;

    gV1190DiagnosticSaved = true;
    if (!!(mode & 1) != !!(status & V1190_STATUS_TRIGGER_MATCH)) {
        cm_msg(MERROR, frontend_name,
               "V1190 acquisition mode/status mismatch before soft-trigger test");
        return false;
    }
    if ((status & V1190_STATUS_DATA_READY) != 0 || events_stored != 0) {
        cm_msg(MERROR, frontend_name,
               "V1190 soft-trigger test requires empty FIFO (Status 0x%04X, Event Stored %u)",
               status, events_stored);
        return false;
    }

    if ((mode & 1) == 0) {
        gV1190ModeMayHaveChanged = true; // Include an ambiguous opcode-write failure.
        if (!v1190_micro_write_opcode(V1190_OPCODE_TRIGGER_MATCH) ||
            !v1190_read_acquisition_mode(mode) ||
            !vme_read16(V1190_BASE + V1190_STATUS, status, "V1190 Status") ||
            (mode & 1) == 0 || (status & V1190_STATUS_TRIGGER_MATCH) == 0) {
            cm_msg(MERROR, frontend_name, "V1190 Trigger Matching setup verification failed");
            restore_v1190_diagnostic_settings();
            return false;
        }
    }

    WORD control = 0;
    if (!vme_read16(V1190_BASE + V1190_CONTROL, control, "V1190 Control RMW read")) {
        restore_v1190_diagnostic_settings();
        return false;
    }
    gV1190EmptyEventMayHaveChanged = true; // Include an ambiguous write failure.
    const WORD temporary = static_cast<WORD>(control | V1190_CONTROL_EMPTY_EVENT);
    WORD readback = 0;
    if (!vme_write16(V1190_BASE + V1190_CONTROL, temporary,
                     "V1190 Empty Event enable") ||
        !vme_read16(V1190_BASE + V1190_CONTROL, readback,
                    "V1190 Control enable verify") ||
        (readback & V1190_CONTROL_EMPTY_EVENT) == 0 ||
        (readback & ~V1190_CONTROL_EMPTY_EVENT) !=
            (control & ~V1190_CONTROL_EMPTY_EVENT)) {
        cm_msg(MERROR, frontend_name, "V1190 Empty Event setup verification failed");
        restore_v1190_diagnostic_settings();
        return false;
    }

    if (!vme_write16(V1190_BASE + V1190_SOFT_TRIGGER, 0,
                     "V1190 Soft Trigger")) {
        restore_v1190_diagnostic_settings();
        return false;
    }
    cm_msg(MINFO, frontend_name,
           "V1190 soft-trigger test armed: Trigger Matching, Empty Event, one Soft Trigger");
    return true;
}
#endif

#if ENABLE_V775_SW_TRIGGER_TEST
static bool restore_v775_diagnostic_settings()
{
    if (!gV775DiagnosticSaved)
        return true;

    if ((gV775SavedBitSet2 & V775_BIT2_EMPTY_PROGRAM) == 0 &&
        gV775EmptyProgramMayHaveChanged) {
        WORD readback = 0;
        if (!vme_write16(V775_BASE + V775_BIT_CLEAR2,
                         V775_BIT2_EMPTY_PROGRAM,
                         "V775 Empty Program restore clear") ||
            !vme_read16(V775_BASE + V775_BIT_SET2, readback,
                        "V775 Bit Set 2 restore verify")) {
            return false;
        }
        if ((readback & V775_BIT2_EMPTY_PROGRAM) != 0) {
            cm_msg(MERROR, frontend_name,
                   "V775 Empty Program restoration verify failed: Bit Set 2=0x%04X",
                   readback);
            return false;
        }
        cm_msg(MINFO, frontend_name,
               "V775 Empty Program restored to disabled (Bit Set 2=0x%04X)",
               readback);
        gV775EmptyProgramMayHaveChanged = false;
    } else {
        cm_msg(MINFO, frontend_name,
               "V775 Empty Program restoration needs no clear; original state was %s",
               (gV775SavedBitSet2 & V775_BIT2_EMPTY_PROGRAM) ? "enabled" : "disabled");
    }

    gV775DiagnosticSaved = false;
    return true;
}

static bool setup_v775_sw_trigger_test()
{
    WORD bitset2 = 0;
    WORD readback = 0;
    WORD status1 = 0;
    WORD status2 = 0;
    DWORD counter = 0;

    if (!vme_read16(V775_BASE + V775_BIT_SET2, bitset2,
                    "V775 Bit Set 2 save"))
        return false;
    gV775SavedBitSet2 = bitset2;
    gV775DiagnosticSaved = true;
    gV775EmptyProgramMayHaveChanged = false;
    cm_msg(MINFO, frontend_name,
           "V775 diagnostic saved Bit Set 2=0x%04X; Empty Program=%s",
           bitset2,
           (bitset2 & V775_BIT2_EMPTY_PROGRAM) ? "enabled" : "disabled");

    if ((bitset2 & V775_BIT2_EMPTY_PROGRAM) == 0) {
        // From this write attempt onward, cleanup assumes the bit may be set.
        gV775EmptyProgramMayHaveChanged = true;
        if (!vme_write16(V775_BASE + V775_BIT_SET2,
                         V775_BIT2_EMPTY_PROGRAM,
                         "V775 Empty Program enable") ||
            !vme_read16(V775_BASE + V775_BIT_SET2, readback,
                        "V775 Bit Set 2 enable verify") ||
            (readback & V775_BIT2_EMPTY_PROGRAM) == 0) {
            cm_msg(MERROR, frontend_name,
                   "V775 Empty Program enable/readback failed");
            restore_v775_diagnostic_settings();
            return false;
        }
        cm_msg(MINFO, frontend_name,
               "V775 Empty Program temporarily enabled (Bit Set 2=0x%04X)",
               readback);
    }

    if (!vme_read16(V775_BASE + V775_STATUS1, status1,
                    "V775 Status 1 before SW Comm") ||
        !vme_read16(V775_BASE + V775_STATUS2, status2,
                    "V775 Status 2 before SW Comm")) {
        restore_v775_diagnostic_settings();
        return false;
    }
    v775_EvtCntRead(gVme, V775_BASE, &counter);
    printf("V775 SW trigger test enabled: issuing one SW Comm at begin of run.\n");
    printf("V775 before SW Comm: Status1=0x%04X Status2=0x%04X Event Counter=0x%06X\n",
           status1, status2, counter);

    if (!vme_write16(V775_BASE + V775_SW_COMM, 0, "V775 SW Comm")) {
        restore_v775_diagnostic_settings();
        return false;
    }

    bool ready = false;
    unsigned polls_done = 0;
    for (unsigned poll = 1; poll <= V775_SW_TRIGGER_MAX_POLLS; ++poll) {
        polls_done = poll;
        if (!vme_read16(V775_BASE + V775_STATUS1, status1,
                        "V775 Status 1 after SW Comm")) {
            restore_v775_diagnostic_settings();
            return false;
        }
        if (status1 & V775_STATUS1_DATA_READY) {
            ready = true;
            break;
        }
        if (poll < V775_SW_TRIGGER_MAX_POLLS)
            ss_sleep(1);
    }
    if (!vme_read16(V775_BASE + V775_STATUS2, status2,
                    "V775 Status 2 after SW Comm")) {
        restore_v775_diagnostic_settings();
        return false;
    }
    v775_EvtCntRead(gVme, V775_BASE, &counter);
    printf("V775 after SW Comm poll %u/%u: DataReady=%s Status1=0x%04X Status2=0x%04X Event Counter=0x%06X\n",
           polls_done, V775_SW_TRIGGER_MAX_POLLS, ready ? "Y" : "N",
           status1, status2, counter);
    if (!ready) {
        cm_msg(MERROR, frontend_name,
               "V775 SW trigger test DataReady timeout after %u polls",
               V775_SW_TRIGGER_MAX_POLLS);
        restore_v775_diagnostic_settings();
        return false;
    }
    return true;
}
#endif

static bool make_v1720e_run_configuration(const V1720ESettings &settings,
                                          V1720E_CONFIG &config,
                                          DWORD &expected_event_words,
                                          DWORD &expected_channel_mask)
{
    if (settings.buffer_organization > 0x0Au) {
        cm_msg(MERROR, frontend_name,
               "V1720E BufferOrganization %u is outside supported range 0..10",
               settings.buffer_organization);
        return false;
    }
    if (settings.post_trigger > 0xFFu) {
        cm_msg(MERROR, frontend_name,
               "V1720E PostTrigger %u is outside the 8-bit register range",
               settings.post_trigger);
        return false;
    }
    /*
     * The installed waveform-recording firmware 4.5 was verified with
     * Custom Size 0x40 producing 256 samples/channel.  The older CAEN
     * register description warns that its generic NLOC conversion may not
     * apply above ROC firmware 3.8, so the first ODB version deliberately
     * accepts only this verified pair.
     */
    if (settings.record_length_samples != V1720E_DEFAULT_RECORD_SAMPLES) {
        cm_msg(MERROR, frontend_name,
               "V1720E RecordLengthSamples %u is unsupported; first ODB version accepts only %u",
               settings.record_length_samples,
               V1720E_DEFAULT_RECORD_SAMPLES);
        return false;
    }

    config = {};
    config.buffer_organization = settings.buffer_organization;
    config.custom_size = V1720E_DEFAULT_CUSTOM_SIZE;
    config.post_trigger = settings.post_trigger;
    if (settings.software_trigger_enabled)
        config.trigger_source |= V1720E_TRIGGER_SOFTWARE;
    if (settings.external_trigger_enabled)
        config.trigger_source |= V1720E_TRIGGER_EXTERNAL;

    unsigned enabled_channels = 0;
    for (unsigned channel = 0; channel < V1720E_CHANNEL_COUNT; ++channel) {
        if (settings.channel_self_trigger_enabled[channel])
            config.trigger_source |= (1u << channel);
        if (settings.channel_enabled[channel]) {
            config.channel_enable |= (1u << channel);
            ++enabled_channels;
        }
        config.dc_offset[channel] = settings.dc_offset[channel];
    }
    if (enabled_channels == 0) {
        cm_msg(MERROR, frontend_name,
               "V1720E configuration has no enabled channels");
        return false;
    }

    expected_channel_mask = config.channel_enable & 0xFFu;
    expected_event_words = 4u + enabled_channels *
        (settings.record_length_samples / 2u);
    if (expected_event_words > V1720E_MAX_EVENT_WORDS) {
        cm_msg(MERROR, frontend_name,
               "V1720E expected event size %u exceeds safety limit %u",
               expected_event_words, V1720E_MAX_EVENT_WORDS);
        return false;
    }
    return true;
}

static bool snapshot_v1720e_settings_for_run()
{
    V1720ESettings settings = {};
    V1720E_CONFIG config = {};
    DWORD expected_event_words = 0;
    DWORD expected_channel_mask = 0;
    if (!read_v1720e_settings(settings))
        return false;
    if (settings.enabled &&
        !make_v1720e_run_configuration(settings, config,
                                       expected_event_words,
                                       expected_channel_mask))
        return false;
    gV1720RunSettings = settings;
    gV1720VariablesEnabled = settings.enabled != FALSE;
    gV1720RunConfig = config;
    gV1720ExpectedEventWords = expected_event_words;
    gV1720ExpectedChannelMask = expected_channel_mask;
    return true;
}

static void publish_v1720e_readback(const V1720E_CONFIG_READBACK &readback,
                                    bool valid)
{
    const DWORD record_length =
        readback.custom_size == V1720E_DEFAULT_CUSTOM_SIZE
            ? V1720E_DEFAULT_RECORD_SAMPLES : 0u;
    const BOOL software_trigger =
        (readback.trigger_source & V1720E_TRIGGER_SOFTWARE) != 0;
    const BOOL external_trigger =
        (readback.trigger_source & V1720E_TRIGGER_EXTERNAL) != 0;
    const BOOL zero_suppression =
        (readback.channel_config & V1720E_CHANNEL_CONFIG_ZS_MASK) != 0;
    const BOOL pack25 =
        (readback.channel_config & V1720E_CHANNEL_CONFIG_PACK25) != 0;
    BOOL self_trigger[V1720E_CHANNEL_COUNT] = {};
    BOOL channel_enabled[V1720E_CHANNEL_COUNT] = {};
    for (unsigned channel = 0; channel < V1720E_CHANNEL_COUNT; ++channel) {
        self_trigger[channel] =
            (readback.trigger_source & (1u << channel)) != 0;
        channel_enabled[channel] =
            (readback.channel_enable & (1u << channel)) != 0;
    }

#define SET_READBACK(name, value, count, type) \
    set_v1720e_output(V1720E_READBACK_PATH, name, &(value), sizeof(value), \
                      count, type)
    SET_READBACK("BoardInfo", readback.board_info, 1, TID_DWORD);
    SET_READBACK("RocFirmwareRevision", readback.roc_firmware, 1, TID_DWORD);
    SET_READBACK("BufferOrganization", readback.buffer_organization, 1, TID_DWORD);
    SET_READBACK("CustomSizeRaw", readback.custom_size, 1, TID_DWORD);
    SET_READBACK("RecordLengthSamples", record_length, 1, TID_DWORD);
    SET_READBACK("PostTrigger", readback.post_trigger, 1, TID_DWORD);
    SET_READBACK("SoftwareTriggerEnabled", software_trigger, 1, TID_BOOL);
    SET_READBACK("ExternalTriggerEnabled", external_trigger, 1, TID_BOOL);
    SET_READBACK("ChannelSelfTriggerEnabled", self_trigger,
                 V1720E_CHANNEL_COUNT, TID_BOOL);
    SET_READBACK("ChannelEnabled", channel_enabled,
                 V1720E_CHANNEL_COUNT, TID_BOOL);
    SET_READBACK("DCOffset", readback.dc_offset,
                 V1720E_CHANNEL_COUNT, TID_WORD);
    SET_READBACK("ZeroSuppressionEnabled", zero_suppression, 1, TID_BOOL);
    SET_READBACK("Pack25Enabled", pack25, 1, TID_BOOL);
    SET_READBACK("TriggerSourceRaw", readback.trigger_source, 1, TID_DWORD);
    SET_READBACK("ChannelEnableRaw", readback.channel_enable, 1, TID_DWORD);
    SET_READBACK("ChannelConfigRaw", readback.channel_config, 1, TID_DWORD);
#undef SET_READBACK
    set_v1720e_readback_valid(valid);
}

static void capture_v1720e_run_readback(
    const V1720E_CONFIG_READBACK &readback, bool valid)
{
    V1720EReadbackSnapshot &snapshot = gVmeRunSnapshot.v1720e_readback;
    snapshot = {};
    snapshot.valid = valid ? TRUE : FALSE;
    snapshot.board_info = readback.board_info;
    snapshot.roc_firmware_revision = readback.roc_firmware;
    snapshot.buffer_organization = readback.buffer_organization;
    snapshot.custom_size_raw = readback.custom_size;
    snapshot.record_length_samples =
        readback.custom_size == V1720E_DEFAULT_CUSTOM_SIZE
            ? V1720E_DEFAULT_RECORD_SAMPLES : 0u;
    snapshot.post_trigger = readback.post_trigger;
    snapshot.software_trigger_enabled =
        (readback.trigger_source & V1720E_TRIGGER_SOFTWARE) != 0;
    snapshot.external_trigger_enabled =
        (readback.trigger_source & V1720E_TRIGGER_EXTERNAL) != 0;
    for (unsigned channel = 0; channel < V1720E_CHANNEL_COUNT; ++channel) {
        snapshot.channel_self_trigger_enabled[channel] =
            (readback.trigger_source & (1u << channel)) != 0;
        snapshot.channel_enabled[channel] =
            (readback.channel_enable & (1u << channel)) != 0;
        snapshot.dc_offset[channel] = readback.dc_offset[channel];
    }
    snapshot.zero_suppression_enabled =
        (readback.channel_config & V1720E_CHANNEL_CONFIG_ZS_MASK) != 0;
    snapshot.pack25_enabled =
        (readback.channel_config & V1720E_CHANNEL_CONFIG_PACK25) != 0;
    snapshot.trigger_source_raw = readback.trigger_source;
    snapshot.channel_enable_raw = readback.channel_enable;
    snapshot.channel_config_raw = readback.channel_config;
}

static bool verify_v1720e_readback(const V1720E_CONFIG &expected,
                                   const V1720E_CONFIG_READBACK &actual)
{
    bool ok = true;
#define VERIFY_V1720(name, expected_value, actual_value) \
    do { \
        if ((expected_value) != (actual_value)) { \
            cm_msg(MERROR, frontend_name, \
                   "V1720E readback mismatch: %s expected 0x%X, got 0x%X", \
                   name, static_cast<unsigned>(expected_value), \
                   static_cast<unsigned>(actual_value)); \
            ok = false; \
        } \
    } while (0)
    VERIFY_V1720("BufferOrganization", expected.buffer_organization,
                 actual.buffer_organization);
    VERIFY_V1720("CustomSizeRaw", expected.custom_size, actual.custom_size);
    VERIFY_V1720("PostTrigger", expected.post_trigger, actual.post_trigger);
    VERIFY_V1720("TriggerSourceRaw", expected.trigger_source,
                 actual.trigger_source);
    VERIFY_V1720("ChannelEnableRaw", expected.channel_enable,
                 actual.channel_enable);
    VERIFY_V1720("ZeroSuppression", 0u,
                 actual.channel_config & V1720E_CHANNEL_CONFIG_ZS_MASK);
    VERIFY_V1720("Pack25", 0u,
                 actual.channel_config & V1720E_CHANNEL_CONFIG_PACK25);
    for (unsigned channel = 0; channel < V1720E_CHANNEL_COUNT; ++channel) {
        if (expected.dc_offset[channel] != actual.dc_offset[channel]) {
            cm_msg(MERROR, frontend_name,
                   "V1720E readback mismatch: DCOffset[%u] expected 0x%04X, got 0x%04X",
                   channel, expected.dc_offset[channel],
                   actual.dc_offset[channel]);
            ok = false;
        }
    }
#undef VERIFY_V1720
    return ok;
}

/* Frontend initialization checks. Keep the established read-only access order. */
static bool check_module_communication(bool check_v1720e)
{
    if (gV792RunSettings.enabled) {
      printf("Checking V792 at 0x%08X...\n", V792_BASE);
      if (!v792_isPresent(gVme, V792_BASE)) {
        gV792Runtime.communication_ok=FALSE; publish_v7xx_variables(V792_VARIABLES_PATH,gV792Runtime,gV792LastVariablesPublish);
        cm_msg(MERROR, frontend_name,
               "V792 not found at 0x%08X", V792_BASE);
        return false;
      }
      gV792Runtime.communication_ok=TRUE; gV792Runtime.dirty=true;
      printf("V792 detected.\n");
    } else { gV792Runtime={}; gV792Runtime.dirty=true; set_module_readback_valid(V792_READBACK_PATH,false); publish_v7xx_variables(V792_VARIABLES_PATH,gV792Runtime,gV792LastVariablesPublish); printf("V792 disabled in ODB; communication check skipped.\n"); }

    WORD v1190_status = 0;
    WORD v1190_events = 0;
    if (gV1190RunSettings.enabled) {
      printf("Checking V1190A at 0x%08X...\n", V1190_BASE);
      if (!vme_read16(V1190_BASE + V1190_STATUS, v1190_status, "V1190 Status") ||
        !vme_read16(V1190_BASE + V1190_EVENT_STORED, v1190_events,
                    "V1190 Event Stored")) {
        gV1190Runtime.communication_ok=FALSE; publish_v1190_variables();
        cm_msg(MERROR, frontend_name,
               "V1190A communication check failed at 0x%08X", V1190_BASE);
        return false;
      }
      gV1190Runtime.communication_ok=TRUE; decode_v1190_runtime(v1190_status,v1190_events,gV1190Runtime.event_counter);
      printf("V1190A detected: Status=0x%04X Event Stored=%u.\n",
           v1190_status, v1190_events);
    } else { gV1190Runtime={}; gV1190Runtime.dirty=true; set_module_readback_valid(V1190_READBACK_PATH,false); publish_v1190_variables(); printf("V1190 disabled in ODB; communication check skipped.\n"); }

    if (gV775RunSettings.enabled) {
      printf("Checking V775 at 0x%08X...\n", V775_BASE);
      if (!v775_isPresent(gVme, V775_BASE)) {
        gV775Runtime.communication_ok=FALSE; publish_v7xx_variables(V775_VARIABLES_PATH,gV775Runtime,gV775LastVariablesPublish);
        cm_msg(MERROR, frontend_name,
               "V775 not found at 0x%08X", V775_BASE);
        return false;
      }
      gV775Runtime.communication_ok=TRUE; gV775Runtime.dirty=true; printf("V775 detected.\n");
    } else { gV775Runtime={}; gV775Runtime.dirty=true; set_module_readback_valid(V775_READBACK_PATH,false); publish_v7xx_variables(V775_VARIABLES_PATH,gV775Runtime,gV775LastVariablesPublish); printf("V775 disabled in ODB; communication check skipped.\n"); }

    if (!check_v1720e) {
        set_v1720e_communication_ok(false);
        set_v1720e_readback_valid(false);
        printf("V1720E disabled in ODB; communication check skipped.\n");
        return true;
    }

    V1720E_BOARD_INFO v1720 = {};
    printf("Checking V1720E at 0x%08X...\n", V1720E_BASE);
    const int v1720_status = v1720e_probe(gVme, V1720E_BASE, &v1720);
    if (v1720_status != MVME_SUCCESS ||
        (v1720.board_info & 0xFFu) != 0x03u ||
        ((v1720.board_info >> 8) & 0xFFu) != 0x02u ||
        ((v1720.board_info >> 16) & 0xFFu) != 8u) {
        cm_msg(MERROR, frontend_name,
               "V1720E identification failed at 0x%08X: status %d "
               "BoardInfo 0x%08X",
               V1720E_BASE, v1720_status, v1720.board_info);
        set_v1720e_communication_ok(false);
        set_v1720e_readback_valid(false);
        return false;
    }
    printf("V1720E detected: BoardInfo=0x%08X ROC-FW=0x%08X "
           "AcqControl=0x%08X AcqStatus=0x%08X EventStored=%u.\n",
           v1720.board_info, v1720.roc_firmware,
           v1720.acquisition_control, v1720.acquisition_status,
           v1720.event_stored);
    set_v1720e_communication_ok(true);
    publish_v1720e_board_state(v1720);
    return true;
}

struct V1190Configuration {
    WORD mode, trigger[5], edge, resolution, dead_time, header, max_hits;
    WORD error_mask, fifo_size, channels[V1190_CHANNEL_MASK_WORDS];
    WORD control, status, firmware, rom_version;
    WORD pout_selection, almost_full_level_words;
};

static bool read_v1190_configuration(V1190Configuration &c)
{
    return v1190_read_acquisition_mode(c.mode) &&
           v1190_micro_read_command(V1190_OPCODE_READ_TRIGGER_CONFIG, c.trigger, 5) &&
           v1190_micro_read_command(V1190_OPCODE_READ_EDGE_MODE, &c.edge, 1) &&
           v1190_micro_read_command(V1190_OPCODE_READ_RESOLUTION, &c.resolution, 1) &&
           v1190_micro_read_command(V1190_OPCODE_READ_DEAD_TIME, &c.dead_time, 1) &&
           v1190_micro_read_command(V1190_OPCODE_READ_TDC_HEADER, &c.header, 1) &&
           v1190_micro_read_command(V1190_OPCODE_READ_MAX_HITS, &c.max_hits, 1) &&
           v1190_micro_read_command(V1190_OPCODE_READ_ERROR_MASK, &c.error_mask, 1) &&
           v1190_micro_read_command(V1190_OPCODE_READ_FIFO_SIZE, &c.fifo_size, 1) &&
           v1190_micro_read_command(V1190_OPCODE_READ_CHANNEL_MASK, c.channels,
                                    V1190_CHANNEL_MASK_WORDS) &&
           vme_read16(V1190_BASE + V1190_CONTROL, c.control, "V1190 Control") &&
           vme_read16(V1190_BASE + V1190_STATUS, c.status, "V1190 Status") &&
           vme_read16(V1190_BASE + V1190_OUT_PROG, c.pout_selection,
                      "V1190 POUT selection") &&
           vme_read16(V1190_BASE + V1190_ALMOST_FULL_LEVEL,
                      c.almost_full_level_words, "V1190 Almost Full Level") &&
           vme_read16(V1190_BASE + V1190_FIRMWARE_REVISION, c.firmware,
                      "V1190 Firmware Revision") &&
           vme_read16(V1190_BASE + V1190_CONFIGURATION_ROM_VERSION,
                      c.rom_version, "V1190 Configuration ROM Version");
}

static int decode_signed_12(WORD value)
{
    int result=value&0x0FFF;
    return (result&0x0800)?result-0x1000:result;
}

static const char *v1190_pout_function(WORD selection)
{
    static const char *const names[] = {
        "DATA_READY", "FULL", "ALMOST_FULL", "ERROR"
    };
    return selection < 4 ? names[selection] : "UNKNOWN";
}

/* POUT is a frontend hardware setting, independent of BOR run settings. */
static bool configure_v1190_pout_startup()
{
    if (!gV1190RunSettings.enabled) return true;
    if (!vme_write16(V1190_BASE + V1190_OUT_PROG,
                     V1190_POUT_ALMOST_FULL, "V1190 startup POUT ALMOST_FULL"))
        return false;

    WORD actual = 0;
    if (!vme_read16(V1190_BASE + V1190_OUT_PROG, actual,
                    "V1190 startup POUT readback"))
        return false;
    if (actual != V1190_POUT_ALMOST_FULL) {
        cm_msg(MERROR, frontend_name,
               "V1190 startup POUT mismatch: expected ALMOST_FULL (0x%04X), actual %s (0x%04X)",
               V1190_POUT_ALMOST_FULL, v1190_pout_function(actual), actual);
        return false;
    }

    WORD almost_full_level = 0;
    if (!vme_read16(V1190_BASE + V1190_ALMOST_FULL_LEVEL,
                    almost_full_level, "V1190 startup Almost Full Level"))
        return false;
    const char *function = v1190_pout_function(actual);
    if (!set_module_output(V1190_READBACK_PATH, "POUTSelection", &actual,
                           sizeof(actual), 1, TID_WORD) ||
        !set_module_output(V1190_READBACK_PATH, "POUTFunction", function,
                           strlen(function) + 1, 1, TID_STRING) ||
        !set_module_output(V1190_READBACK_PATH, "AlmostFullLevelWords",
                           &almost_full_level, sizeof(almost_full_level),
                           1, TID_WORD)) {
        cm_msg(MERROR, frontend_name,
               "Cannot publish V1190 startup POUT readback");
        return false;
    }
    cm_msg(MINFO, frontend_name,
           "V1190 startup POUT expected=ALMOST_FULL (0x%04X) actual=%s (0x%04X); Almost Full Level=%u words",
           V1190_POUT_ALMOST_FULL, function, actual, almost_full_level);
    return true;
}

static bool encode_v1190_semantics(const V1190Settings &s, WORD &resolution,
                                   WORD &dead_time, WORD &max_hits,
                                   WORD (&channels)[V1190_CHANNEL_MASK_WORDS])
{
    if (s.window_width>0x0FFF || s.window_offset < -2048 || s.window_offset > 2047 ||
        s.extra_search_margin>0x0FFF || s.reject_margin>0x0FFF || s.edge_mode<1 || s.edge_mode>3) {
        cm_msg(MERROR,frontend_name,"Invalid V1190 trigger window or EdgeMode setting"); return false;
    }
    if (s.resolution_ps==800) resolution=0;
    else if (s.resolution_ps==200) resolution=1;
    else if (s.resolution_ps==100) resolution=2;
    else { cm_msg(MERROR,frontend_name,"V1190 ResolutionPs must be 800, 200, or 100"); return false; }
    if (s.dead_time_ns==5) dead_time=0;
    else if (s.dead_time_ns==10) dead_time=1;
    else if (s.dead_time_ns==30) dead_time=2;
    else if (s.dead_time_ns==100) dead_time=3;
    else { cm_msg(MERROR,frontend_name,"V1190 DeadTimeNs must be 5, 10, 30, or 100"); return false; }
    if (s.max_hits_per_event==-1) max_hits=9;
    else if (s.max_hits_per_event==0) max_hits=0;
    else {
        const int values[]={1,2,4,8,16,32,64,128}; max_hits=0xFFFF;
        for (unsigned i=0;i<8;++i) if (s.max_hits_per_event==values[i]) max_hits=i+1;
        if (max_hits==0xFFFF) { cm_msg(MERROR,frontend_name,"V1190 MaxHitsPerEvent must be -1, 0, or a power of two from 1 through 128"); return false; }
    }
    memset(channels,0,sizeof(channels));
    for (unsigned i=0;i<128;++i) if (s.channel_enabled[i]) channels[i/16]|=WORD(1u<<(i%16));
    return true;
}

static DWORD resolution_ps_from_code(WORD code) { const DWORD v[]={800,200,100,0}; return v[code&3]; }
static DWORD dead_time_ns_from_code(WORD code) { const DWORD v[]={5,10,30,100}; return v[code&3]; }
static INT max_hits_from_code(WORD code) { code&=0xF; if(code==9)return -1; if(code==0)return 0; return code<=8?INT(1u<<(code-1)):-2; }

static bool validate_and_snapshot_module_settings()
{
    V792Settings a={}; V1190Settings b={}; V775Settings c={};
    if(!read_v792_settings(a)||!read_v1190_settings(b)||!read_v775_settings(c)) return false;
    if(a.enabled && a.iped>0xFF) { cm_msg(MERROR,frontend_name,"V792 Iped exceeds 8-bit range"); return false; }
    WORD r=0,d=0,h=0,m[V1190_CHANNEL_MASK_WORDS]={};
    if(b.enabled && !encode_v1190_semantics(b,r,d,h,m)) return false;
    if(c.enabled && c.full_scale_range>0xFF) { cm_msg(MERROR,frontend_name,"V775 FullScaleRange exceeds 8-bit range"); return false; }
    gV792RunSettings=a; gV1190RunSettings=b; gV775RunSettings=c; return true;
}

static bool snapshot_rpv130_enabled_for_run()
{
    char path[256];
    BOOL enabled = FALSE, single_event_busy = FALSE;
    if (!make_odb_path(path, sizeof(path), RPV130_SETTINGS_PATH, "Enabled") ||
        !get_absolute_odb_value(path, &enabled, sizeof(enabled), TID_BOOL))
        return false;
    gRpv130EnabledForRun = enabled != FALSE;
    if (!make_odb_path(path, sizeof(path), RPV130_SETTINGS_PATH,
                       "SingleEventBusyEnabled") ||
        !get_absolute_odb_value(path, &single_event_busy,
                                sizeof(single_event_busy), TID_BOOL))
        return false;
    gSingleEventBusyEnabledForRun = single_event_busy != FALSE;
    return true;
}

static void capture_vme_requested_snapshot()
{
    gVmeRunSnapshot.v792_requested = gV792RunSettings;
    gVmeRunSnapshot.v1190_requested = gV1190RunSettings;
    gVmeRunSnapshot.v775_requested = gV775RunSettings;
    gVmeRunSnapshot.v1720e_requested = gV1720RunSettings;
    gVmeRunSnapshot.rpv130_enabled =
        gRpv130EnabledForRun ? TRUE : FALSE;
    gVmeRunSnapshot.rpv130_single_event_busy_enabled =
        gSingleEventBusyEnabledForRun ? TRUE : FALSE;
    gVmeRunSnapshot.enabled_for_run =
        (gV792RunSettings.enabled || gV1190RunSettings.enabled ||
         gV775RunSettings.enabled || gV1720RunSettings.enabled ||
         gRpv130EnabledForRun) ? TRUE : FALSE;
}

static void publish_vme_enabled_for_run()
{
    gV792Runtime.enabled_for_run = gV792RunSettings.enabled;
    gV1190Runtime.enabled_for_run = gV1190RunSettings.enabled;
    gV775Runtime.enabled_for_run = gV775RunSettings.enabled;
    gV1720Runtime.enabled_for_run = gV1720RunSettings.enabled;
    gV792Runtime.dirty = true;
    gV1190Runtime.dirty = true;
    gV775Runtime.dirty = true;
    gV1720Runtime.dirty = true;
    publish_v7xx_variables(V792_VARIABLES_PATH, gV792Runtime,
                           gV792LastVariablesPublish);
    publish_v1190_variables();
    publish_v7xx_variables(V775_VARIABLES_PATH, gV775Runtime,
                           gV775LastVariablesPublish);
    publish_v1720e_variables();
    if (gRpv130EnabledForRun) {
        const BOOL yes = TRUE;
        set_module_output(RPV130_VARIABLES_PATH, "EnabledForRun", &yes,
                          sizeof(yes), 1, TID_BOOL);
    } else {
        publish_rpv130_disabled_state();
    }
}

static bool validate_v792_event_source_dependency()
{
    if (!gV792RunSettings.enabled &&
        (gV1190RunSettings.enabled || gV775RunSettings.enabled ||
         gV1720RunSettings.enabled)) {
        cm_msg(MERROR, frontend_name,
               "V792 must be enabled when any VME physics readout module "
               "is enabled");
        return false;
    }
    return true;
}

[[maybe_unused]] static bool log_current_configuration(const char *phase)
{
    WORD vf = 0, vs1 = 0, vs2 = 0, vb = 0, iped = 0;
    WORD tf = 0, ts1 = 0, ts2 = 0, tb = 0, fsr = 0;
    WORD tth[V775_MAX_CHANNELS];
    WORD vth[V792_MAX_CHANNELS];
    V1190Configuration c = {};
    if (!vme_read16(V792_BASE + V792_FIRM_REV, vf, "V792 Firmware") ||
        !vme_read16(V792_BASE + V792_CSR1_RO, vs1, "V792 Status 1") ||
        !vme_read16(V792_BASE + V792_CSR2_RO, vs2, "V792 Status 2") ||
        !vme_read16(V792_BASE + V792_BIT_SET2_RW, vb, "V792 Bit Set 2") ||
        !vme_read16(V792_BASE + V792_IPED_RW, iped, "V792 Iped") ||
        !read_v1190_configuration(c)) return false;
    if (gV775RunSettings.enabled &&
        (!vme_read16(V775_BASE + V775_FIRMWARE_REVISION, tf, "V775 Firmware") ||
         !vme_read16(V775_BASE + V775_STATUS1, ts1, "V775 Status 1") ||
         !vme_read16(V775_BASE + V775_STATUS2, ts2, "V775 Status 2") ||
         !vme_read16(V775_BASE + V775_BIT_SET2, tb, "V775 Bit Set 2") ||
         !vme_read16(V775_BASE + V775_FULL_SCALE_RANGE, fsr, "V775 FSR") ||
         v775_ThresholdRead(gVme, V775_BASE, tth) != V775_MAX_CHANNELS)) return false;
    if (v792_ThresholdRead(gVme, V792_BASE, vth) != V792_MAX_CHANNELS) return false;
    printf("BOR configuration snapshot (%s):\n", phase);
    printf("  V792 : firmware=0x%04X status=[0x%04X,0x%04X] BitSet2=0x%04X Iped=0x%04X\n",
           vf, vs1, vs2, vb, iped);
    printf("  V1190: firmware=0x%04X status=0x%04X control=0x%04X mode=%u trigger=[%u,%d,%u,%u,%u] edge=%u res=%u dead=%u hits=%u header=%u error=0x%03X fifo=%u\n",
           c.firmware, c.status, c.control, c.mode & 1, c.trigger[0],
           static_cast<int16_t>(c.trigger[1]), c.trigger[2], c.trigger[3],
           c.trigger[4] & 1, c.edge & 3, c.resolution & 3, c.dead_time & 3,
           c.max_hits & 0xF, c.header & 1, c.error_mask & 0x7FF, c.fifo_size & 0xF);
    printf("  V1190 channel mask:");
    for (size_t i = 0; i < V1190_CHANNEL_MASK_WORDS; ++i) printf(" %04X", c.channels[i]);
    if (gV775RunSettings.enabled) {
        printf("\n  V775 : firmware=0x%04X status=[0x%04X,0x%04X] BitSet2=0x%04X FSR=0x%04X\n",
               tf, ts1, ts2, tb, fsr);
        printf("    VALID=0 datum write: %s (invalid datum %s buffer)\n",
               tb & V775_BIT2_VALID_CONTROL ? "ENABLED" : "DISABLED",
               tb & V775_BIT2_VALID_CONTROL ? "is written to" : "is not written to");
    } else {
        printf("\n  V775 : DISABLED (no access)\n");
    }
    printf("  V792 thresholds:");
    for (unsigned i = 0; i < V792_MAX_CHANNELS; ++i) printf(" %03X", vth[i]);
    if (gV775RunSettings.enabled) {
        printf("\n  V775 thresholds:");
        for (unsigned i = 0; i < V775_MAX_CHANNELS; ++i) printf(" %03X", tth[i]);
    }
    printf("\n");
    return true;
}

static bool configure_v792_for_run()
{
    if (!gV792RunSettings.enabled) return true;
    const DWORD zsreg=gV792RunSettings.zero_suppression_enabled?V792_BIT_CLEAR2_WO:V792_BIT_SET2_RW;
    const DWORD atreg=gV792RunSettings.all_trigger_enabled?V792_BIT_SET2_RW:V792_BIT_CLEAR2_WO;
    return vme_write16(V792_BASE + V792_IPED_RW, gV792RunSettings.iped, "V792 Iped") &&
           vme_write16(V792_BASE + zsreg,V792_BIT2_LOW_THRESHOLD,"V792 zero suppression") &&
           vme_write16(V792_BASE + atreg,V792_BIT2_ALL_TRIGGER,"V792 ALL TRG");
}

static bool configure_v1190_control_for_run(WORD current)
{
    const WORD mask = V1190_CONTROL_EMPTY_EVENT |
                      V1190_CONTROL_EVENT_FIFO |
                      V1190_CONTROL_EXT_TRIGGER_TIME;
    WORD requested = 0;
    if (gV1190RunSettings.empty_event_enabled)
        requested |= V1190_CONTROL_EMPTY_EVENT;
    /* The source-selected BLT mode requires a FIFO entry for each event. */
    if (gV1190RunSettings.event_fifo_enabled ||
        V1190_READOUT_MODE_SELECT == V1190_EVENT_FIFO_BLT32)
        requested |= V1190_CONTROL_EVENT_FIFO;
    if (gV1190RunSettings.extended_trigger_time_enabled)
        requested |= V1190_CONTROL_EXT_TRIGGER_TIME;
    const WORD control = WORD((current & ~mask) | requested);
    if (!vme_write16(V1190_BASE + V1190_CONTROL, control,
                     "V1190 Control run settings")) return false;
    if (V1190_READOUT_MODE_SELECT == V1190_EVENT_FIFO_BLT32) {
        WORD readback = 0;
        if (!vme_read16(V1190_BASE + V1190_CONTROL, readback,
                        "V1190 Event FIFO enable readback")) return false;
        if (!(readback & V1190_CONTROL_EVENT_FIFO)) {
            cm_msg(MERROR, frontend_name,
                   "V1190 Event FIFO enable readback failed: Control=0x%04X",
                   readback);
            return false;
        }
    }
    return true;
}

static bool configure_v1190_for_run()
{
    if (!gV1190RunSettings.enabled) return true;
    const WORD width=gV1190RunSettings.window_width;
    const WORD offset=WORD(gV1190RunSettings.window_offset)&0x0FFF;
    const WORD extra=gV1190RunSettings.extra_search_margin, reject=gV1190RunSettings.reject_margin;
    const WORD edge=gV1190RunSettings.edge_mode; WORD resolution=0,dead=0,hits=0;
    WORD channels[V1190_CHANNEL_MASK_WORDS]={};
    if(!encode_v1190_semantics(gV1190RunSettings,resolution,dead,hits,channels)) return false;
    WORD control = 0;
    if (!v1190_micro_write_command(gV1190RunSettings.trigger_matching_enabled?V1190_OPCODE_TRIGGER_MATCH:V1190_OPCODE_CONTINUOUS,NULL,0) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_WINDOW_WIDTH, &width, 1) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_WINDOW_OFFSET, &offset, 1) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_EXTRA_MARGIN, &extra, 1) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_REJECT_MARGIN, &reject, 1) ||
        !v1190_micro_write_command(gV1190RunSettings.trigger_subtraction_enabled?V1190_OPCODE_ENABLE_TRIGGER_SUBTRACTION:V1190_OPCODE_DISABLE_TRIGGER_SUBTRACTION,NULL,0) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_EDGE_MODE, &edge, 1) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_RESOLUTION, &resolution, 1) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_DEAD_TIME, &dead, 1) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_MAX_HITS, &hits, 1) ||
        !v1190_micro_write_command(V1190_OPCODE_WRITE_CHANNEL_MASK,channels,V1190_CHANNEL_MASK_WORDS) ||
        !v1190_micro_write_command(gV1190RunSettings.tdc_header_enabled?V1190_OPCODE_ENABLE_TDC_HEADER:V1190_OPCODE_DISABLE_TDC_HEADER,NULL,0) ||
        !vme_read16(V1190_BASE + V1190_CONTROL, control, "V1190 Control RMW read")) return false;
    return configure_v1190_control_for_run(control);
}

static bool configure_v775_for_run()
{
    if (!gV775RunSettings.enabled) return true;
    WORD set=0,clear=0;
#define V775BIT(flag,bit) do { if(flag) set|=bit; else clear|=bit; } while(0)
    V775BIT(gV775RunSettings.over_range_enabled,V775_BIT2_OVER_RANGE); V775BIT(gV775RunSettings.low_threshold_enabled,V775_BIT2_LOW_THRESHOLD);
    V775BIT(gV775RunSettings.common_stop,V775_BIT2_COMMON_STOP); V775BIT(gV775RunSettings.empty_program_enabled,V775_BIT2_EMPTY_PROGRAM);
    V775BIT(gV775RunSettings.valid_control_enabled,V775_BIT2_VALID_CONTROL); V775BIT(gV775RunSettings.sliding_scale_enabled,V775_BIT2_SLIDE_ENABLE);
    V775BIT(gV775RunSettings.all_trigger_enabled,V775_BIT2_ALL_TRIGGER);
#undef V775BIT
    return vme_write16(V775_BASE + V775_FULL_SCALE_RANGE, gV775RunSettings.full_scale_range,
                       "V775 Full Scale Range") &&
           vme_write16(V775_BASE + V775_BIT_SET2, set,
                       "V775 run bits set") &&
           vme_write16(V775_BASE + V775_BIT_CLEAR2, clear,
                       "V775 run bits clear");
}
static bool configure_v1720e_for_run()
{
    if (!gV1720RunSettings.enabled) {
        set_v1720e_communication_ok(false);
        set_v1720e_readback_valid(false);
        printf("  V1720E: DISABLED by BOR Settings snapshot; configuration skipped.\n");
        return true;
    }
    V1720E_BOARD_INFO before = {};
    const int probe_status = v1720e_probe(gVme, V1720E_BASE, &before);
    if (probe_status != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name,
               "V1720E pre-configuration read failed: status %d",
               probe_status);
        set_v1720e_communication_ok(false);
        set_v1720e_readback_valid(false);
        return false;
    }
    printf("  V1720E Event Stored before BOR configuration: %u\n",
           before.event_stored);
    const int status = v1720e_configure(gVme, V1720E_BASE,
                                        &gV1720RunConfig);
    if (status != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name,
               "V1720E configuration/readback failed: status %d", status);
        set_v1720e_communication_ok(false);
        set_v1720e_readback_valid(false);
        return false;
    }
    V1720E_CONFIG_READBACK readback = {};
    const int readback_status =
        v1720e_read_configuration(gVme, V1720E_BASE, &readback);
    if (readback_status != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name,
               "V1720E configuration readback failed: status %d",
               readback_status);
        set_v1720e_communication_ok(false);
        set_v1720e_readback_valid(false);
        return false;
    }
    const bool verified = verify_v1720e_readback(gV1720RunConfig, readback);
    capture_v1720e_run_readback(readback, verified);
    publish_v1720e_readback(readback, verified);
    if (!verified) {
        set_v1720e_communication_ok(false);
        return false;
    }
    printf("  V1720E: BufferOrg=0x%X CustomSize=0x%X PostTrigger=0x%X "
           "TriggerSource=0x%08X ChannelMask=0x%02X\n",
           gV1720RunConfig.buffer_organization,
           gV1720RunConfig.custom_size, gV1720RunConfig.post_trigger,
           gV1720RunConfig.trigger_source, gV1720RunConfig.channel_enable);
    return true;
}

static bool verify_value(const char *module, const char *item,
                         unsigned expected, unsigned actual)
{
    if (expected == actual) return true;
    cm_msg(MERROR, frontend_name,
           "%s configuration verify failed: %s expected 0x%X, read back 0x%X",
           module, item, expected, actual);
    return false;
}

static bool verify_v792_configuration()
{
    if(!gV792RunSettings.enabled) { set_module_readback_valid(V792_READBACK_PATH,false); return true; }
    WORD iped=0,bits=0,firmware=0,thresholds[32]={};
    if (!vme_read16(V792_BASE + V792_IPED_RW, iped, "V792 Iped verify") ||
        !vme_read16(V792_BASE + V792_BIT_SET2_RW,bits,"V792 Bit Set 2 verify") ||
        !vme_read16(V792_BASE + V792_FIRM_REV,firmware,"V792 Firmware verify") ||
        v792_ThresholdRead(gVme,V792_BASE,thresholds)!=V792_MAX_CHANNELS) return false;
    const BOOL zs=!(bits&V792_BIT2_LOW_THRESHOLD), all=!!(bits&V792_BIT2_ALL_TRIGGER);
    bool ok=verify_value("V792","Iped",gV792RunSettings.iped,iped&0xFF);
    ok=verify_value("V792","ZeroSuppression",gV792RunSettings.zero_suppression_enabled,zs)&&ok;
    ok=verify_value("V792","AllTrigger",gV792RunSettings.all_trigger_enabled,all)&&ok;
    gVmeRunSnapshot.v792_readback.valid = ok ? TRUE : FALSE;
    gVmeRunSnapshot.v792_readback.firmware_revision = firmware;
    gVmeRunSnapshot.v792_readback.iped = iped;
    gVmeRunSnapshot.v792_readback.zero_suppression_enabled = zs;
    gVmeRunSnapshot.v792_readback.all_trigger_enabled = all;
    gVmeRunSnapshot.v792_readback.bit_set2_raw = bits;
    memcpy(gVmeRunSnapshot.v792_readback.threshold, thresholds,
           sizeof(thresholds));
    set_module_output(V792_READBACK_PATH,"FirmwareRevision",&firmware,sizeof(firmware),1,TID_WORD);
    set_module_output(V792_READBACK_PATH,"Iped",&iped,sizeof(iped),1,TID_WORD);
    set_module_output(V792_READBACK_PATH,"ZeroSuppressionEnabled",&zs,sizeof(zs),1,TID_BOOL);
    set_module_output(V792_READBACK_PATH,"AllTriggerEnabled",&all,sizeof(all),1,TID_BOOL);
    set_module_output(V792_READBACK_PATH,"BitSet2Raw",&bits,sizeof(bits),1,TID_WORD);
    set_module_output(V792_READBACK_PATH,"Threshold",thresholds,sizeof(thresholds),32,TID_WORD);
    set_module_readback_valid(V792_READBACK_PATH,ok); return ok;
}

static bool verify_v1190_configuration()
{
    if(!gV1190RunSettings.enabled) { set_module_readback_valid(V1190_READBACK_PATH,false); return true; }
    V1190Configuration c = {};
    if (!read_v1190_configuration(c)) return false;
    WORD er=0,ed=0,eh=0,channels[V1190_CHANNEL_MASK_WORDS]={};
    if(!encode_v1190_semantics(gV1190RunSettings,er,ed,eh,channels)) return false;
    bool ok = true;
#define V1190_VERIFY(item, expected, actual) \
    do { ok = verify_value("V1190", item, expected, actual) && ok; } while (0)
    V1190_VERIFY("Acquisition mode",gV1190RunSettings.trigger_matching_enabled,c.mode&1);
    V1190_VERIFY("Window width",gV1190RunSettings.window_width,c.trigger[0]&0xFFF);
    V1190_VERIFY("Window offset",WORD(gV1190RunSettings.window_offset)&0xFFF,c.trigger[1]&0xFFF);
    V1190_VERIFY("Extra search margin",gV1190RunSettings.extra_search_margin,c.trigger[2]&0xFFF);
    V1190_VERIFY("Reject margin",gV1190RunSettings.reject_margin,c.trigger[3]&0xFFF);
    V1190_VERIFY("Trigger subtraction",gV1190RunSettings.trigger_subtraction_enabled,c.trigger[4]&1);
    V1190_VERIFY("Edge mode",gV1190RunSettings.edge_mode,c.edge&3);
    V1190_VERIFY("Resolution",er,c.resolution&3); V1190_VERIFY("Dead time",ed,c.dead_time&3);
    V1190_VERIFY("Maximum hits",eh,c.max_hits&0xF); V1190_VERIFY("TDC Header/Trailer",gV1190RunSettings.tdc_header_enabled,c.header&1);
    for (size_t i = 0; i < V1190_CHANNEL_MASK_WORDS; ++i)
        V1190_VERIFY("Channel mask word",channels[i],c.channels[i]);
    V1190_VERIFY("Empty Event",gV1190RunSettings.empty_event_enabled,!!(c.control&V1190_CONTROL_EMPTY_EVENT));
    V1190_VERIFY("Event FIFO",
                 gV1190RunSettings.event_fifo_enabled ||
                     V1190_READOUT_MODE_SELECT == V1190_EVENT_FIFO_BLT32,
                 !!(c.control&V1190_CONTROL_EVENT_FIFO));
    V1190_VERIFY("Extended Trigger Time Tag",gV1190RunSettings.extended_trigger_time_enabled,
                 !!(c.control & V1190_CONTROL_EXT_TRIGGER_TIME));
    V1190_VERIFY("POUT selection", V1190_POUT_ALMOST_FULL,
                 c.pout_selection);
    cm_msg(MINFO, frontend_name,
           "V1190 POUT expected=ALMOST_FULL (%u) actual=%s (0x%04X); Almost Full Level=%u words",
           V1190_POUT_ALMOST_FULL,
           v1190_pout_function(c.pout_selection),
           c.pout_selection, c.almost_full_level_words);
#undef V1190_VERIFY
    V1190Settings rb={}; rb.trigger_matching_enabled=!!(c.mode&1); rb.window_width=c.trigger[0]&0xFFF;
    rb.window_offset=decode_signed_12(c.trigger[1]); rb.extra_search_margin=c.trigger[2]&0xFFF; rb.reject_margin=c.trigger[3]&0xFFF;
    rb.trigger_subtraction_enabled=!!(c.trigger[4]&1); rb.edge_mode=c.edge&3; rb.resolution_ps=resolution_ps_from_code(c.resolution);
    rb.dead_time_ns=dead_time_ns_from_code(c.dead_time); rb.max_hits_per_event=max_hits_from_code(c.max_hits); rb.tdc_header_enabled=!!(c.header&1);
    rb.empty_event_enabled=!!(c.control&V1190_CONTROL_EMPTY_EVENT); rb.event_fifo_enabled=!!(c.control&V1190_CONTROL_EVENT_FIFO); rb.extended_trigger_time_enabled=!!(c.control&V1190_CONTROL_EXT_TRIGGER_TIME);
    for(unsigned i=0;i<128;++i) rb.channel_enabled[i]=!!(c.channels[i/16]&(1u<<(i%16)));
    const char *board=(c.rom_version&0xFF)==0?"V1190A":((c.rom_version&0xFF)==1?"V1190B":"Unknown");
    gVmeRunSnapshot.v1190_readback.valid = ok ? TRUE : FALSE;
    gVmeRunSnapshot.v1190_readback.firmware_revision = c.firmware;
    gVmeRunSnapshot.v1190_readback.configuration_rom_version = c.rom_version;
    snprintf(gVmeRunSnapshot.v1190_readback.board_type,
             sizeof(gVmeRunSnapshot.v1190_readback.board_type), "%s", board);
    gVmeRunSnapshot.v1190_readback.settings = rb;
    gVmeRunSnapshot.v1190_readback.error_mask = c.error_mask & 0x7FF;
    gVmeRunSnapshot.v1190_readback.effective_fifo_size_words =
        (c.fifo_size & 0xF) <= 7 ? (1u << ((c.fifo_size & 0xF) + 1)) : 0;
    gVmeRunSnapshot.v1190_readback.control_raw = c.control;
    gVmeRunSnapshot.v1190_readback.pout_selection = c.pout_selection;
    snprintf(gVmeRunSnapshot.v1190_readback.pout_function,
             sizeof(gVmeRunSnapshot.v1190_readback.pout_function), "%s",
             v1190_pout_function(c.pout_selection));
    gVmeRunSnapshot.v1190_readback.almost_full_level_words =
        c.almost_full_level_words;
#define P1190(k,m,cnt,t) set_module_output(V1190_READBACK_PATH,k,&rb.m,sizeof(rb.m),cnt,t)
    set_module_output(V1190_READBACK_PATH,"FirmwareRevision",&c.firmware,sizeof(c.firmware),1,TID_WORD); set_module_output(V1190_READBACK_PATH,"ConfigurationRomVersion",&c.rom_version,sizeof(c.rom_version),1,TID_WORD); set_module_output(V1190_READBACK_PATH,"BoardType",board,strlen(board)+1,1,TID_STRING);
    P1190("TriggerMatchingEnabled",trigger_matching_enabled,1,TID_BOOL); P1190("WindowWidth",window_width,1,TID_DWORD); P1190("WindowOffset",window_offset,1,TID_INT); P1190("ExtraSearchMargin",extra_search_margin,1,TID_DWORD); P1190("RejectMargin",reject_margin,1,TID_DWORD); P1190("TriggerSubtractionEnabled",trigger_subtraction_enabled,1,TID_BOOL); P1190("EdgeMode",edge_mode,1,TID_DWORD); P1190("ResolutionPs",resolution_ps,1,TID_DWORD); P1190("DeadTimeNs",dead_time_ns,1,TID_DWORD); P1190("MaxHitsPerEvent",max_hits_per_event,1,TID_INT); P1190("TdcHeaderEnabled",tdc_header_enabled,1,TID_BOOL); P1190("EmptyEventEnabled",empty_event_enabled,1,TID_BOOL); P1190("EventFifoEnabled",event_fifo_enabled,1,TID_BOOL); P1190("ExtendedTriggerTimeEnabled",extended_trigger_time_enabled,1,TID_BOOL); P1190("ChannelEnabled",channel_enabled,128,TID_BOOL);
#undef P1190
    WORD error=c.error_mask&0x7FF; DWORD fifo=(c.fifo_size&0xF)<=7?(1u<<((c.fifo_size&0xF)+1)):0;
    set_module_output(V1190_READBACK_PATH,"ErrorMask",&error,sizeof(error),1,TID_WORD); set_module_output(V1190_READBACK_PATH,"EffectiveFifoSizeWords",&fifo,sizeof(fifo),1,TID_DWORD); set_module_output(V1190_READBACK_PATH,"ControlRaw",&c.control,sizeof(c.control),1,TID_WORD);
    set_module_output(V1190_READBACK_PATH,"POUTSelection",&c.pout_selection,sizeof(c.pout_selection),1,TID_WORD);
    const char *pout_function = v1190_pout_function(c.pout_selection);
    set_module_output(V1190_READBACK_PATH,"POUTFunction",pout_function,strlen(pout_function)+1,1,TID_STRING);
    set_module_output(V1190_READBACK_PATH,"AlmostFullLevelWords",&c.almost_full_level_words,sizeof(c.almost_full_level_words),1,TID_WORD);
    set_module_readback_valid(V1190_READBACK_PATH,ok);
    return ok;
}

static bool verify_v775_configuration()
{
    if(!gV775RunSettings.enabled) { set_module_readback_valid(V775_READBACK_PATH,false); return true; }
    WORD fsr=0,bits=0,firmware=0,fclr=0,thresholds[32]={};
    if (!vme_read16(V775_BASE + V775_FULL_SCALE_RANGE, fsr, "V775 FSR verify") ||
        !vme_read16(V775_BASE+V775_BIT_SET2,bits,"V775 Bit Set 2 verify") || !vme_read16(V775_BASE+V775_FIRMWARE_REVISION,firmware,"V775 Firmware") || !vme_read16(V775_BASE+V775_FCLR_WINDOW,fclr,"V775 Fast Clear") || v775_ThresholdRead(gVme,V775_BASE,thresholds)!=V775_MAX_CHANNELS) return false;
    bool ok=verify_value("V775","Full Scale Range",gV775RunSettings.full_scale_range,fsr&0xFF);
#define VV775(name,member,bit) ok=verify_value("V775",name,gV775RunSettings.member,!!(bits&bit))&&ok
    VV775("OverRange",over_range_enabled,V775_BIT2_OVER_RANGE); VV775("LowThreshold",low_threshold_enabled,V775_BIT2_LOW_THRESHOLD); VV775("CommonStop",common_stop,V775_BIT2_COMMON_STOP); VV775("EmptyProgram",empty_program_enabled,V775_BIT2_EMPTY_PROGRAM); VV775("ValidControl",valid_control_enabled,V775_BIT2_VALID_CONTROL); VV775("SlidingScale",sliding_scale_enabled,V775_BIT2_SLIDE_ENABLE); VV775("AllTrigger",all_trigger_enabled,V775_BIT2_ALL_TRIGGER);
#undef VV775
    V775ReadbackSnapshot &snapshot = gVmeRunSnapshot.v775_readback;
    snapshot.valid = ok ? TRUE : FALSE;
    snapshot.firmware_revision = firmware;
    snapshot.full_scale_range = fsr;
    snapshot.fast_clear_window = fclr;
    snapshot.over_range_enabled = !!(bits & V775_BIT2_OVER_RANGE);
    snapshot.low_threshold_enabled = !!(bits & V775_BIT2_LOW_THRESHOLD);
    snapshot.common_stop = !!(bits & V775_BIT2_COMMON_STOP);
    snapshot.empty_program_enabled = !!(bits & V775_BIT2_EMPTY_PROGRAM);
    snapshot.valid_control_enabled = !!(bits & V775_BIT2_VALID_CONTROL);
    snapshot.sliding_scale_enabled = !!(bits & V775_BIT2_SLIDE_ENABLE);
    snapshot.all_trigger_enabled = !!(bits & V775_BIT2_ALL_TRIGGER);
    snapshot.bit_set2_raw = bits;
    memcpy(snapshot.threshold, thresholds, sizeof(thresholds));
    set_module_output(V775_READBACK_PATH,"FirmwareRevision",&firmware,sizeof(firmware),1,TID_WORD); set_module_output(V775_READBACK_PATH,"FullScaleRange",&fsr,sizeof(fsr),1,TID_WORD); set_module_output(V775_READBACK_PATH,"FastClearWindow",&fclr,sizeof(fclr),1,TID_WORD);
#define RB775(k,m,b) { const BOOL v=!!(bits&b); set_module_output(V775_READBACK_PATH,k,&v,sizeof(v),1,TID_BOOL); }
    RB775("OverRangeEnabled",over_range_enabled,V775_BIT2_OVER_RANGE); RB775("LowThresholdEnabled",low_threshold_enabled,V775_BIT2_LOW_THRESHOLD); RB775("CommonStop",common_stop,V775_BIT2_COMMON_STOP); RB775("EmptyProgramEnabled",empty_program_enabled,V775_BIT2_EMPTY_PROGRAM); RB775("ValidControlEnabled",valid_control_enabled,V775_BIT2_VALID_CONTROL); RB775("SlidingScaleEnabled",sliding_scale_enabled,V775_BIT2_SLIDE_ENABLE); RB775("AllTriggerEnabled",all_trigger_enabled,V775_BIT2_ALL_TRIGGER);
#undef RB775
    set_module_output(V775_READBACK_PATH,"BitSet2Raw",&bits,sizeof(bits),1,TID_WORD); set_module_output(V775_READBACK_PATH,"Threshold",thresholds,sizeof(thresholds),32,TID_WORD); set_module_readback_valid(V775_READBACK_PATH,ok); return ok;
}
static bool clear_module_buffers()
{
    if (gV792RunSettings.enabled &&
        (!vme_write16(V792_BASE + V792_BIT_SET2_RW,0x0004,"V792 Data Clear set") || !vme_write16(V792_BASE + V792_BIT_CLEAR2_WO,0x0004,"V792 Data Clear clear"))) return false;
    if (gV1190RunSettings.enabled && !vme_write16(V1190_BASE+V1190_SOFT_CLEAR,0,"V1190 Software Clear")) return false;
    if (gV775RunSettings.enabled &&
        (!vme_write16(V775_BASE + V775_BIT_SET2, V775_BIT2_CLEAR_DATA, "V775 Data Clear set") ||
         !vme_write16(V775_BASE + V775_BIT_CLEAR2, V775_BIT2_CLEAR_DATA, "V775 Data Clear clear"))) return false;
    WORD status = 0;
    if (gV1190RunSettings.enabled && !vme_read16(V1190_BASE+V1190_STATUS,status,"V1190 Status after clear")) return false;
    if ((gV792RunSettings.enabled && v792_DataReady(gVme,V792_BASE)) || (gV1190RunSettings.enabled && (status&V1190_STATUS_DATA_READY))
        || (gV775RunSettings.enabled && v775_DataReady(gVme,V775_BASE))
        ) {
        cm_msg(MERROR, frontend_name, "Buffer clear verify failed: DataReady remains asserted");
        return false;
    }
    return true;
}

static void set_vme_clear_results(const std::string &value)
{
    gBufferClearResults.v792 = value;
    gBufferClearResults.v1190 = value;
    gBufferClearResults.v775 = value;
    gBufferClearResults.v1720e = value;
}

static void process_manual_buffer_clear_request()
{
    DWORD request_id = 0;
    if (!get_absolute_odb_value(BUFFER_CLEAR_COMMAND_PATH, &request_id,
                                sizeof(request_id), TID_DWORD) ||
        request_id <= gBufferClearStatus.last_handled_request_id)
        return;

    const daq::BufferClearTransition transition =
        daq::beginBufferClearRequest(gBufferClearStatus, request_id);
    if (!transition.handled)
        return;
    gBufferClearStatus = transition.pending;
    set_vme_clear_results("Not attempted");
    publish_buffer_clear_status();

    const time_t now = time(NULL);
    const uint64_t unix_time =
        now < 0 ? 0 : static_cast<uint64_t>(now);
    INT current_run_state = 0;
    if (!get_absolute_odb_value("/Runinfo/State", &current_run_state,
                                sizeof(current_run_state), TID_INT)) {
        gBufferClearStatus = daq::rejectBufferClearRequest(
            gBufferClearStatus, "Cannot verify MIDAS Run state", unix_time);
        set_vme_clear_results("Not attempted: Run state unavailable");
        publish_buffer_clear_status();
        return;
    }
    if (current_run_state != STATE_STOPPED) {
        const daq::BufferClearRunState state =
            current_run_state == STATE_RUNNING
                ? daq::BufferClearRunState::kRunning
                : (current_run_state == STATE_PAUSED
                       ? daq::BufferClearRunState::kPaused
                       : daq::BufferClearRunState::kUnknown);
        const std::string error =
            daq::bufferClearRunStateRejection(state, "VME");
        gBufferClearStatus = daq::rejectBufferClearRequest(
            gBufferClearStatus, error, unix_time);
        set_vme_clear_results("Not attempted: " + error);
        publish_buffer_clear_status();
        cm_msg(MINFO, frontend_name, "Request %u rejected: %s", request_id,
               error.c_str());
        return;
    }
    if (!gVme) {
        gBufferClearStatus = daq::rejectBufferClearRequest(
            gBufferClearStatus, "VME interface is not open", unix_time);
        set_vme_clear_results("Not attempted: VME interface unavailable");
        publish_buffer_clear_status();
        return;
    }

    V792Settings v792 = {};
    V1190Settings v1190 = {};
    V775Settings v775 = {};
    V1720ESettings v1720 = {};
    if (!read_v792_settings(v792) || !read_v1190_settings(v1190) ||
        !read_v775_settings(v775) || !read_v1720e_settings(v1720)) {
        gBufferClearStatus = daq::rejectBufferClearRequest(
            gBufferClearStatus, "Cannot snapshot VME module Enabled settings",
            unix_time);
        set_vme_clear_results("Not attempted: ODB Settings unavailable");
        publish_buffer_clear_status();
        return;
    }

    gBufferClearStatus =
        daq::markBufferClearExecuting(gBufferClearStatus);
    publish_buffer_clear_status();
    bool all_ok = true;
    std::string errors;
    const auto fail = [&](const char *module, std::string &result,
                          const std::string &detail) {
        result = "Failed: " + detail;
        if (!errors.empty()) errors += "; ";
        errors += module;
        errors += ": ";
        errors += detail;
        all_ok = false;
    };

    if (!v792.enabled) {
        gBufferClearResults.v792 = "Skipped: disabled in ODB";
    } else if (!vme_write16(V792_BASE + V792_BIT_SET2_RW, 0x0004,
                            "V792 manual Data Clear set") ||
               !vme_write16(V792_BASE + V792_BIT_CLEAR2_WO, 0x0004,
                            "V792 manual Data Clear clear")) {
        fail("V792", gBufferClearResults.v792, "Data Clear write failed");
    } else if (v792_DataReady(gVme, V792_BASE)) {
        fail("V792", gBufferClearResults.v792,
             "DataReady remains asserted after Data Clear");
    } else {
        gBufferClearResults.v792 = "Succeeded: Data Clear";
    }

    WORD v1190_status = 0;
    if (!v1190.enabled) {
        gBufferClearResults.v1190 = "Skipped: disabled in ODB";
    } else if (!vme_write16(V1190_BASE + V1190_SOFT_CLEAR, 0,
                            "V1190 manual Software Clear")) {
        fail("V1190", gBufferClearResults.v1190,
             "Software Clear write failed");
    } else if (!vme_read16(V1190_BASE + V1190_STATUS, v1190_status,
                           "V1190 Status after manual Software Clear")) {
        fail("V1190", gBufferClearResults.v1190,
             "status verification read failed");
    } else if (v1190_status & V1190_STATUS_DATA_READY) {
        fail("V1190", gBufferClearResults.v1190,
             "DataReady remains asserted after Software Clear");
    } else {
        gBufferClearResults.v1190 = "Succeeded: Software Clear";
    }

    if (!v775.enabled) {
        gBufferClearResults.v775 = "Skipped: disabled in ODB";
    } else if (!vme_write16(V775_BASE + V775_BIT_SET2,
                            V775_BIT2_CLEAR_DATA,
                            "V775 manual Data Clear set") ||
               !vme_write16(V775_BASE + V775_BIT_CLEAR2,
                            V775_BIT2_CLEAR_DATA,
                            "V775 manual Data Clear clear")) {
        fail("V775", gBufferClearResults.v775, "Data Clear write failed");
    } else if (v775_DataReady(gVme, V775_BASE)) {
        fail("V775", gBufferClearResults.v775,
             "DataReady remains asserted after Data Clear");
    } else {
        gBufferClearResults.v775 = "Succeeded: Data Clear";
    }

    if (!v1720.enabled) {
        gBufferClearResults.v1720e = "Skipped: disabled in ODB";
    } else {
        DWORD stored = 0;
        int stored_valid = 0;
        const int status =
            v1720e_software_clear(gVme, V1720E_BASE, &stored, &stored_valid);
        if (status != MVME_SUCCESS) {
            const std::string stored_text = stored_valid
                ? std::to_string(stored)
                : "unavailable";
            fail("V1720E", gBufferClearResults.v1720e,
                 std::string("Software Clear failed (status ") +
                     std::to_string(status) + ", EventStored=" +
                     stored_text + ")");
        } else {
            gBufferClearResults.v1720e =
                "Succeeded: Software Clear; EventStored=0";
        }
    }

    gBufferClearStatus = daq::finishBufferClearRequest(
        gBufferClearStatus, all_ok, errors, unix_time);
    publish_buffer_clear_status();
    if (all_ok) {
        cm_msg(MINFO, frontend_name,
               "Request %u VME buffer clear succeeded: V792=%s; V1190=%s; "
               "V775=%s; V1720E=%s; RPV130 untouched",
               request_id, gBufferClearResults.v792.c_str(),
               gBufferClearResults.v1190.c_str(),
               gBufferClearResults.v775.c_str(),
               gBufferClearResults.v1720e.c_str());
    } else {
        cm_msg(MERROR, frontend_name,
               "Request %u VME buffer clear failed: %s; RPV130 untouched",
               request_id, errors.c_str());
    }
}

static bool reset_module_event_counters()
{
    /*
     * No additional VME write is needed here. V792 (and enabled V775) Data
     * Clear resets accepted-event counters because ALL TRG is configured zero.
     * V1190 Software Clear resets both its Output Buffer and Event Counter.
     * verify_run_start_state() reads and logs all enabled counters.
     */
    return true;
}

static bool verify_run_start_state()
{
    WORD status = 0;
    DWORD v792_counter = 0, v1190_counter = 0;
    if(gV792RunSettings.enabled) v792_EvtCntRead(gVme,V792_BASE,&v792_counter);
    DWORD v775_counter = 0;
    if(gV775RunSettings.enabled) v775_EvtCntRead(gVme,V775_BASE,&v775_counter);
    if(gV1190RunSettings.enabled && (!vme_read32(V1190_BASE+V1190_EVENT_COUNTER,v1190_counter,"V1190 Event Counter") || !vme_read16(V1190_BASE+V1190_STATUS,status,"V1190 run-start Status"))) return false;
    printf("BOR event counters: V792=%s0x%06X V1190=%s0x%08X",gV792RunSettings.enabled?"":"DISABLED/",v792_counter&V7XX_EVENT_COUNTER_MASK,gV1190RunSettings.enabled?"":"DISABLED/",v1190_counter);
    printf(" V775=%s0x%06X",gV775RunSettings.enabled?"":"DISABLED/",v775_counter&V7XX_EVENT_COUNTER_MASK);
    printf("\n");
    if ((gV792RunSettings.enabled&&v792_DataReady(gVme,V792_BASE)) || (gV1190RunSettings.enabled&&(status&V1190_STATUS_DATA_READY))
        || (gV775RunSettings.enabled&&v775_DataReady(gVme,V775_BASE))
        ) {
        cm_msg(MERROR, frontend_name, "BOR run-start verify failed: module buffer is not empty");
        return false;
    }
    if (gV1190RunSettings.enabled &&
        V1190_READOUT_MODE_SELECT == V1190_EVENT_FIFO_BLT32) {
        WORD fifo_status = 0, fifo_stored = 0;
        if (!vme_read16(V1190_BASE + V1190_EVENT_FIFO_STATUS,
                        fifo_status, "V1190 run-start Event FIFO Status") ||
            !vme_read16(V1190_BASE + V1190_EVENT_FIFO_STORED,
                        fifo_stored, "V1190 run-start Event FIFO Stored"))
            return false;
        if ((fifo_status & V1190_FIFO_STATUS_DATA_READY) || fifo_stored) {
            cm_msg(MERROR, frontend_name,
                   "BOR run-start verify failed: Event FIFO not empty (status=0x%04X stored=%u)",
                   fifo_status, fifo_stored);
            return false;
        }
    }
    printf("BOR configuration complete:\n  V792  : %s\n  V1190 : %s\n"
           "  V775  : %s\n"
           "  V1720E: %s\n  Buffers empty\n"
           "  Event counters reset\n  Run may start\n",
           gV792RunSettings.enabled?"READY":"DISABLED",gV1190RunSettings.enabled?"READY":"DISABLED",
           gV775RunSettings.enabled?"READY":"DISABLED",
           gV1720RunSettings.enabled ? "READY" : "DISABLED");
    return true;
}

static bool prepare_modules_for_run()
{
    if (!check_module_communication(gV1720RunSettings.enabled != FALSE)) return false;
    if (!configure_v792_for_run() || !configure_v1190_for_run()
        || !configure_v775_for_run()
        || !configure_v1720e_for_run()) return false;
    if (!verify_v792_configuration() || !verify_v1190_configuration()
        || !verify_v775_configuration()
        ) return false;
    if (!clear_module_buffers() || !reset_module_event_counters() ||
        !verify_run_start_state())
        return false;
    if (!gV1720RunSettings.enabled) {
        gV1720StartAttempted = false;
        gV1720Started = false;
        return true;
    }
    gV1720StartAttempted = true;
    const int start_status = v1720e_start(gVme, V1720E_BASE);
    if (start_status != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name,
               "V1720E Acquisition Start failed: status %d", start_status);
        return false;
    }
    gV1720Started = true;
    DWORD v1720_events = 0;
    int v1720_ready = 0;
    DWORD v1720_acquisition_status = 0;
    const int ready_status = v1720e_data_ready(gVme, V1720E_BASE,
                                               &v1720_ready, &v1720_events,
                                               &v1720_acquisition_status);
    if (ready_status != MVME_SUCCESS || v1720_events > 1) {
        cm_msg(MERROR, frontend_name,
               "V1720E post-start buffer verification failed: status %d "
               "ready=%d Event Stored=%u",
               ready_status, v1720_ready, v1720_events);
        stop_v1720e_and_publish_state("BOR post-start verification failure");
        return false;
    }
    printf("  V1720E Acquisition STARTED; Event Stored after RUN memory reset=%u\n",
           v1720_events);
    V1720E_BOARD_INFO state = {};
    state.acquisition_status = v1720_acquisition_status;
    state.event_stored = v1720_events;
    if (v1720e_probe(gVme, V1720E_BASE, &state) == MVME_SUCCESS)
        publish_v1720e_board_state(state);
    return true;
}

static bool vme_configuration_ready()
{
    return gVmeRunSnapshot.frontend_bor_complete == TRUE &&
           (!gV792RunSettings.enabled ||
            gVmeRunSnapshot.v792_readback.valid == TRUE) &&
           (!gV1190RunSettings.enabled ||
            gVmeRunSnapshot.v1190_readback.valid == TRUE) &&
           (!gV775RunSettings.enabled ||
            gVmeRunSnapshot.v775_readback.valid == TRUE) &&
           (!gV1720RunSettings.enabled ||
            (gVmeRunSnapshot.v1720e_readback.valid == TRUE &&
             gV1720Started));
}

#if ENABLE_V792_SW_TRIGGER_TEST
static void setup_v792_sw_trigger_test()
{
    WORD cnt_low = v792_Read16(gVme, V792_BASE, V792_EVT_CNT_L_RO);
    WORD cnt_high = v792_Read16(gVme, V792_BASE, V792_EVT_CNT_H_RO);

    printf("Event counter before reset: high=0x%04X low=0x%04X -> 0x%06X\n",
           cnt_high, cnt_low, ((cnt_high & 0xFF) << 16) | cnt_low);

    v792_SingleShotReset(gVme, V792_BASE);

    cnt_low = v792_Read16(gVme, V792_BASE, V792_EVT_CNT_L_RO);
    cnt_high = v792_Read16(gVme, V792_BASE, V792_EVT_CNT_H_RO);

    printf("Event counter after single-shot reset: high=0x%04X low=0x%04X -> 0x%06X\n",
           cnt_high, cnt_low, ((cnt_high & 0xFF) << 16) | cnt_low);

    v792_Status(gVme, V792_BASE);
    printf("V792 SW trigger test enabled: issuing one SW comm at begin of run.\n");
    v792_Trigger(gVme, V792_BASE);
}
#endif

static void reset_run_statistics()
{
    gReadoutFailed = false;
    gBltStopRequested.store(false, std::memory_order_relaxed);
    gRunStatistics = {};
    gRunStatistics.v1720_min_ttt_delta = 0x7FFFFFFFu;
}

static void log_run_statistics()
{
    printf("Event counter mismatches during run: %llu\n",
           static_cast<unsigned long long>(gRunStatistics.counter_mismatch_count));
    if (gRunStatistics.counter_mismatch_count != 0) {
        printf("First mismatch serial: %u\n", gRunStatistics.first_mismatch_serial);
        printf("Last mismatch serial : %u\n", gRunStatistics.last_mismatch_serial);
    }
    printf("V1720E integrity: malformed=%llu size-errors=%llu mask-errors=%llu "
           "read-timeouts=%llu counter-discontinuities=%llu\n",
           static_cast<unsigned long long>(gRunStatistics.v1720_malformed_count),
           static_cast<unsigned long long>(gRunStatistics.v1720_size_error_count),
           static_cast<unsigned long long>(gRunStatistics.v1720_mask_error_count),
           static_cast<unsigned long long>(gRunStatistics.v1720_read_timeout_count),
           static_cast<unsigned long long>(
               gRunStatistics.v1720_counter_discontinuity_count));
    if (gRunStatistics.v1720_have_previous) {
        printf("V1720E counters: first=%u last=%u; TTT delta count=%llu",
               gRunStatistics.v1720_first_counter,
               gRunStatistics.v1720_last_counter,
               static_cast<unsigned long long>(gRunStatistics.v1720_ttt_count));
        if (gRunStatistics.v1720_ttt_count != 0)
            printf(" min=%u max=%u", gRunStatistics.v1720_min_ttt_delta,
                   gRunStatistics.v1720_max_ttt_delta);
        printf("\n");
    }
}

static void refresh_enabled_module_variables()
{
    if(gV792RunSettings.enabled) {
        WORD s1=0,s2=0; DWORD counter=gV792Runtime.event_counter;
        if(vme_read16(V792_BASE+V792_CSR1_RO,s1,"V792 final Status1")&&vme_read16(V792_BASE+V792_CSR2_RO,s2,"V792 final Status2")) { v792_EvtCntRead(gVme,V792_BASE,&counter); gV792Runtime.communication_ok=TRUE; decode_v7xx_runtime(gV792Runtime,s1,s2,counter); } else gV792Runtime.communication_ok=FALSE;
        publish_v7xx_variables(V792_VARIABLES_PATH,gV792Runtime,gV792LastVariablesPublish);
    }
    if(gV1190RunSettings.enabled) {
        WORD status=0,stored=0; DWORD counter=gV1190Runtime.event_counter;
        if(vme_read16(V1190_BASE+V1190_STATUS,status,"V1190 final Status")&&vme_read16(V1190_BASE+V1190_EVENT_STORED,stored,"V1190 final Event Stored")&&vme_read32(V1190_BASE+V1190_EVENT_COUNTER,counter,"V1190 final Event Counter")) { gV1190Runtime.communication_ok=TRUE; decode_v1190_runtime(status,stored,counter); } else gV1190Runtime.communication_ok=FALSE;
        publish_v1190_variables();
    }
    if(gV775RunSettings.enabled) {
        WORD s1=0,s2=0; DWORD counter=gV775Runtime.event_counter;
        if(vme_read16(V775_BASE+V775_STATUS1,s1,"V775 final Status1")&&vme_read16(V775_BASE+V775_STATUS2,s2,"V775 final Status2")) { v775_EvtCntRead(gVme,V775_BASE,&counter); gV775Runtime.communication_ok=TRUE; decode_v7xx_runtime(gV775Runtime,s1,s2,counter); } else gV775Runtime.communication_ok=FALSE;
        publish_v7xx_variables(V775_VARIABLES_PATH,gV775Runtime,gV775LastVariablesPublish);
    }
}


/* Open VME, check modules, and initialize frontend-owned hardware settings. */
static INT start_abort(INT run_number, char *error);

INT frontend_init()
{
    global_busy::disable_readout();
    INT current_run_state = 0;
    if (!get_absolute_odb_value("/Runinfo/State", &current_run_state,
                                sizeof(current_run_state), TID_INT)) {
        cm_msg(MERROR, frontend_name,
               "Cannot verify MIDAS Run state for V1720E startup recovery");
        return FE_ERR_ODB;
    }
    if (current_run_state != STATE_STOPPED &&
        current_run_state != STATE_RUNNING &&
        current_run_state != STATE_PAUSED) {
        cm_msg(MERROR, frontend_name,
               "Unknown MIDAS Run state %d; refusing V1720E startup recovery",
               current_run_state);
        return FE_ERR_ODB;
    }
    gV1720StartupRunState = current_run_state;
#if ENABLE_V792_SW_TRIGGER_TEST || ENABLE_V1190_SOFT_TRIGGER_TEST || ENABLE_V775_SW_TRIGGER_TEST
    printf("============================================================\n"
           " WARNING: SOFTWARE-TRIGGER DIAGNOSTIC BUILD\n"
           " V792 SW trigger    : ENABLED\n"
           " V1190 Soft trigger : ENABLED\n"
           " V775 SW trigger    : ENABLED\n"
           "============================================================\n");
#else
    printf("Software-trigger diagnostics: disabled\n");
#endif

    if (!initialize_rpv130_odb() || !initialize_other_module_odb() ||
        !initialize_v1720e_odb() || !initialize_run_counters_odb() ||
        !initialize_buffer_clear_mailbox()) {
        cm_msg(MERROR, frontend_name,
               "Cannot initialize VME module ODB schema/settings");
        return FE_ERR_ODB;
    }
    if (!publish_configuration_status(false, 0)) {
        cm_msg(MERROR, frontend_name,
               "Cannot initialize VME frontend configuration status");
        return FE_ERR_ODB;
    }
    reset_vme_run_snapshot(0);
    if (!publish_vme_run_snapshot()) {
        cm_msg(MERROR, frontend_name,
               "Cannot initialize VME RunSnapshot ODB schema");
        return FE_ERR_ODB;
    }
    if (!global_busy::initialize()) return FE_ERR_ODB;
    const INT transition_status =
        cm_register_transition(TR_STARTABORT, start_abort, 500);
    if (transition_status != CM_SUCCESS) {
        cm_msg(MERROR, frontend_name,
               "Cannot register TR_STARTABORT callback: status %d",
               transition_status);
        return transition_status;
    }
    if (cm_register_transition(TR_START, global_busy::before_start, 400) != CM_SUCCESS ||
        cm_register_transition(TR_START, global_busy::after_start, 600) != CM_SUCCESS ||
        cm_register_transition(TR_STOP, global_busy::before_stop, 400) != CM_SUCCESS ||
        cm_register_transition(TR_STARTABORT, global_busy::start_abort, 400) != CM_SUCCESS)
        return FE_ERR_ODB;

    printf("Opening VME interface...\n");

    INT status = mvme_open(&gVme, 0);

    if (status != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "mvme_open() failed: %d", status);
        return FE_ERR_HW;
    }
    log_v3718_out0_diagnostic(gVme);
    global_busy::attach(gVme);
    // OUT0 may be unconfigured or temporarily unavailable. Keep VME diagnostics
    // usable; the START transition will require a successful BUSY assertion.
    global_busy::set_global_busy(true);

    mvme_set_am(gVme, MVME_AM_A24_ND);
    mvme_set_dmode(gVme, MVME_DMODE_D16);

    printf("VME interface opened.\n");
    publish_rpv130_status(true);
    if (!check_module_communication(gV1720StartupEnabled)) {
        mvme_close(gVme);
        gVme = NULL;
        global_busy::attach(NULL);
        return FE_ERR_HW;
    }
    // Recheck just before recovery so a run-state change during initialization
    // cannot use the earlier STOPPED snapshot to authorize a V1720E stop.
    if (!get_absolute_odb_value("/Runinfo/State", &current_run_state,
                                sizeof(current_run_state), TID_INT)) {
        cm_msg(MERROR, frontend_name,
               "Cannot recheck MIDAS Run state before V1720E startup recovery");
        mvme_close(gVme);
        gVme = NULL;
        global_busy::attach(NULL);
        return FE_ERR_ODB;
    }
    gV1720StartupRunState = current_run_state;
    if (current_run_state != STATE_STOPPED &&
        current_run_state != STATE_RUNNING &&
        current_run_state != STATE_PAUSED) {
        cm_msg(MERROR, frontend_name,
               "Unknown MIDAS Run state %d before V1720E startup recovery",
               current_run_state);
        mvme_close(gVme);
        gVme = NULL;
        global_busy::attach(NULL);
        return FE_ERR_ODB;
    }
    if (current_run_state == STATE_STOPPED &&
        !quiesce_rpv130_single_event_busy("STOPPED frontend startup")) {
        mvme_close(gVme);
        gVme = NULL;
        global_busy::attach(NULL);
        return FE_ERR_HW;
    }
    if (gV1720StartupEnabled && current_run_state == STATE_STOPPED) {
        DWORD control = 0, acquisition_status = 0;
        const int read_status = v1720e_read_run_state(
            gVme, V1720E_BASE, &control, &acquisition_status);
        if (read_status != MVME_SUCCESS) {
            cm_msg(MERROR, frontend_name,
                   "Cannot read V1720E RUN state at STOPPED startup: status %d",
                   read_status);
            mvme_close(gVme);
            gVme = NULL;
            global_busy::attach(NULL);
            return FE_ERR_HW;
        }
        if ((control & V1720E_ACQ_RUN) ||
            (acquisition_status & V1720E_ACQ_RUN)) {
            cm_msg(MERROR, frontend_name,
                   "V1720E RUN persisted while MIDAS STOPPED: "
                   "AcqControl=0x%08X AcqStatus=0x%08X; attempting stop",
                   control, acquisition_status);
            if (!stop_v1720e_and_publish_state("STOPPED frontend startup recovery")) {
                cm_msg(MERROR, frontend_name,
                       "V1720E startup recovery failed; fevme startup refused");
                mvme_close(gVme);
                gVme = NULL;
                global_busy::attach(NULL);
                return FE_ERR_HW;
            }
        }
    } else if (current_run_state == STATE_RUNNING ||
               current_run_state == STATE_PAUSED) {
        cm_msg(MINFO, frontend_name,
               "V1720E startup auto-stop skipped: MIDAS state %d",
               current_run_state);
    }
    if (gV1190RunSettings.enabled) {
        INT state_before_pout = 0;
        INT transition_in_progress = 0;
        if (!get_absolute_odb_value("/Runinfo/State", &state_before_pout,
                                    sizeof(state_before_pout), TID_INT) ||
            !get_absolute_odb_value("/Runinfo/Transition in progress",
                                    &transition_in_progress,
                                    sizeof(transition_in_progress), TID_INT)) {
            cm_msg(MERROR, frontend_name,
                   "Cannot verify MIDAS transition state before V1190 startup POUT setting");
            mvme_close(gVme);
            gVme = NULL;
            global_busy::attach(NULL);
            return FE_ERR_ODB;
        }
        if (state_before_pout != STATE_STOPPED || transition_in_progress) {
            cm_msg(MERROR, frontend_name,
                   "V1190 startup POUT setting requires stable STOPPED state; MIDAS state %d transition %d",
                   state_before_pout, transition_in_progress);
            mvme_close(gVme);
            gVme = NULL;
            global_busy::attach(NULL);
            return FE_ERR_HW;
        }
        if (!configure_v1190_pout_startup()) {
            cm_msg(MERROR, frontend_name,
                   "V1190 startup POUT configuration failed; fevme startup refused");
            mvme_close(gVme);
            gVme = NULL;
            global_busy::attach(NULL);
            return FE_ERR_HW;
        }
    }
    return SUCCESS;
}


/* Close the MIDAS VME interface when the frontend terminates. */
INT frontend_exit()
{
    global_busy::publish_ready(true, false, 0);
    if (gVme) global_busy::set_global_busy(true);
    const bool rpv130_stopped =
        quiesce_rpv130_single_event_busy("frontend exit");
    if (gVme) {
        const bool owns_v1720_run = gV1720StartAttempted || gV1720Started;
        const bool midas_active = run_state == STATE_RUNNING ||
                                  run_state == STATE_PAUSED;
        if (!owns_v1720_run &&
            (gV1720StartupRunState != STATE_STOPPED || midas_active)) {
            cm_msg(MINFO, frontend_name,
                   "V1720E auto-stop skipped on frontend exit: "
                   "startup MIDAS state %d, current state %d, no frontend start",
                   gV1720StartupRunState, run_state);
        } else if (!stop_v1720e_and_publish_state("frontend exit")) {
            cm_msg(MERROR, frontend_name,
                   "V1720E hardware stop failed during frontend exit");
        }
        refresh_enabled_module_variables();
#if ENABLE_V1190_SOFT_TRIGGER_TEST
        if (gV1190RunSettings.enabled && !restore_v1190_diagnostic_settings())
            cm_msg(MERROR, frontend_name, "V1190 diagnostic restoration failed during frontend exit");
#endif
#if ENABLE_V775_SW_TRIGGER_TEST
        if (gV775RunSettings.enabled && !restore_v775_diagnostic_settings())
            cm_msg(MERROR, frontend_name, "V775 diagnostic restoration failed during frontend exit");
#endif
        mvme_close(gVme);
        gVme = NULL;
        global_busy::attach(NULL);
    }

    printf("VME interface closed.\n");

    return rpv130_stopped ? SUCCESS : FE_ERR_HW;
}

/* Begin a run: reset software state, prepare normal operation, then arm diagnostics. */
INT begin_of_run(INT run_number, char *error)
{
    const auto finish = [run_number](INT result) {
        cm_msg(MINFO, frontend_name, "START 500 begin_of_run exit run %d status %d",
               run_number, result);
        return result;
    };
    cm_msg(MINFO, frontend_name, "START 500 begin_of_run enter run %d", run_number);
    printf("Begin run %d\n", run_number);
    if (!global_busy::publish_ready(true, false, 0)) return finish(FE_ERR_ODB);
    if (!publish_configuration_status(false, run_number)) {
        snprintf(error, 256,
                 "Cannot reset VME configuration status at BOR");
        return finish(FE_ERR_ODB);
    }
    reset_run_statistics();
    gV1190BltDiagnosticCount.store(0, std::memory_order_relaxed);
    gV1720BltDiagnosticCount.store(0, std::memory_order_relaxed);
    v1190_fifo_blt_state_reset(&gV1190FifoBltState);
    gRpv130TimingEventCount.store(0, std::memory_order_relaxed);
    gRpv130PollReadyNs.store(0, std::memory_order_relaxed);
    gRpv130LastPollMissNs.store(0, std::memory_order_relaxed);
    gRpv130PollPreviousMissNs.store(0, std::memory_order_relaxed);
    mark_run_counters_dirty();
    if (!publish_run_counters()) {
        cm_msg(MERROR, frontend_name,
               "Cannot reset VME RunCounters ODB values at BOR");
        snprintf(error, 256, "Cannot reset VME RunCounters");
        mark_configuration_failed(run_number);
        return finish(FE_ERR_ODB);
    }
    reset_vme_run_snapshot(run_number);

    if (!validate_and_snapshot_module_settings() ||
        !snapshot_v1720e_settings_for_run() ||
        !snapshot_rpv130_enabled_for_run()) {
        cm_msg(MERROR, frontend_name,
               "Cannot snapshot/validate VME module Settings at BOR");
        snprintf(error, 256, "Invalid VME module ODB Settings");
        mark_configuration_failed(run_number);
        return finish(FE_ERR_ODB);
    }
    if (!validate_v792_event_source_dependency()) {
        snprintf(error, 256,
                 "V792 must be enabled when any VME physics readout module "
                 "is enabled");
        mark_configuration_failed(run_number);
        return finish(FE_ERR_ODB);
    }
    capture_vme_requested_snapshot();
    publish_vme_enabled_for_run();
    set_module_readback_valid(V792_READBACK_PATH,false);
    set_module_readback_valid(V1190_READBACK_PATH,false);
    set_module_readback_valid(V775_READBACK_PATH,false);
    set_v1720e_readback_valid(false);

    if (!prepare_modules_for_run()) {
        stop_v1720e_and_publish_state("BOR failure");
        cm_msg(MERROR, frontend_name,
               "BOR configuration failed; refusing to start run %d", run_number);
        snprintf(error, 256, "Normal BOR module preparation failed");
        mark_configuration_failed(run_number);
        return finish(FE_ERR_HW);
    }

#if ENABLE_V792_SW_TRIGGER_TEST
    if(gV792RunSettings.enabled) setup_v792_sw_trigger_test();
#endif
#if ENABLE_V1190_SOFT_TRIGGER_TEST
    if (gV1190RunSettings.enabled && (!restore_v1190_diagnostic_settings() ||
        !setup_v1190_soft_trigger_test())) {
        snprintf(error, 256, "V1190 soft-trigger diagnostic setup failed");
        restore_v1190_diagnostic_settings();
        stop_v1720e_and_publish_state("BOR diagnostic setup failure");
        mark_configuration_failed(run_number);
        return finish(FE_ERR_HW);
    }
#endif
#if ENABLE_V775_SW_TRIGGER_TEST
    if (gV775RunSettings.enabled && (!restore_v775_diagnostic_settings() ||
        !setup_v775_sw_trigger_test())) {
        snprintf(error, 256, "V775 SW trigger diagnostic setup failed");
        restore_v775_diagnostic_settings();
        stop_v1720e_and_publish_state("BOR diagnostic setup failure");
        mark_configuration_failed(run_number);
        return finish(FE_ERR_HW);
    }
#endif
    if (!arm_rpv130_single_event_busy()) {
        snprintf(error, 256, "RPV130 Single Event BUSY arm failed");
        quiesce_rpv130_single_event_busy("BOR arm failure");
        stop_v1720e_and_publish_state("BOR RPV130 arm failure");
        mark_configuration_failed(run_number);
        return finish(FE_ERR_HW);
    }
    gVmeRunSnapshot.frontend_bor_complete = TRUE;
    if (!vme_configuration_ready()) {
        gVmeRunSnapshot.frontend_bor_complete = FALSE;
        quiesce_rpv130_single_event_busy("BOR readiness failure");
        stop_v1720e_and_publish_state("BOR readiness failure");
        cm_msg(MERROR, frontend_name,
               "VME BOR completed without all enabled modules ready");
        snprintf(error, 256, "VME configuration readiness check failed");
        mark_configuration_failed(run_number);
        return finish(FE_ERR_HW);
    }
    if (!publish_vme_run_snapshot()) {
        gVmeRunSnapshot.frontend_bor_complete = FALSE;
        quiesce_rpv130_single_event_busy("BOR RunSnapshot publish failure");
        stop_v1720e_and_publish_state("BOR RunSnapshot publish failure");
        cm_msg(MERROR, frontend_name,
               "Cannot publish completed VME RunSnapshot for run %d",
               run_number);
        snprintf(error, 256, "Cannot publish completed VME RunSnapshot");
        mark_configuration_failed(run_number);
        return finish(FE_ERR_ODB);
    }
    if (!publish_configuration_status(true, run_number)) {
        gVmeRunSnapshot.frontend_bor_complete = FALSE;
        publish_vme_run_snapshot();
        quiesce_rpv130_single_event_busy("BOR status publish failure");
        stop_v1720e_and_publish_state("BOR status publish failure");
        cm_msg(MERROR, frontend_name,
               "Cannot publish successful VME configuration status for run %d",
               run_number);
        snprintf(error, 256, "Cannot publish VME configuration status");
        mark_configuration_failed(run_number);
        return finish(FE_ERR_ODB);
    }
    if (!global_busy::publish_ready(true, true, run_number)) {
        snprintf(error, 256, "Cannot publish VME DAQReady");
        quiesce_rpv130_single_event_busy("BOR DAQReady publish failure");
        return finish(FE_ERR_ODB);
    }
    return finish(SUCCESS);
}

/* Handle the end of a MIDAS run. */
INT end_of_run(INT run_number, char *error)
{
    global_busy::disable_readout();
    global_busy::publish_ready(true, false, 0);
    bool restore_failed = false;
    printf("End run %d\n", run_number);
    const bool rpv130_stopped = quiesce_rpv130_single_event_busy("EOR");
    const bool v1720_stopped = stop_v1720e_and_publish_state("EOR");
    if (!rpv130_stopped || !v1720_stopped) {
        publish_run_counters();
        snprintf(error, 256, "%s failed during EOR",
                 !rpv130_stopped ? "RPV130 BUSY disable" :
                                   "V1720E Acquisition Stop");
        return FE_ERR_HW;
    }
    log_run_statistics();
    if (!publish_run_counters()) {
        cm_msg(MERROR, frontend_name,
               "Cannot publish final VME RunCounters ODB values at EOR");
        snprintf(error, 256, "Cannot publish final VME RunCounters");
        return FE_ERR_ODB;
    }
    refresh_enabled_module_variables();

#if ENABLE_V1190_SOFT_TRIGGER_TEST
    if (gV1190RunSettings.enabled && !restore_v1190_diagnostic_settings())
        restore_failed = true;
#endif
#if ENABLE_V775_SW_TRIGGER_TEST
    if (gV775RunSettings.enabled && !restore_v775_diagnostic_settings())
        restore_failed = true;
#endif
    if (restore_failed) {
        snprintf(error, 256, "Diagnostic settings restoration failed");
        return FE_ERR_HW;
    }
    return SUCCESS;
}


/* Roll back hardware and framework state after any failed START transition. */
static INT start_abort(INT run_number, char *error)
{
    global_busy::disable_readout();
    global_busy::publish_ready(true, false, 0);
    if (error)
        error[0] = '\0';

    /* A successful BOR enables the legacy MFE readout before another client
     * can fail the common START. Stop software readout before hardware. */
    readout_enable(FALSE);
    const bool rpv130_stopped =
        quiesce_rpv130_single_event_busy("STARTABORT");
    V1720StopOutcome stop_outcome = V1720StopOutcome::Disabled;
    const bool stopped = stop_v1720e_and_publish_state(
        "STARTABORT rollback", &stop_outcome);

    gVmeRunSnapshot.frontend_bor_complete = FALSE;
    publish_vme_run_snapshot();
    mark_configuration_failed(run_number);

    run_state = STATE_STOPPED;
    cm_set_client_run_state(run_state);
    if (!stopped || !rpv130_stopped) {
        if (error)
            snprintf(error, 256,
                     "%s failed during STARTABORT",
                     !rpv130_stopped ? "RPV130 BUSY disable" :
                                       "V1720E Acquisition Stop");
        return FE_ERR_HW;
    }
    cm_msg(MINFO, frontend_name,
           "STARTABORT rollback completed for run %d; V1720E %s",
           run_number, v1720_stop_outcome_name(stop_outcome));
    return SUCCESS;
}


/* Handle a MIDAS run pause. */
INT pause_run(INT run_number, char *error)
{
    return SUCCESS;
}


/* Handle resuming a paused MIDAS run. */
INT resume_run(INT run_number, char *error)
{
    return SUCCESS;
}


/* Poll RPV130 status outside the DAQ event readout path. */
INT frontend_loop()
{
    if (gBltStopRequested.exchange(false, std::memory_order_relaxed) &&
        run_state == STATE_RUNNING) {
        char error[TRANSITION_ERROR_STRING_LENGTH] = {};
        const INT status = cm_transition(TR_STOP, 0, error, sizeof(error),
                                         TR_ASYNC, FALSE);
        if (status != CM_SUCCESS) {
            cm_msg(MERROR, frontend_name,
                   "BLT32 failure: RUN stop request failed: status %d %s",
                   status, error);
            gBltStopRequested.store(true, std::memory_order_relaxed);
        } else {
            cm_msg(MERROR, frontend_name,
                   "BLT32 failure: RUN stop requested");
        }
    }
    global_busy::process_diagnostic_request(run_state == STATE_STOPPED);
    process_manual_buffer_clear_request();
    publish_rpv130_status(false);
    const DWORD now = ss_millitime();
    const auto due=[](DWORD now,DWORD last,bool dirty) {
        const DWORD elapsed=static_cast<DWORD>(now-last);
        return (dirty&&elapsed>=MODULE_VARIABLES_MIN_PUBLISH_INTERVAL_MS)||elapsed>=MODULE_VARIABLES_HEARTBEAT_INTERVAL_MS;
    };
    if (due(now, gRunCountersLastPublish, gRunCountersDirty))
        publish_run_counters();
    if(gV792RunSettings.enabled && due(now,gV792LastVariablesPublish,gV792Runtime.dirty)) {
        WORD s1=0,s2=0; DWORD counter=gV792Runtime.event_counter;
        if(vme_read16(V792_BASE+V792_CSR1_RO,s1,"V792 runtime Status1")&&vme_read16(V792_BASE+V792_CSR2_RO,s2,"V792 runtime Status2")) {
            v792_EvtCntRead(gVme,V792_BASE,&counter); gV792Runtime.communication_ok=TRUE; decode_v7xx_runtime(gV792Runtime,s1,s2,counter);
        } else gV792Runtime.communication_ok=FALSE;
        publish_v7xx_variables(V792_VARIABLES_PATH,gV792Runtime,gV792LastVariablesPublish);
    }
    if(gV1190RunSettings.enabled && due(now,gV1190LastVariablesPublish,gV1190Runtime.dirty)) {
        WORD status=0,stored=0; DWORD counter=gV1190Runtime.event_counter;
        if(vme_read16(V1190_BASE+V1190_STATUS,status,"V1190 runtime Status")&&vme_read16(V1190_BASE+V1190_EVENT_STORED,stored,"V1190 runtime Event Stored")&&vme_read32(V1190_BASE+V1190_EVENT_COUNTER,counter,"V1190 runtime Event Counter")) {
            gV1190Runtime.communication_ok=TRUE; decode_v1190_runtime(status,stored,counter);
        } else gV1190Runtime.communication_ok=FALSE;
        publish_v1190_variables();
    }
    if(gV775RunSettings.enabled && due(now,gV775LastVariablesPublish,gV775Runtime.dirty)) {
        WORD s1=0,s2=0; DWORD counter=gV775Runtime.event_counter;
        if(vme_read16(V775_BASE+V775_STATUS1,s1,"V775 runtime Status1")&&vme_read16(V775_BASE+V775_STATUS2,s2,"V775 runtime Status2")) {
            v775_EvtCntRead(gVme,V775_BASE,&counter); gV775Runtime.communication_ok=TRUE; decode_v7xx_runtime(gV775Runtime,s1,s2,counter);
        } else gV775Runtime.communication_ok=FALSE;
        publish_v7xx_variables(V775_VARIABLES_PATH,gV775Runtime,gV775LastVariablesPublish);
    }
    const DWORD elapsed =
        static_cast<DWORD>(now - gV1720LastVariablesPublish);
    if (gV1720VariablesEnabled &&
        ((gV1720Runtime.dirty &&
          elapsed >= V1720E_VARIABLES_MIN_PUBLISH_INTERVAL_MS) ||
         elapsed >= V1720E_VARIABLES_HEARTBEAT_INTERVAL_MS))
        publish_v1720e_variables();

    // EQ_POLLED/RO_RUNNING keeps poll_event() out of STOPPED and PAUSED.
    // frontend_call_loop remains enabled for status housekeeping, so yield
    // CPU here when trigger-latency-sensitive readout is inactive.
    if (run_state != STATE_RUNNING)
        ss_sleep(FRONTEND_IDLE_SLEEP_MS);
    return SUCCESS;
}


/* Poll without consuming FIFO words; test mode performs timing iterations only. */
INT poll_event(INT source, INT count, BOOL test)
{
    if (!global_busy::readout_allowed())
        return 0;
    if (!gVme || gReadoutFailed ||
        !gV792RunSettings.enabled ||
        (gV1720RunSettings.enabled && !gV1720Started))
        return 0;
    for (INT i = 0; i < count; ++i) {
        if (v792_DataReady(gVme, V792_BASE) && !test) {
            if (gSingleEventBusyEnabledForRun &&
                gRpv130TimingEventCount.load(std::memory_order_relaxed) <
                    RPV130_TIMING_EVENT_LIMIT) {
                uint64_t empty = 0;
                if (gRpv130PollReadyNs.compare_exchange_strong(
                        empty, monotonic_ns(), std::memory_order_relaxed))
                    gRpv130PollPreviousMissNs.store(
                        gRpv130LastPollMissNs.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
            }
            return 1;
        }
    }
    if (!test && gSingleEventBusyEnabledForRun &&
        gRpv130TimingEventCount.load(std::memory_order_relaxed) <
            RPV130_TIMING_EVENT_LIMIT)
        gRpv130LastPollMissNs.store(monotonic_ns(),
                                    std::memory_order_relaxed);
    return 0;
}


/* Configure interrupt-driven event acquisition; interrupt mode is not used in this test frontend. */
INT interrupt_configure(INT cmd, INT source, PTYPE adr)
{
    return SUCCESS;
}

/* Event synchronization layer. Readers expose native counters; pairing uses low 22 bits. */
static bool check_event_counter_match(const V792EventInfo &v792,
                                      const V1190EventInfo &v1190,
                                      const V775EventInfo &v775,
                                      const V1720E_EVENT_INFO *v1720)
{
    bool have=false; DWORD reference=0;
#define CMP(enabled,value) do { if(enabled) { DWORD n=(value)&V1190_EVENT_COUNTER_MASK; if(have&&n!=reference)return false; reference=n; have=true; } } while(0)
    CMP(gV792RunSettings.enabled,v792.event_counter); CMP(gV1190RunSettings.enabled,v1190.event_counter);
    CMP(gV775RunSettings.enabled,v775.event_counter);
    CMP(v1720!=NULL,v1720?v1720->event_counter:0);
#undef CMP
    return true;
}

static void log_event_counter_mismatch(const V792EventInfo &v792,
                                       const V1190EventInfo &v1190,
                                       const V775EventInfo &v775,
                                       const V1720E_EVENT_INFO *v1720,
                                       DWORD midas_serial)
{
    if (gRunStatistics.counter_mismatch_count == 0)
        gRunStatistics.first_mismatch_serial = midas_serial;
    ++gRunStatistics.counter_mismatch_count;
    gRunStatistics.last_mismatch_serial = midas_serial;
    mark_run_counters_dirty();

    const uint64_t count = gRunStatistics.counter_mismatch_count;
    if (count <= 10) {
        if (v1720) {
            cm_msg(MINFO, frontend_name,
                   "WARNING: Event counter mismatch (accepted): MIDAS serial=%u "
                   "V792=0x%06X V1190=0x%06X "
                   "V775=0x%06X "
                   "V1720=0x%06X",
                   midas_serial, v792.event_counter & V7XX_EVENT_COUNTER_MASK,
                   v1190.event_counter & V1190_EVENT_COUNTER_MASK,
                   v775.event_counter & V7XX_EVENT_COUNTER_MASK,
                   v1720->event_counter & V7XX_EVENT_COUNTER_MASK);
        } else {
            cm_msg(MINFO, frontend_name,
                   "WARNING: Event counter mismatch (accepted): MIDAS serial=%u "
                   "V792=0x%06X V1190=0x%06X"
                   " V775=0x%06X"
                   " (V1720 disabled)",
                   midas_serial, v792.event_counter & V7XX_EVENT_COUNTER_MASK,
                   v1190.event_counter & V1190_EVENT_COUNTER_MASK
                   , v775.event_counter & V7XX_EVENT_COUNTER_MASK
                   );
        }
    } else if (count % 1000 == 0) {
        cm_msg(MINFO, frontend_name,
               "WARNING: Event counter mismatch summary: %llu mismatches through MIDAS serial %u",
               static_cast<unsigned long long>(count), midas_serial);
    }
}

static bool update_v1720e_integrity(const V1720E_EVENT_INFO &event,
                                    DWORD midas_serial)
{
    bool valid = true;
    if (!event.size_valid) {
        valid = false;
        ++gRunStatistics.v1720_size_error_count;
        mark_run_counters_dirty();
        cm_msg(MERROR, frontend_name,
               "V1720E Event Size error at MIDAS serial %u: got %u, expected %u",
               midas_serial, event.event_size, gV1720ExpectedEventWords);
    }
    if (!event.channel_mask_valid) {
        valid = false;
        ++gRunStatistics.v1720_mask_error_count;
        mark_run_counters_dirty();
        cm_msg(MERROR, frontend_name,
               "V1720E Channel Mask error at MIDAS serial %u: got 0x%02X, expected 0x%02X",
               midas_serial, event.channel_mask, gV1720ExpectedChannelMask);
    }
    if (!gRunStatistics.v1720_have_previous) {
        gRunStatistics.v1720_first_counter = event.event_counter;
        gRunStatistics.v1720_have_previous = true;
    } else {
        const DWORD counter_delta =
            (event.event_counter - gRunStatistics.v1720_previous_counter) &
            V7XX_EVENT_COUNTER_MASK;
        const DWORD ttt_delta =
            (event.trigger_time_tag - gRunStatistics.v1720_previous_ttt) &
            0x7FFFFFFFu;
        if (counter_delta != 1) {
            valid = false;
            ++gRunStatistics.v1720_counter_discontinuity_count;
            mark_run_counters_dirty();
            cm_msg(MERROR, frontend_name,
                   "V1720E counter discontinuity at MIDAS serial %u: "
                   "previous=%u current=%u delta=%u",
                   midas_serial, gRunStatistics.v1720_previous_counter,
                   event.event_counter, counter_delta);
        }
        if (gRunStatistics.v1720_ttt_count == 0 ||
            ttt_delta < gRunStatistics.v1720_min_ttt_delta)
            gRunStatistics.v1720_min_ttt_delta = ttt_delta;
        if (ttt_delta > gRunStatistics.v1720_max_ttt_delta)
            gRunStatistics.v1720_max_ttt_delta = ttt_delta;
        ++gRunStatistics.v1720_ttt_count;
    }
    gRunStatistics.v1720_last_counter = event.event_counter;
    gRunStatistics.v1720_previous_counter = event.event_counter;
    gRunStatistics.v1720_previous_ttt = event.trigger_time_tag;
    return valid;
}

/* MIDAS publishing layer. Hardware access and counter pairing stay outside. */
static INT build_midas_event(char *pevent,
                             const DWORD *v792_data, const V792EventInfo &v792,
                             const DWORD *v1190_data, const V1190EventInfo &v1190,
                             const DWORD *v775_data, const V775EventInfo &v775,
                             const DWORD *v1720_data,
                             const V1720E_EVENT_INFO *v1720)
{
    bk_init32(pevent);
    void *bank = NULL;
    if(gV792RunSettings.enabled) { bk_create(pevent,"ADC0",TID_DWORD,&bank); memcpy(bank,v792_data,v792.words*sizeof(DWORD)); bk_close(pevent,static_cast<DWORD*>(bank)+v792.words); }

    if(gV1190RunSettings.enabled) { bank=NULL; bk_create(pevent,"TDC0",TID_DWORD,&bank); memcpy(bank,v1190_data,v1190.words*sizeof(DWORD)); bk_close(pevent,static_cast<DWORD*>(bank)+v1190.words); }

    if(gV775RunSettings.enabled) { bank=NULL; bk_create(pevent,"TDC1",TID_DWORD,&bank); memcpy(bank,v775_data,v775.words*sizeof(DWORD)); bk_close(pevent,static_cast<DWORD*>(bank)+v775.words); }

    if (v1720) {
        bank = NULL;
        bk_create(pevent, "FADC", TID_DWORD, &bank);
        memcpy(bank, v1720_data, v1720->words * sizeof(DWORD));
        bk_close(pevent, static_cast<DWORD *>(bank) + v1720->words);
    }
    return bk_size(pevent);
}


/* Acquire one event per module, check pairing, and publish one MIDAS event. */
INT read_vme_event(char *pevent, INT off)
{
    Rpv130EventTiming timing;
    if (gSingleEventBusyEnabledForRun &&
        gRpv130TimingEventCount.load(std::memory_order_relaxed) <
            RPV130_TIMING_EVENT_LIMIT)
        timing.read_start_ns = monotonic_ns();
    if (!global_busy::readout_allowed())
        return 0;
    if (!gVme || gReadoutFailed)
        return 0;
    if (gSingleEventBusyEnabledForRun && timing.read_start_ns) {
        const unsigned index = gRpv130TimingEventCount.fetch_add(
            1, std::memory_order_relaxed) + 1;
        timing.active = index <= RPV130_TIMING_EVENT_LIMIT;
        if (timing.active) {
            timing.index = index;
            timing.serial = SERIAL_NUMBER(pevent);
            timing.poll_ready_ns = gRpv130PollReadyNs.exchange(
                0, std::memory_order_relaxed);
            timing.poll_previous_miss_ns = gRpv130PollPreviousMissNs.exchange(
                0, std::memory_order_relaxed);
        }
    }

    if (gSingleEventBusyEnabledForRun) {
        bool busy1 = false;
        uint8_t csr1 = 0;
        const int busy_status = rpv130_read_busy1(
            gVme, RPV130_BASE_ADDRESS, &busy1, &csr1);
        if (timing.active) timing.csr_confirm_ns = monotonic_ns();
        if (busy_status != MVME_SUCCESS ||
            !busy1 ||
            (csr1 & RPV130_CSR1_CHANNEL1_ARMED) !=
                RPV130_CSR1_CHANNEL1_ARMED) {
            fail_single_event_busy("FIN1 BUSY1 absent, disarmed, or CSR1 read failed");
            return 0;
        }
        if (!publish_rpv130_busy_state(true, true)) {
            fail_single_event_busy("RPV130 ODB BUSY status update failed");
            return 0;
        }
    }

    // V792 is the primary trigger. Do not consume it until every enabled peer FIFO is ready.
    if (timing.active) timing.peers_start_ns = monotonic_ns();
    bool peers_ready = true;
    if (gV1190RunSettings.enabled) {
        if (timing.active) timing.v1190_ready_start_ns = monotonic_ns();
        peers_ready = wait_for_v1190_data_ready();
        if (timing.active) timing.v1190_ready_end_ns = monotonic_ns();
    }
    if (peers_ready && gV775RunSettings.enabled) {
        if (timing.active) timing.v775_ready_start_ns = monotonic_ns();
        peers_ready = wait_for_v775_data_ready();
        if (timing.active) timing.v775_ready_end_ns = monotonic_ns();
    }
    if (peers_ready && gV1720RunSettings.enabled) {
        if (timing.active) timing.v1720_ready_start_ns = monotonic_ns();
        peers_ready = wait_for_v1720e_data_ready();
        if (timing.active) timing.v1720_ready_end_ns = monotonic_ns();
    }
    if (timing.active) timing.peers_end_ns = monotonic_ns();
    if (!peers_ready) {
        gReadoutFailed = true;
        cm_msg(MERROR, frontend_name,
               "Synchronized readout disabled after module-ready timeout; "
               "no FIFO was consumed. Check hardware and restart the run.");
        fail_single_event_busy("module-ready timeout");
        return 0;
    }

    DWORD v792_data[V792_MAX_EVENT_WORDS];
    DWORD v1190_data[V1190_MAX_EVENT_WORDS];
    DWORD v775_data[V775_MAX_EVENT_WORDS];
    DWORD v1720_data[V1720E_MAX_EVENT_WORDS];
    V792EventInfo v792 = {};
    if (gV792RunSettings.enabled) {
        if (timing.active) timing.v792_start_ns = monotonic_ns();
        v792 = V792_READOUT_MODE_SELECT == V792_BLT32 ?
            read_v792_blt32_event(v792_data) :
            read_v792_single_event(v792_data);
        if (timing.active) timing.v792_end_ns = monotonic_ns();
    }
    if (gV792RunSettings.enabled && (!v792.valid || v792.words == 0)) {
        gReadoutFailed = true;
        cm_msg(MERROR, frontend_name,
               "V792 readout disabled after error; no partial bank sent. Check hardware and restart the run to reset readout.");
        fail_single_event_busy("V792 event readout failed");
        return 0;
    }

    V1190EventInfo v1190 = {};
    V1190_FIFO_BLT_TIMING v1190_phases = {};
    bool v1190_diagnostic = false;
    if (gV1190RunSettings.enabled) {
        if (timing.active ||
            V1190_READOUT_MODE_SELECT == V1190_EVENT_FIFO_BLT32)
            timing.v1190_start_ns = monotonic_ns();
        v1190 = V1190_READOUT_MODE_SELECT == V1190_EVENT_FIFO_BLT32 ?
            read_v1190_fifo_blt32_event(v1190_data, v1190_phases,
                                         v1190_diagnostic) :
            read_v1190_single_event(v1190_data);
        if (timing.active ||
            V1190_READOUT_MODE_SELECT == V1190_EVENT_FIFO_BLT32)
            timing.v1190_end_ns = monotonic_ns();

    }
    if (gV1190RunSettings.enabled && (!v1190.valid || v1190.words == 0)) {
        gReadoutFailed = true;
        cm_msg(MERROR, frontend_name,
               "V1190 readout failed; V792 consumed. No MIDAS event sent; stop RUN.");
        fail_single_event_busy("V1190 event readout failed");
        return 0;
    }

    V775EventInfo v775 = {};
    if (gV775RunSettings.enabled) {
        if (timing.active) timing.v775_start_ns = monotonic_ns();
        v775 = read_v775_single_event(v775_data);
        if (timing.active) timing.v775_end_ns = monotonic_ns();
    }
    if (gV775RunSettings.enabled && (!v775.valid || v775.words == 0)) {
        gReadoutFailed = true;
        cm_msg(MERROR, frontend_name,
               "V775 readout disabled after error; V792/V1190 events were consumed but no partial MIDAS event was sent. Check hardware and restart the run.");
        fail_single_event_busy("V775 event readout failed");
        return 0;
    }

    V1720E_EVENT_INFO v1720 = {};
    V1720E_EVENT_INFO *v1720_event = NULL;
    if (gV1720RunSettings.enabled) {
        if (timing.active || V1720E_READOUT_MODE_SELECT == BLT32)
            timing.v1720_start_ns = monotonic_ns();
        const int v1720_status =
            v1720e_read_event_mode(gVme, V1720E_BASE, v1720_data,
                                   V1720E_MAX_EVENT_WORDS,
                                   gV1720ExpectedEventWords,
                                   gV1720ExpectedChannelMask,
                                   V1720E_READOUT_MODE_SELECT, &v1720);
        if (timing.active || V1720E_READOUT_MODE_SELECT == BLT32)
            timing.v1720_end_ns = monotonic_ns();
        if (v1720_status != MVME_SUCCESS || !v1720.header_valid ||
            v1720.words < 4) {
            if (V1720E_READOUT_MODE_SELECT == BLT32 &&
                v1720.blt_requested_bytes != 0) {
                cm_msg(MERROR, frontend_name,
                       "V1720E BLT32 failed: CAEN status=%d requested=%d bytes actual=%d bytes",
                       v1720.blt_status, v1720.blt_requested_bytes,
                       v1720.blt_actual_bytes);
                gBltStopRequested.store(true, std::memory_order_relaxed);
            }
            if (v1720_status != MVME_SUCCESS)
                gV1720Runtime.communication_ok = FALSE;
            gV1720Runtime.dirty = true;
            ++gRunStatistics.v1720_malformed_count;
            mark_run_counters_dirty();
            gReadoutFailed = true;
            cm_msg(MERROR, frontend_name,
                   "V1720E readout disabled after malformed/partial event: "
                   "status %d words=%zu header-valid=%d. V792/V1190"
                   "/V775"
                   " events "
                   "were consumed but no partial MIDAS event was sent.",
                   v1720_status, v1720.words, v1720.header_valid);
            fail_single_event_busy("V1720E event readout failed");
            return 0;
        }
        v1720_event = &v1720;
        const bool v1720_integrity =
            update_v1720e_integrity(v1720, SERIAL_NUMBER(pevent));
        if (!v1720_integrity) {
            gReadoutFailed = true;
            fail_single_event_busy("V1720E event consistency failure");
            return 0;
        }
        gV1720Runtime.event_counter = v1720.event_counter;
        gV1720Runtime.trigger_time_tag = v1720.trigger_time_tag;
        gV1720Runtime.dirty = true;
    }

    if (!check_event_counter_match(v792, v1190, v775, v1720_event)) {
        if (gSingleEventBusyEnabledForRun) {
            fail_single_event_busy("participating module event counters differ");
            return 0;
        }
        log_event_counter_mismatch(v792, v1190, v775, v1720_event,
                                   SERIAL_NUMBER(pevent));
    }
    if (timing.active) {
        timing.consistency_end_ns = monotonic_ns();
        timing.build_start_ns = timing.consistency_end_ns;
    }

    const INT event_size = build_midas_event(pevent,
                             v792_data, v792,
                             v1190_data, v1190,
                             v775_data, v775,
                             v1720_data, v1720_event);
    if (timing.active) timing.build_end_ns = monotonic_ns();
    if (gSingleEventBusyEnabledForRun) {
        if (event_size <= 0) {
            fail_single_event_busy("MIDAS event construction failed");
            return 0;
        }
        uint8_t csr1 = 0;
        if (timing.active) timing.clear_call_ns = monotonic_ns();
        const int clear_status = timing.active ?
            rpv130_clear_busy1_and_rearm_timed(
                gVme, RPV130_BASE_ADDRESS, &csr1, &timing.writes) :
            rpv130_clear_busy1_and_rearm(
                gVme, RPV130_BASE_ADDRESS, &csr1);
        if (timing.active) timing.clear_return_ns = monotonic_ns();
        if (clear_status != MVME_SUCCESS) {
            fail_single_event_busy("CLR1/re-arm or CSR1 readback failed");
            return 0;
        }
        if (!publish_rpv130_busy_state(
                (csr1 & RPV130_CSR1_BUSY1) != 0, true)) {
            fail_single_event_busy("RPV130 ODB BUSY status update failed");
            return 0;
        }
    }
    timing.outcome = "OK";
    return event_size;
}

INT read_vme_configuration_event(char *pevent, INT)
{
    if (gVmeRunSnapshot.frontend_bor_complete != TRUE)
        return 0;

    HNDLE hDB = 0;
    HNDLE hKey = 0;
    cm_get_experiment_database(&hDB, NULL);
    const INT find_status =
        db_find_key(hDB, 0, VME_RUN_SNAPSHOT_PATH, &hKey);
    if (find_status != DB_SUCCESS) {
        cm_msg(MERROR, frontend_name,
               "Cannot find completed VME RunSnapshot for CONFIG event: %d",
               find_status);
        return 0;
    }

    char *json = NULL;
    int json_capacity = 0;
    int json_length = 0;
    const INT json_status = db_copy_json_save(
        hDB, hKey, &json, &json_capacity, &json_length);
    if (json_status != DB_SUCCESS || json == NULL || json_length <= 0) {
        cm_msg(MERROR, frontend_name,
               "Cannot serialize VME RunSnapshot JSON: status %d length %d",
               json_status, json_length);
        free(json);
        return 0;
    }
    if (json_length > max_event_size - 64) {
        cm_msg(MERROR, frontend_name,
               "VME RunSnapshot JSON is too large: %d bytes", json_length);
        free(json);
        return 0;
    }

    bk_init32(pevent);
    char *data = NULL;
    bk_create(pevent, "VCFG", TID_CHAR, reinterpret_cast<void **>(&data));
    memcpy(data, json, static_cast<size_t>(json_length));
    bk_close(pevent, data + json_length);
    free(json);
    return bk_size(pevent);
}


/* Define the MIDAS equipment handled by this frontend. */
EQUIPMENT equipment[] = {
    {
        "VME",                    // Equipment name
        {
            1,                    // Event ID
            0,                    // Trigger mask
            "SYSTEM",             // Event buffer name
            EQ_POLLED,            // Equipment type
            0,                    // Event source
            "MIDAS",              // Data format
            TRUE,                 // Enable equipment
            RO_RUNNING,           // Readout condition
            500,                  // Polling period [ms]
            0,                    // Event limit; 0 = no automatic stop
            0,                    // Number of sub-events
            0,                    // History logging period [s]
            "",                   // Frontend host name
            "",                   // Frontend name
            "",                   // Frontend source file name
            "",                   // Equipment status text
            "",                   // Equipment status color
            FALSE                 // Hidden flag
        },
        read_vme_event,           // Event readout function
    },
    {
        "VME Configuration",
        {
            5,                    // Unused across repository and current ODB
            0,
            "SYSTEM",
            EQ_PERIODIC,
            0,
            "MIDAS",
            TRUE,
            RO_BOR,
            0,
            0,
            0,
            0,
            "", "", "", "", "", FALSE
        },
        read_vme_configuration_event,
    },

    {""}                          // End of equipment list
};
