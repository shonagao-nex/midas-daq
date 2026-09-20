/* V1190A temporary Trigger Matching + Empty Event soft-trigger diagnostic.
 *
 * This program requires the module to start in Continuous Storage mode with
 * an empty output buffer and Empty Event disabled.  It temporarily enables
 * Trigger Matching and the Control Register Empty Event bit, issues exactly
 * one software trigger, and reads at most one event.  Cleanup restores only
 * the Empty Event bit (by read-modify-write) and restores Continuous Storage.
 * It performs no reset, data clear, or event-counter reset.
 *
 * Build (but do not run merely to test the build):
 *   gcc -std=c11 -O2 -Wall -Wextra -Wpedantic \
 *       -o test_v1190_soft_event.exe test_v1190_soft_event.c -lCAENVME
 */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <CAENVMElib.h>

#define V1190_BASE             UINT32_C(0x00C10000)
#define V1190_OUTPUT_BUFFER    UINT32_C(0x0000)
#define V1190_CONTROL          UINT32_C(0x1000)
#define V1190_STATUS           UINT32_C(0x1002)
#define V1190_SOFT_TRIGGER     UINT32_C(0x101A)
#define V1190_EVENT_STORED     UINT32_C(0x1020)
#define V1190_MICRO_DATA       UINT32_C(0x102E)
#define V1190_MICRO_HANDSHAKE  UINT32_C(0x1030)

#define CONTROL_EMPTY_EVENT    UINT16_C(0x0008)
#define STATUS_DATA_READY      UINT16_C(0x0001)
#define STATUS_TRIGGER_MATCH   UINT16_C(0x0008)
#define MICRO_WRITE_OK         UINT16_C(0x0001)
#define MICRO_READ_OK          UINT16_C(0x0002)
#define OPCODE_TRIGGER_MATCH   UINT16_C(0x0000)
#define OPCODE_CONTINUOUS      UINT16_C(0x0100)
#define OPCODE_READ_ACQ_MODE   UINT16_C(0x0200)

enum {
    MICRO_MAX_POLLS = 1000,
    DATA_MAX_POLLS = 100,
    MAX_EVENT_WORDS = 4096,
    POLL_DELAY_NS = 1000000
};

enum word_type {
    TYPE_MEASUREMENT      = 0x00,
    TYPE_TDC_HEADER       = 0x01,
    TYPE_TDC_TRAILER      = 0x03,
    TYPE_ERROR            = 0x04,
    TYPE_GLOBAL_HEADER    = 0x08,
    TYPE_GLOBAL_TRAILER   = 0x10,
    TYPE_EXT_TRIGGER_TIME = 0x11
};

struct event_summary {
    unsigned words_read;
    uint32_t event_counter;
    unsigned header_geo;
    unsigned trailer_word_count;
};

static void delay_1ms(void)
{
    const struct timespec delay = { .tv_sec = 0, .tv_nsec = POLL_DELAY_NS };
    (void)nanosleep(&delay, NULL);
}

static void report_vme_error(const char *operation, uint32_t address,
                             CVErrorCodes rc)
{
    fprintf(stderr, "%s at 0x%08" PRIX32 " failed: rc=%d (%s)\n",
            operation, address, (int)rc, CAENVME_DecodeError(rc));
}

static int read16(int32_t handle, uint32_t offset, uint16_t *value)
{
    CVErrorCodes rc = CAENVME_ReadCycle(handle, V1190_BASE + offset, value,
                                        cvA24_U_DATA, cvD16);
    if (rc != cvSuccess) {
        report_vme_error("D16 read", V1190_BASE + offset, rc);
        return -1;
    }
    return 0;
}

static int write16(int32_t handle, uint32_t offset, uint16_t value,
                   const char *operation)
{
    CVErrorCodes rc = CAENVME_WriteCycle(handle, V1190_BASE + offset, &value,
                                         cvA24_U_DATA, cvD16);
    if (rc != cvSuccess) {
        report_vme_error(operation, V1190_BASE + offset, rc);
        return -1;
    }
    return 0;
}

static int read_fifo32(int32_t handle, uint32_t *value)
{
    CVErrorCodes rc = CAENVME_ReadCycle(handle,
                                        V1190_BASE + V1190_OUTPUT_BUFFER,
                                        value, cvA24_U_DATA, cvD32);
    if (rc != cvSuccess) {
        report_vme_error("FIFO D32 read", V1190_BASE, rc);
        return -1;
    }
    return 0;
}

static int micro_wait(int32_t handle, uint16_t ready_bit, const char *what)
{
    uint16_t handshake = 0;

    for (unsigned poll = 0; poll < MICRO_MAX_POLLS; ++poll) {
        if (read16(handle, V1190_MICRO_HANDSHAKE, &handshake) != 0)
            return -1;
        if ((handshake & ready_bit) != 0)
            return 0;
        delay_1ms();
    }
    fprintf(stderr, "microcontroller %s timeout after %u polls "
            "(handshake=0x%04X)\n", what, MICRO_MAX_POLLS, handshake);
    return -1;
}

static int micro_write_opcode(int32_t handle, uint16_t opcode)
{
    if (micro_wait(handle, MICRO_WRITE_OK, "write-ready") != 0)
        return -1;
    if (write16(handle, V1190_MICRO_DATA, opcode,
                "microcontroller opcode write") != 0)
        return -1;
    printf("Microcontroller opcode written: 0x%04X\n", opcode);
    return 0;
}

static int read_acquisition_mode(int32_t handle, uint16_t *mode)
{
    if (micro_write_opcode(handle, OPCODE_READ_ACQ_MODE) != 0)
        return -1;
    if (micro_wait(handle, MICRO_READ_OK, "read-ready") != 0)
        return -1;
    if (read16(handle, V1190_MICRO_DATA, mode) != 0)
        return -1;
    printf("Acquisition mode readback: raw=0x%04X (%s)\n", *mode,
           (*mode & 1u) ? "Trigger Matching" : "Continuous Storage");
    return 0;
}

static void print_status(uint16_t status)
{
    printf("Status: raw=0x%04X DataReady=%u Acquisition=%s\n",
           status, (unsigned)((status & STATUS_DATA_READY) != 0),
           (status & STATUS_TRIGGER_MATCH) != 0
               ? "Trigger Matching" : "Continuous Storage");
}

static const char *word_type_name(unsigned type)
{
    switch (type) {
    case TYPE_GLOBAL_HEADER:    return "Global Header";
    case TYPE_TDC_HEADER:       return "TDC Header";
    case TYPE_MEASUREMENT:      return "Measurement";
    case TYPE_TDC_TRAILER:      return "TDC Trailer";
    case TYPE_ERROR:            return "Error";
    case TYPE_EXT_TRIGGER_TIME: return "Extended Trigger Time Tag";
    case TYPE_GLOBAL_TRAILER:   return "Global Trailer";
    default:                    return "Unknown/unsupported";
    }
}

static void decode_and_print_word(unsigned index, uint32_t word)
{
    unsigned type = (word >> 27) & 0x1fu;

    printf("  [%04u] 0x%08" PRIX32 "  %-25s", index, word,
           word_type_name(type));
    switch (type) {
    case TYPE_GLOBAL_HEADER:
        printf(" event_counter=%" PRIu32 " GEO=%u",
               (word >> 5) & UINT32_C(0x003FFFFF),
               (unsigned)(word & 0x1fu));
        break;
    case TYPE_TDC_HEADER:
        printf(" TDC=%u event_id=%u bunch_id=%u",
               (unsigned)((word >> 24) & 0x3u),
               (unsigned)((word >> 12) & 0xfffu),
               (unsigned)(word & 0xfffu));
        break;
    case TYPE_MEASUREMENT:
        printf(" channel=%u edge=%s value=%u",
               (unsigned)((word >> 19) & 0x7fu),
               (word & UINT32_C(0x04000000)) != 0 ? "trailing" : "leading",
               (unsigned)(word & UINT32_C(0x0007FFFF)));
        break;
    case TYPE_TDC_TRAILER:
        printf(" TDC=%u event_id=%u word_count=%u",
               (unsigned)((word >> 24) & 0x3u),
               (unsigned)((word >> 12) & 0xfffu),
               (unsigned)(word & 0xfffu));
        break;
    case TYPE_ERROR:
        printf(" TDC=%u flags=0x%04X",
               (unsigned)((word >> 24) & 0x3u),
               (unsigned)(word & 0x7fffu));
        break;
    case TYPE_EXT_TRIGGER_TIME:
        printf(" time_tag=0x%07" PRIX32,
               word & UINT32_C(0x07FFFFFF));
        break;
    case TYPE_GLOBAL_TRAILER:
        printf(" status=0x%X word_count=%u GEO=%u",
               (unsigned)((word >> 24) & 0x7u),
               (unsigned)((word >> 5) & 0xffffu),
               (unsigned)(word & 0x1fu));
        break;
    default:
        break;
    }
    putchar('\n');
}

static int read_one_event(int32_t handle, struct event_summary *summary)
{
    *summary = (struct event_summary){0};

    puts("Event readout (D32 single cycles):");
    for (unsigned index = 0; index < MAX_EVENT_WORDS; ++index) {
        uint32_t word;
        unsigned type;

        if (read_fifo32(handle, &word) != 0)
            return -1;
        summary->words_read = index + 1;
        type = (word >> 27) & 0x1fu;
        decode_and_print_word(index, word);

        if (index == 0) {
            if (type != TYPE_GLOBAL_HEADER) {
                fputs("Event format error: first word is not a Global Header; "
                      "no resynchronization read will be attempted.\n", stderr);
                return -1;
            }
            summary->event_counter =
                (word >> 5) & UINT32_C(0x003FFFFF);
            summary->header_geo = word & 0x1fu;
            continue;
        }

        if (type == TYPE_GLOBAL_TRAILER) {
            summary->trailer_word_count = (word >> 5) & 0xffffu;
            printf("Global Header event counter: %" PRIu32 " (0x%06" PRIX32 ")\n",
                   summary->event_counter, summary->event_counter);
            printf("Global Trailer word count: %u; actual words read: %u\n",
                   summary->trailer_word_count, summary->words_read);
            if ((word & 0x1fu) != summary->header_geo) {
                fprintf(stderr, "Event format error: Header GEO %u != "
                        "Trailer GEO %u.\n", summary->header_geo,
                        (unsigned)(word & 0x1fu));
                return -1;
            }
            if (summary->trailer_word_count != summary->words_read) {
                fputs("Event format error: Global Trailer word count does not "
                      "match actual read count.\n", stderr);
                return -1;
            }
            return 0; /* Do not read any word after the Global Trailer. */
        }

        switch (type) {
        case TYPE_TDC_HEADER:
        case TYPE_MEASUREMENT:
        case TYPE_TDC_TRAILER:
        case TYPE_ERROR:
        case TYPE_EXT_TRIGGER_TIME:
            break;
        case TYPE_GLOBAL_HEADER:
            fputs("Event format error: second Global Header before trailer.\n",
                  stderr);
            return -1;
        default:
            fprintf(stderr, "Event format error: unsupported word type 0x%02X.\n",
                    type);
            return -1;
        }
    }
    fprintf(stderr, "Event format error: no Global Trailer within %u words.\n",
            MAX_EVENT_WORDS);
    return -1;
}

static int restore_hardware(int32_t handle, uint16_t saved_control,
                            int mode_may_have_changed,
                            int empty_bit_may_have_changed)
{
    int failed = 0;
    uint16_t value;

    puts("\nRestoration:");
    if (empty_bit_may_have_changed) {
        if (read16(handle, V1190_CONTROL, &value) != 0) {
            fputs("  Cannot restore Empty Event bit: Control read failed.\n",
                  stderr);
            failed = 1;
        } else {
            uint16_t restored = (uint16_t)((value & ~CONTROL_EMPTY_EVENT) |
                                           (saved_control & CONTROL_EMPTY_EVENT));
            printf("  Control RMW: current=0x%04X restored-write=0x%04X\n",
                   value, restored);
            if (write16(handle, V1190_CONTROL, restored,
                        "Control Empty Event restoration") != 0) {
                failed = 1;
            } else if (read16(handle, V1190_CONTROL, &value) != 0) {
                failed = 1;
            } else if ((value & CONTROL_EMPTY_EVENT) !=
                       (saved_control & CONTROL_EMPTY_EVENT)) {
                fprintf(stderr, "  Empty Event restoration verify failed: "
                        "Control=0x%04X.\n", value);
                failed = 1;
            } else {
                printf("  Empty Event restored and verified: %s "
                       "(Control=0x%04X).\n",
                       (value & CONTROL_EMPTY_EVENT) ? "enabled" : "disabled",
                       value);
            }
        }
    } else {
        puts("  Empty Event was not changed; no Control write required.");
    }

    /* Attempt mode restoration even if Control restoration failed. */
    if (mode_may_have_changed) {
        uint16_t mode;
        uint16_t status;

        if (micro_write_opcode(handle, OPCODE_CONTINUOUS) != 0) {
            failed = 1;
        } else if (read_acquisition_mode(handle, &mode) != 0) {
            failed = 1;
        } else if (read16(handle, V1190_STATUS, &status) != 0) {
            failed = 1;
        } else {
            print_status(status);
            if ((mode & 1u) != 0 ||
                (status & STATUS_TRIGGER_MATCH) != 0) {
                fputs("  Continuous Storage restoration verify failed.\n",
                      stderr);
                failed = 1;
            } else {
                puts("  Continuous Storage restored and verified by "
                     "acquisition-mode and Status readback.");
            }
        }
    } else {
        puts("  Acquisition mode was not changed; no mode write required.");
    }
    return failed ? -1 : 0;
}

int main(void)
{
    uint32_t link = 0;
    int32_t handle = -1;
    CVErrorCodes rc;
    uint16_t saved_mode = 0;
    uint16_t mode_readback = 0;
    uint16_t saved_control = 0;
    uint16_t status = 0;
    uint16_t events_stored = 0;
    int have_saved_control = 0;
    int mode_may_have_changed = 0;
    int empty_bit_may_have_changed = 0;
    int result = EXIT_FAILURE;
    struct event_summary event;

    setbuf(stdout, NULL);
    rc = CAENVME_Init2(cvUSB_V3718, &link, 0, &handle);
    printf("Open V3718 (cvUSB_V3718, link=0, board=0): rc=%d (%s)\n",
           (int)rc, CAENVME_DecodeError(rc));
    if (rc != cvSuccess)
        return EXIT_FAILURE;

    puts("V1190A base=0x00C10000, A24_U_DATA, FIFO D32.");
    puts("Initial checks (no configuration change yet):");
    if (read_acquisition_mode(handle, &saved_mode) != 0)
        goto cleanup;
    if (read16(handle, V1190_CONTROL, &saved_control) != 0)
        goto cleanup;
    have_saved_control = 1;
    printf("Saved Control Register: 0x%04X; Empty Event=%s\n",
           saved_control,
           (saved_control & CONTROL_EMPTY_EVENT) ? "enabled" : "disabled");
    if (read16(handle, V1190_STATUS, &status) != 0)
        goto cleanup;
    print_status(status);
    if (read16(handle, V1190_EVENT_STORED, &events_stored) != 0)
        goto cleanup;
    printf("Event Stored: %u\n", events_stored);

    if ((status & STATUS_DATA_READY) != 0 || events_stored != 0) {
        fputs("Refusing test: DataReady and Event Stored must both be zero; "
              "configuration was not changed.\n", stderr);
        goto cleanup;
    }
    if ((saved_mode & 1u) != 0 ||
        (status & STATUS_TRIGGER_MATCH) != 0) {
        fputs("Refusing test: acquisition mode is not Continuous Storage; "
              "configuration was not changed.\n", stderr);
        goto cleanup;
    }
    if ((saved_control & CONTROL_EMPTY_EVENT) != 0) {
        fputs("Refusing test: Empty Event is already enabled; configuration "
              "was not changed.\n", stderr);
        goto cleanup;
    }

    puts("\nTemporary configuration:");
    /* From the write attempt onward, cleanup conservatively assumes the mode
       may have changed, including an ambiguous VME write failure. */
    mode_may_have_changed = 1;
    if (micro_write_opcode(handle, OPCODE_TRIGGER_MATCH) != 0)
        goto cleanup;
    if (read_acquisition_mode(handle, &mode_readback) != 0)
        goto cleanup;
    if (read16(handle, V1190_STATUS, &status) != 0)
        goto cleanup;
    print_status(status);
    if ((mode_readback & 1u) == 0 ||
        (status & STATUS_TRIGGER_MATCH) == 0) {
        fputs("Trigger Matching verification failed.\n", stderr);
        goto cleanup;
    }

    /* Re-read immediately before RMW so unrelated Control bits are retained. */
    if (read16(handle, V1190_CONTROL, &status) != 0)
        goto cleanup;
    empty_bit_may_have_changed = 1;
    if (write16(handle, V1190_CONTROL,
                (uint16_t)(status | CONTROL_EMPTY_EVENT),
                "Control Empty Event enable") != 0)
        goto cleanup;
    if (read16(handle, V1190_CONTROL, &events_stored) != 0)
        goto cleanup;
    printf("Control readback after Empty Event enable: 0x%04X\n",
           events_stored);
    if ((events_stored & CONTROL_EMPTY_EVENT) == 0 ||
        (events_stored & ~CONTROL_EMPTY_EVENT) !=
            (status & ~CONTROL_EMPTY_EVENT)) {
        fputs("Empty Event readback failed or another Control bit changed.\n",
              stderr);
        goto cleanup;
    }

    puts("\nIssuing exactly one V1190 Software Trigger.");
    if (write16(handle, V1190_SOFT_TRIGGER, 0,
                "V1190 Software Trigger") != 0)
        goto cleanup;

    for (unsigned poll = 1; poll <= DATA_MAX_POLLS; ++poll) {
        if (read16(handle, V1190_STATUS, &status) != 0)
            goto cleanup;
        printf("DataReady poll %3u/%u: %u\n", poll, DATA_MAX_POLLS,
               (unsigned)((status & STATUS_DATA_READY) != 0));
        if ((status & STATUS_DATA_READY) != 0)
            break;
        if (poll == DATA_MAX_POLLS) {
            puts("DataReady timeout after 100 polls; no FIFO read performed.");
            goto post_read_state;
        }
        delay_1ms();
    }

    if (read_one_event(handle, &event) != 0)
        goto post_read_state;
    result = EXIT_SUCCESS;

post_read_state:
    puts("\nState after event readout/timeout:");
    if (read16(handle, V1190_STATUS, &status) == 0)
        print_status(status);
    else
        result = EXIT_FAILURE;
    if (read16(handle, V1190_EVENT_STORED, &events_stored) == 0)
        printf("Event Stored: %u\n", events_stored);
    else
        result = EXIT_FAILURE;

cleanup:
    if (have_saved_control &&
        restore_hardware(handle, saved_control, mode_may_have_changed,
                         empty_bit_may_have_changed) != 0)
        result = EXIT_FAILURE;

    rc = CAENVME_End(handle);
    printf("Close V3718: rc=%d (%s)\n",
           (int)rc, CAENVME_DecodeError(rc));
    if (rc != cvSuccess)
        result = EXIT_FAILURE;
    return result;
}
