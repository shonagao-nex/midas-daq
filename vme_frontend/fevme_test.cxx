#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "midas.h"
#include "mfe.h"
#include "mvmestd.h"
#include "vme/v792.h"
#include "v775.h"

/* Module locations. */
#define V792_BASE 0x00600000
#define V1190_BASE 0x00C10000
#define V775_BASE 0x00000000

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
static const size_t V1190_MAX_EVENT_WORDS = 4096;
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
                                       V775_BIT2_VALID_CONTROL |
                                       V775_BIT2_COMMON_STOP |
                                       V775_BIT2_EMPTY_PROGRAM;
static const WORD V775_RUN_CLEAR_BITS = V775_BIT2_SLIDE_ENABLE |
                                         V775_BIT2_ALL_TRIGGER;

/* Finite ready/handshake polling limits. */
static const unsigned V1190_READY_MAX_POLLS = 100;
static const unsigned V775_READY_MAX_POLLS = 100;
static const unsigned V775_SW_TRIGGER_MAX_POLLS = 100;
static const unsigned V1190_MICRO_MAX_POLLS = 1000;

/* V1190 regular registers and normal-run Control bits. */
static const DWORD V1190_CONTROL = 0x1000;
static const DWORD V1190_STATUS = 0x1002;
static const DWORD V1190_SOFT_CLEAR = 0x1016;
static const DWORD V1190_SOFT_TRIGGER = 0x101A;
static const DWORD V1190_EVENT_COUNTER = 0x101C;
static const DWORD V1190_EVENT_STORED = 0x1020;
static const DWORD V1190_FIRMWARE_REVISION = 0x1026;
static const DWORD V1190_MICRO_DATA = 0x102E;
static const DWORD V1190_MICRO_HANDSHAKE = 0x1030;
static const WORD V1190_CONTROL_EMPTY_EVENT = 0x0008;
static const WORD V1190_CONTROL_EVENT_FIFO = 0x0100;
static const WORD V1190_CONTROL_EXT_TRIGGER_TIME = 0x0200;
static const WORD V1190_STATUS_DATA_READY = 0x0001;
static const WORD V1190_STATUS_TRIGGER_MATCH = 0x0008;
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
static const WORD V1190_OPCODE_READ_TRIGGER_CONFIG = 0x1600;
static const WORD V1190_OPCODE_SET_EDGE_MODE = 0x2200;
static const WORD V1190_OPCODE_READ_EDGE_MODE = 0x2300;
static const WORD V1190_OPCODE_SET_RESOLUTION = 0x2400;
static const WORD V1190_OPCODE_READ_RESOLUTION = 0x2600;
static const WORD V1190_OPCODE_SET_DEAD_TIME = 0x2800;
static const WORD V1190_OPCODE_READ_DEAD_TIME = 0x2900;
static const WORD V1190_OPCODE_ENABLE_TDC_HEADER = 0x3000;
static const WORD V1190_OPCODE_READ_TDC_HEADER = 0x3200;
static const WORD V1190_OPCODE_SET_MAX_HITS = 0x3300;
static const WORD V1190_OPCODE_READ_MAX_HITS = 0x3400;
static const WORD V1190_OPCODE_READ_ERROR_MASK = 0x3A00;
static const WORD V1190_OPCODE_READ_FIFO_SIZE = 0x3C00;
static const WORD V1190_OPCODE_ENABLE_ALL_CHANNELS = 0x4200;
static const WORD V1190_OPCODE_READ_CHANNEL_MASK = 0x4500;
static const WORD V1190_RUN_WINDOW_WIDTH = 12;
static const WORD V1190_RUN_WINDOW_OFFSET = 0xFFF4; // signed -12 counts
static const WORD V1190_RUN_EXTRA_MARGIN = 8;
static const WORD V1190_RUN_REJECT_MARGIN = 4;
static const WORD V1190_RUN_EDGE_MODE = 3;
static const WORD V1190_RUN_RESOLUTION = 2; // 100 ps
static const WORD V1190_RUN_DEAD_TIME = 0;  // approximately 5 ns
static const WORD V1190_RUN_MAX_HITS = 9;   // unlimited
static const size_t V1190_CHANNEL_MASK_WORDS = 8;

const char *frontend_name = "fe_vme_test";      // MIDAS frontend name
const char *frontend_file_name = __FILE__;      // Frontend source file name

BOOL frontend_call_loop = FALSE;                // Disable periodic call to frontend_loop()
BOOL equipment_common_overwrite = TRUE;         // Apply polled/run-only settings to existing ODB equipment
INT display_period = 1000;                      // MIDAS status display update period [ms]
INT max_event_size = 1024 * 1024;               // Maximum event size [bytes]
INT max_event_size_frag = 5 * 1024 * 1024;      // Maximum fragmented event size [bytes]
INT event_buffer_size = 10 * 1024 * 1024;       // MIDAS event buffer size [bytes]

static MVME_INTERFACE *gVme = NULL;              // MIDAS VME interface handle
static bool gReadoutFailed = false;              // Inhibit reads after a partial/malformed event

struct RunStatistics {
    uint64_t counter_mismatch_count;
    uint32_t first_mismatch_serial;
    uint32_t last_mismatch_serial;
};

static RunStatistics gRunStatistics = {0, 0, 0};

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

static bool wait_for_v1190_data_ready()
{
    WORD status = 0;
    for (unsigned poll = 0; poll < V1190_READY_MAX_POLLS; ++poll) {
        if (!vme_read16(V1190_BASE + V1190_STATUS, status, "V1190 Status"))
            return false;
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

/* Frontend initialization checks. Keep the established read-only access order. */
static bool check_module_communication()
{
    printf("Checking V792 at 0x%08X...\n", V792_BASE);
    if (!v792_isPresent(gVme, V792_BASE)) {
        cm_msg(MERROR, frontend_name,
               "V792 not found at 0x%08X", V792_BASE);
        return false;
    }
    printf("V792 detected.\n");
    v792_Status(gVme, V792_BASE);

    WORD v1190_status = 0;
    WORD v1190_events = 0;
    printf("Checking V1190A at 0x%08X...\n", V1190_BASE);
    if (!vme_read16(V1190_BASE + V1190_STATUS, v1190_status, "V1190 Status") ||
        !vme_read16(V1190_BASE + V1190_EVENT_STORED, v1190_events,
                    "V1190 Event Stored")) {
        cm_msg(MERROR, frontend_name,
               "V1190A communication check failed at 0x%08X", V1190_BASE);
        return false;
    }
    printf("V1190A detected: Status=0x%04X Event Stored=%u.\n",
           v1190_status, v1190_events);

    printf("Checking V775 at 0x%08X...\n", V775_BASE);
    if (!v775_isPresent(gVme, V775_BASE)) {
        cm_msg(MERROR, frontend_name,
               "V775 not found at 0x%08X", V775_BASE);
        return false;
    }
    printf("V775 detected.\n");
    v775_Status(gVme, V775_BASE);
    return true;
}

struct V1190Configuration {
    WORD mode, trigger[5], edge, resolution, dead_time, header, max_hits;
    WORD error_mask, fifo_size, channels[V1190_CHANNEL_MASK_WORDS];
    WORD control, status, firmware;
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
           vme_read16(V1190_BASE + V1190_FIRMWARE_REVISION, c.firmware,
                      "V1190 Firmware Revision");
}

static bool log_current_configuration(const char *phase)
{
    WORD vf = 0, vs1 = 0, vs2 = 0, vb = 0, iped = 0;
    WORD tf = 0, ts1 = 0, ts2 = 0, tb = 0, fsr = 0;
    WORD vth[V792_MAX_CHANNELS], tth[V775_MAX_CHANNELS];
    V1190Configuration c = {};
    if (!vme_read16(V792_BASE + V792_FIRM_REV, vf, "V792 Firmware") ||
        !vme_read16(V792_BASE + V792_CSR1_RO, vs1, "V792 Status 1") ||
        !vme_read16(V792_BASE + V792_CSR2_RO, vs2, "V792 Status 2") ||
        !vme_read16(V792_BASE + V792_BIT_SET2_RW, vb, "V792 Bit Set 2") ||
        !vme_read16(V792_BASE + V792_IPED_RW, iped, "V792 Iped") ||
        !read_v1190_configuration(c) ||
        !vme_read16(V775_BASE + V775_FIRMWARE_REVISION, tf, "V775 Firmware") ||
        !vme_read16(V775_BASE + V775_STATUS1, ts1, "V775 Status 1") ||
        !vme_read16(V775_BASE + V775_STATUS2, ts2, "V775 Status 2") ||
        !vme_read16(V775_BASE + V775_BIT_SET2, tb, "V775 Bit Set 2") ||
        !vme_read16(V775_BASE + V775_FULL_SCALE_RANGE, fsr, "V775 FSR")) return false;
    if (v792_ThresholdRead(gVme, V792_BASE, vth) != V792_MAX_CHANNELS ||
        v775_ThresholdRead(gVme, V775_BASE, tth) != V775_MAX_CHANNELS) return false;
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
    printf("\n  V775 : firmware=0x%04X status=[0x%04X,0x%04X] BitSet2=0x%04X FSR=0x%04X\n",
           tf, ts1, ts2, tb, fsr);
    printf("  V792 thresholds:");
    for (unsigned i = 0; i < V792_MAX_CHANNELS; ++i) printf(" %03X", vth[i]);
    printf("\n  V775 thresholds:");
    for (unsigned i = 0; i < V775_MAX_CHANNELS; ++i) printf(" %03X", tth[i]);
    printf("\n");
    return true;
}

static bool configure_v792_for_run()
{
    return vme_write16(V792_BASE + V792_IPED_RW, V792_RUN_IPED, "V792 Iped") &&
           vme_write16(V792_BASE + V792_BIT_SET2_RW, V792_BIT2_LOW_THRESHOLD,
                       "V792 zero suppression disable") &&
           vme_write16(V792_BASE + V792_BIT_CLEAR2_WO, V792_BIT2_ALL_TRIGGER,
                       "V792 ALL TRG clear");
}

static bool configure_v1190_for_run()
{
    const WORD width = V1190_RUN_WINDOW_WIDTH, offset = V1190_RUN_WINDOW_OFFSET;
    const WORD extra = V1190_RUN_EXTRA_MARGIN, reject = V1190_RUN_REJECT_MARGIN;
    const WORD edge = V1190_RUN_EDGE_MODE, resolution = V1190_RUN_RESOLUTION;
    const WORD dead = V1190_RUN_DEAD_TIME, hits = V1190_RUN_MAX_HITS;
    WORD control = 0;
    if (!v1190_micro_write_command(V1190_OPCODE_TRIGGER_MATCH, NULL, 0) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_WINDOW_WIDTH, &width, 1) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_WINDOW_OFFSET, &offset, 1) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_EXTRA_MARGIN, &extra, 1) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_REJECT_MARGIN, &reject, 1) ||
        !v1190_micro_write_command(V1190_OPCODE_DISABLE_TRIGGER_SUBTRACTION, NULL, 0) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_EDGE_MODE, &edge, 1) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_RESOLUTION, &resolution, 1) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_DEAD_TIME, &dead, 1) ||
        !v1190_micro_write_command(V1190_OPCODE_SET_MAX_HITS, &hits, 1) ||
        !v1190_micro_write_command(V1190_OPCODE_ENABLE_ALL_CHANNELS, NULL, 0) ||
        !v1190_micro_write_command(V1190_OPCODE_ENABLE_TDC_HEADER, NULL, 0) ||
        !vme_read16(V1190_BASE + V1190_CONTROL, control, "V1190 Control RMW read")) return false;
    control = static_cast<WORD>((control | V1190_CONTROL_EMPTY_EVENT) &
              ~(V1190_CONTROL_EVENT_FIFO | V1190_CONTROL_EXT_TRIGGER_TIME));
    return vme_write16(V1190_BASE + V1190_CONTROL, control, "V1190 Control run settings");
}

static bool configure_v775_for_run()
{
    return vme_write16(V775_BASE + V775_FULL_SCALE_RANGE, V775_RUN_FULL_SCALE,
                       "V775 Full Scale Range") &&
           vme_write16(V775_BASE + V775_BIT_SET2, V775_RUN_SET_BITS,
                       "V775 run bits set") &&
           vme_write16(V775_BASE + V775_BIT_CLEAR2, V775_RUN_CLEAR_BITS,
                       "V775 run bits clear");
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
    WORD iped = 0, bits = 0;
    if (!vme_read16(V792_BASE + V792_IPED_RW, iped, "V792 Iped verify") ||
        !vme_read16(V792_BASE + V792_BIT_SET2_RW, bits, "V792 Bit Set 2 verify")) return false;
    bool ok = verify_value("V792", "Iped", V792_RUN_IPED, iped & 0xFF);
    ok = verify_value("V792", "zero suppression disabled", 1,
                      !!(bits & V792_BIT2_LOW_THRESHOLD)) && ok;
    return verify_value("V792", "ALL TRG", 0, !!(bits & V792_BIT2_ALL_TRIGGER)) && ok;
}

static bool verify_v1190_configuration()
{
    V1190Configuration c = {};
    if (!read_v1190_configuration(c)) return false;
    bool ok = true;
#define V1190_VERIFY(item, expected, actual) \
    do { ok = verify_value("V1190", item, expected, actual) && ok; } while (0)
    V1190_VERIFY("Acquisition mode", 1, c.mode & 1);
    V1190_VERIFY("Window width", V1190_RUN_WINDOW_WIDTH, c.trigger[0] & 0xFFF);
    V1190_VERIFY("Window offset", V1190_RUN_WINDOW_OFFSET & 0xFFF, c.trigger[1] & 0xFFF);
    V1190_VERIFY("Extra search margin", V1190_RUN_EXTRA_MARGIN, c.trigger[2] & 0xFFF);
    V1190_VERIFY("Reject margin", V1190_RUN_REJECT_MARGIN, c.trigger[3] & 0xFFF);
    V1190_VERIFY("Trigger subtraction", 0, c.trigger[4] & 1);
    V1190_VERIFY("Edge mode", V1190_RUN_EDGE_MODE, c.edge & 3);
    V1190_VERIFY("Resolution", V1190_RUN_RESOLUTION, c.resolution & 3);
    V1190_VERIFY("Dead time", V1190_RUN_DEAD_TIME, c.dead_time & 3);
    V1190_VERIFY("Maximum hits", V1190_RUN_MAX_HITS, c.max_hits & 0xF);
    V1190_VERIFY("TDC Header/Trailer", 1, c.header & 1);
    for (size_t i = 0; i < V1190_CHANNEL_MASK_WORDS; ++i)
        V1190_VERIFY("Channel mask word", 0xFFFF, c.channels[i]);
    V1190_VERIFY("Empty Event", 1, !!(c.control & V1190_CONTROL_EMPTY_EVENT));
    V1190_VERIFY("Event FIFO", 0, !!(c.control & V1190_CONTROL_EVENT_FIFO));
    V1190_VERIFY("Extended Trigger Time Tag", 0,
                 !!(c.control & V1190_CONTROL_EXT_TRIGGER_TIME));
#undef V1190_VERIFY
    return ok;
}

static bool verify_v775_configuration()
{
    WORD fsr = 0, bits = 0;
    if (!vme_read16(V775_BASE + V775_FULL_SCALE_RANGE, fsr, "V775 FSR verify") ||
        !vme_read16(V775_BASE + V775_BIT_SET2, bits, "V775 Bit Set 2 verify")) return false;
    bool ok = verify_value("V775", "Full Scale Range", V775_RUN_FULL_SCALE, fsr & 0xFF);
    ok = verify_value("V775", "required set bits", V775_RUN_SET_BITS,
                      bits & V775_RUN_SET_BITS) && ok;
    return verify_value("V775", "required clear bits", 0,
                        bits & V775_RUN_CLEAR_BITS) && ok;
}

static bool clear_module_buffers()
{
    if (!vme_write16(V792_BASE + V792_BIT_SET2_RW, 0x0004, "V792 Data Clear set") ||
        !vme_write16(V792_BASE + V792_BIT_CLEAR2_WO, 0x0004, "V792 Data Clear clear") ||
        !vme_write16(V1190_BASE + V1190_SOFT_CLEAR, 0, "V1190 Software Clear") ||
        !vme_write16(V775_BASE + V775_BIT_SET2, V775_BIT2_CLEAR_DATA, "V775 Data Clear set") ||
        !vme_write16(V775_BASE + V775_BIT_CLEAR2, V775_BIT2_CLEAR_DATA, "V775 Data Clear clear")) return false;
    WORD status = 0;
    if (!vme_read16(V1190_BASE + V1190_STATUS, status, "V1190 Status after clear")) return false;
    if (v792_DataReady(gVme, V792_BASE) || (status & V1190_STATUS_DATA_READY) ||
        v775_DataReady(gVme, V775_BASE)) {
        cm_msg(MERROR, frontend_name, "Buffer clear verify failed: DataReady remains asserted");
        return false;
    }
    return true;
}

static bool reset_module_event_counters()
{
    /*
     * No additional VME write is needed here. V792/V775 Data Clear resets
     * their accepted-event counters because ALL TRG is configured to zero.
     * V1190 Software Clear resets both its Output Buffer and Event Counter.
     * verify_run_start_state() reads and logs all three counters.
     */
    return true;
}

static bool verify_run_start_state()
{
    WORD status = 0;
    DWORD v792_counter = 0, v1190_counter = 0, v775_counter = 0;
    v792_EvtCntRead(gVme, V792_BASE, &v792_counter);
    v775_EvtCntRead(gVme, V775_BASE, &v775_counter);
    if (!vme_read32(V1190_BASE + V1190_EVENT_COUNTER, v1190_counter,
                    "V1190 Event Counter") ||
        !vme_read16(V1190_BASE + V1190_STATUS, status, "V1190 run-start Status")) return false;
    printf("BOR event counters: V792=0x%06X V1190=0x%08X V775=0x%06X\n",
           v792_counter & V7XX_EVENT_COUNTER_MASK, v1190_counter,
           v775_counter & V7XX_EVENT_COUNTER_MASK);
    if (v792_DataReady(gVme, V792_BASE) || (status & V1190_STATUS_DATA_READY) ||
        v775_DataReady(gVme, V775_BASE)) {
        cm_msg(MERROR, frontend_name, "BOR run-start verify failed: module buffer is not empty");
        return false;
    }
    printf("BOR configuration complete:\n  V792  : READY\n  V1190 : READY\n"
           "  V775  : READY\n  Buffers empty\n  Event counters reset\n  Run may start\n");
    return true;
}

static bool prepare_modules_for_run()
{
    if (!check_module_communication() ||
        !log_current_configuration("before configuration")) return false;
    if (!configure_v792_for_run() || !configure_v1190_for_run() ||
        !configure_v775_for_run()) return false;
    if (!verify_v792_configuration() || !verify_v1190_configuration() ||
        !verify_v775_configuration()) return false;
    return log_current_configuration("after configuration") &&
           clear_module_buffers() && reset_module_event_counters() &&
           verify_run_start_state();
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
    gRunStatistics.counter_mismatch_count = 0;
    gRunStatistics.first_mismatch_serial = 0;
    gRunStatistics.last_mismatch_serial = 0;
}

static void log_run_statistics()
{
    printf("Event counter mismatches during run: %llu\n",
           static_cast<unsigned long long>(gRunStatistics.counter_mismatch_count));
    if (gRunStatistics.counter_mismatch_count != 0) {
        printf("First mismatch serial: %u\n", gRunStatistics.first_mismatch_serial);
        printf("Last mismatch serial : %u\n", gRunStatistics.last_mismatch_serial);
    }
}


/* Open VME and verify both modules without changing their configuration. */
INT frontend_init()
{
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

    printf("Opening VME interface...\n");

    INT status = mvme_open(&gVme, 0);

    if (status != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "mvme_open() failed: %d", status);
        return FE_ERR_HW;
    }

    mvme_set_am(gVme, MVME_AM_A24_ND);
    mvme_set_dmode(gVme, MVME_DMODE_D16);

    printf("VME interface opened.\n");
    if (!check_module_communication()) {
        mvme_close(gVme);
        gVme = NULL;
        return FE_ERR_HW;
    }
    return SUCCESS;
}


/* Close the MIDAS VME interface when the frontend terminates. */
INT frontend_exit()
{
    if (gVme) {
#if ENABLE_V1190_SOFT_TRIGGER_TEST
        if (!restore_v1190_diagnostic_settings())
            cm_msg(MERROR, frontend_name, "V1190 diagnostic restoration failed during frontend exit");
#endif
#if ENABLE_V775_SW_TRIGGER_TEST
        if (!restore_v775_diagnostic_settings())
            cm_msg(MERROR, frontend_name, "V775 diagnostic restoration failed during frontend exit");
#endif
        mvme_close(gVme);
        gVme = NULL;
    }

    printf("VME interface closed.\n");

    return SUCCESS;
}

/* Begin a run: reset software state, prepare normal operation, then arm diagnostics. */
INT begin_of_run(INT run_number, char *error)
{
    printf("Begin run %d\n", run_number);
    reset_run_statistics();

    if (!prepare_modules_for_run()) {
        cm_msg(MERROR, frontend_name,
               "BOR configuration failed; refusing to start run %d", run_number);
        snprintf(error, 256, "Normal BOR module preparation failed");
        return FE_ERR_HW;
    }

#if ENABLE_V792_SW_TRIGGER_TEST
    setup_v792_sw_trigger_test();
#endif
#if ENABLE_V1190_SOFT_TRIGGER_TEST
    if (!restore_v1190_diagnostic_settings() ||
        !setup_v1190_soft_trigger_test()) {
        snprintf(error, 256, "V1190 soft-trigger diagnostic setup failed");
        restore_v1190_diagnostic_settings();
        return FE_ERR_HW;
    }
#endif
#if ENABLE_V775_SW_TRIGGER_TEST
    if (!restore_v775_diagnostic_settings() ||
        !setup_v775_sw_trigger_test()) {
        snprintf(error, 256, "V775 SW trigger diagnostic setup failed");
        restore_v775_diagnostic_settings();
        return FE_ERR_HW;
    }
#endif
    return SUCCESS;
}

/* Handle the end of a MIDAS run. */
INT end_of_run(INT run_number, char *error)
{
    bool restore_failed = false;
    printf("End run %d\n", run_number);
    log_run_statistics();

#if ENABLE_V1190_SOFT_TRIGGER_TEST
    if (!restore_v1190_diagnostic_settings())
        restore_failed = true;
#endif
#if ENABLE_V775_SW_TRIGGER_TEST
    if (!restore_v775_diagnostic_settings())
        restore_failed = true;
#endif
    if (restore_failed) {
        snprintf(error, 256, "Diagnostic settings restoration failed");
        return FE_ERR_HW;
    }
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


/* Periodic frontend loop; disabled because frontend_call_loop is FALSE. */
INT frontend_loop()
{
    return SUCCESS;
}


/* Poll without consuming FIFO words; test mode performs timing iterations only. */
INT poll_event(INT source, INT count, BOOL test)
{
    if (!gVme || gReadoutFailed)
        return 0;
    for (INT i = 0; i < count; ++i) {
        if (v792_DataReady(gVme, V792_BASE) && !test)
            return 1;
    }
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
                                      const V775EventInfo &v775)
{
    const DWORD v792_counter22 = v792.event_counter & V1190_EVENT_COUNTER_MASK;
    const DWORD v1190_counter22 = v1190.event_counter & V1190_EVENT_COUNTER_MASK;
    const DWORD v775_counter22 = v775.event_counter & V1190_EVENT_COUNTER_MASK;
    return v792_counter22 == v1190_counter22 &&
           v792_counter22 == v775_counter22;
}

static void log_event_counter_mismatch(const V792EventInfo &v792,
                                       const V1190EventInfo &v1190,
                                       const V775EventInfo &v775,
                                       DWORD midas_serial)
{
    if (gRunStatistics.counter_mismatch_count == 0)
        gRunStatistics.first_mismatch_serial = midas_serial;
    ++gRunStatistics.counter_mismatch_count;
    gRunStatistics.last_mismatch_serial = midas_serial;

    const uint64_t count = gRunStatistics.counter_mismatch_count;
    if (count <= 10) {
        cm_msg(MINFO, frontend_name,
               "WARNING: Event counter mismatch (accepted): MIDAS serial=%u V792=0x%06X V1190=0x%06X V775=0x%06X",
               midas_serial, v792.event_counter & V7XX_EVENT_COUNTER_MASK,
               v1190.event_counter & V1190_EVENT_COUNTER_MASK,
               v775.event_counter & V7XX_EVENT_COUNTER_MASK);
    } else if (count % 1000 == 0) {
        cm_msg(MINFO, frontend_name,
               "WARNING: Event counter mismatch summary: %llu mismatches through MIDAS serial %u",
               static_cast<unsigned long long>(count), midas_serial);
    }
}

/* MIDAS publishing layer. Hardware access and counter pairing stay outside. */
static INT build_midas_event(char *pevent,
                             const DWORD *v792_data, const V792EventInfo &v792,
                             const DWORD *v1190_data, const V1190EventInfo &v1190,
                             const DWORD *v775_data, const V775EventInfo &v775)
{
    bk_init32(pevent);
    void *bank = NULL;
    bk_create(pevent, "ADC0", TID_DWORD, &bank);
    memcpy(bank, v792_data, v792.words * sizeof(DWORD));
    bk_close(pevent, static_cast<DWORD *>(bank) + v792.words);

    bank = NULL;
    bk_create(pevent, "TDC0", TID_DWORD, &bank);
    memcpy(bank, v1190_data, v1190.words * sizeof(DWORD));
    bk_close(pevent, static_cast<DWORD *>(bank) + v1190.words);

    bank = NULL;
    bk_create(pevent, "TDC1", TID_DWORD, &bank);
    memcpy(bank, v775_data, v775.words * sizeof(DWORD));
    bk_close(pevent, static_cast<DWORD *>(bank) + v775.words);
    return bk_size(pevent);
}


/* Acquire one event per module, check pairing, and publish one MIDAS event. */
INT read_vme_event(char *pevent, INT off)
{
    if (!gVme || gReadoutFailed)
        return 0;

    // V792 is the primary trigger. Do not consume it until both TDC FIFOs are ready.
    if (!wait_for_v1190_data_ready() || !wait_for_v775_data_ready())
        return 0;

    DWORD v792_data[V792_MAX_EVENT_WORDS];
    DWORD v1190_data[V1190_MAX_EVENT_WORDS];
    DWORD v775_data[V775_MAX_EVENT_WORDS];
    const V792EventInfo v792 = read_v792_single_event(v792_data);
    if (!v792.valid || v792.words == 0) {
        gReadoutFailed = true;
        cm_msg(MERROR, frontend_name,
               "V792 readout disabled after error; no partial bank sent. Check hardware and restart the run to reset readout.");
        return 0;
    }

    const V1190EventInfo v1190 = read_v1190_single_event(v1190_data);
    if (!v1190.valid || v1190.words == 0) {
        gReadoutFailed = true;
        cm_msg(MERROR, frontend_name,
               "V1190 readout disabled after error; V792 event was consumed but no partial MIDAS event was sent. Check hardware and restart the run.");
        return 0;
    }

    const V775EventInfo v775 = read_v775_single_event(v775_data);
    if (!v775.valid || v775.words == 0) {
        gReadoutFailed = true;
        cm_msg(MERROR, frontend_name,
               "V775 readout disabled after error; V792/V1190 events were consumed but no partial MIDAS event was sent. Check hardware and restart the run.");
        return 0;
    }

    if (!check_event_counter_match(v792, v1190, v775))
        log_event_counter_mismatch(v792, v1190, v775, SERIAL_NUMBER(pevent));

    return build_midas_event(pevent,
                             v792_data, v792,
                             v1190_data, v1190,
                             v775_data, v775);
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

    {""}                          // End of equipment list
};
