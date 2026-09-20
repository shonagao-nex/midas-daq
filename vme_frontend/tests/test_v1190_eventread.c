/* Standalone V1190B single-event diagnostic using CAENVMELib directly.
 * Build: gcc -std=c11 -O2 -Wall -Wextra -Wpedantic
 *        -o test_v1190_eventread.exe test_v1190_eventread.c -lCAENVME
 *
 * No reset, clear, event reset, or configuration write is performed. One D16
 * Software Trigger write is issued only if the FIFO is initially empty and the
 * Status Register reports Trigger Matching mode. If an event appears, exactly
 * one event is read through its Global Trailer using D32 single cycles.
 */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <CAENVMElib.h>

#define V1190_BASE UINT32_C(0x00C10000)
enum { MAX_WORDS = 4096, MAX_POLLS = 100 };

enum {
    TYPE_MEASUREMENT       = 0x00,
    TYPE_TDC_HEADER        = 0x01,
    TYPE_TDC_TRAILER       = 0x03,
    TYPE_TDC_ERROR         = 0x04,
    TYPE_GLOBAL_HEADER     = 0x08,
    TYPE_GLOBAL_TRAILER    = 0x10,
    TYPE_EXT_TRIGGER_TIME  = 0x11,
    TYPE_FILLER            = 0x18
};

struct module_state {
    uint16_t firmware;
    uint16_t status;
    uint16_t events_stored;
    uint32_t event_counter;
};

struct event_summary {
    unsigned words;
    uint32_t header_event_counter;
    unsigned header_geo;
    unsigned trailer_word_count;
};

static int read16(int32_t handle, uint32_t offset, uint16_t *value,
                  const char *name)
{
    CVErrorCodes rc = CAENVME_ReadCycle(handle, V1190_BASE + offset, value,
                                        cvA24_U_DATA, cvD16);
    printf("  %-18s D16 address=0x%08" PRIX32 " ", name,
           V1190_BASE + offset);
    if (rc == cvSuccess)
        printf("value=0x%04X ", (unsigned)*value);
    else
        printf("value=INVALID ");
    printf("rc=%d (%s)\n", (int)rc, CAENVME_DecodeError(rc));
    return rc == cvSuccess ? 0 : -1;
}

static int read32(int32_t handle, uint32_t offset, uint32_t *value,
                  const char *name)
{
    CVErrorCodes rc = CAENVME_ReadCycle(handle, V1190_BASE + offset, value,
                                        cvA24_U_DATA, cvD32);
    printf("  %-18s D32 address=0x%08" PRIX32 " ", name,
           V1190_BASE + offset);
    if (rc == cvSuccess)
        printf("value=0x%08" PRIX32 " ", *value);
    else
        printf("value=INVALID ");
    printf("rc=%d (%s)\n", (int)rc, CAENVME_DecodeError(rc));
    return rc == cvSuccess ? 0 : -1;
}

static int read_state(int32_t handle, const char *label,
                      struct module_state *state)
{
    printf("\n%s\n", label);
    if (read16(handle, 0x1026, &state->firmware, "Firmware Revision") != 0)
        return -1;
    if (read16(handle, 0x1002, &state->status, "Status") != 0)
        return -1;
    printf("    DataReady=%u AlmostFull=%u Full=%u TriggerMatching=%u"
           " TDCHeaderTrailer=%u\n",
           (unsigned)(state->status & 1u),
           (unsigned)((state->status >> 1) & 1u),
           (unsigned)((state->status >> 2) & 1u),
           (unsigned)((state->status >> 3) & 1u),
           (unsigned)((state->status >> 4) & 1u));
    printf("    Acquisition mode=%s\n",
           (state->status & 0x0008u) ? "Trigger Matching" : "Continuous Storage");
    if (read16(handle, 0x1020, &state->events_stored, "Event Stored") != 0)
        return -1;
    if (read32(handle, 0x101C, &state->event_counter, "Event Counter") != 0)
        return -1;
    return 0;
}

static int issue_soft_trigger(int32_t handle)
{
    const uint16_t value = 0;
    CVErrorCodes rc = CAENVME_WriteCycle(handle, V1190_BASE + 0x101A,
                                         &value, cvA24_U_DATA, cvD16);
    printf("\nSoftware Trigger: write D16 address=0x%08" PRIX32
           " value=0x%04X rc=%d (%s)\n",
           V1190_BASE + UINT32_C(0x101A), (unsigned)value, (int)rc,
           CAENVME_DecodeError(rc));
    return rc == cvSuccess ? 0 : -1;
}

static const char *type_name(unsigned type)
{
    switch (type) {
    case TYPE_MEASUREMENT:      return "TDC Measurement";
    case TYPE_TDC_HEADER:       return "TDC Header";
    case TYPE_TDC_TRAILER:      return "TDC Trailer";
    case TYPE_TDC_ERROR:        return "TDC Error";
    case TYPE_GLOBAL_HEADER:    return "Global Header";
    case TYPE_GLOBAL_TRAILER:   return "Global Trailer";
    case TYPE_EXT_TRIGGER_TIME: return "Extended Trigger Time Tag";
    case TYPE_FILLER:           return "Filler";
    default:                    return "Reserved";
    }
}

static void print_word(unsigned index, uint32_t word)
{
    unsigned type = (word >> 27) & 0x1fu;
    printf("[%04u] read_rc=%d (%s) raw=0x%08" PRIX32
           " type=0x%02X %-25s",
           index, (int)cvSuccess, CAENVME_DecodeError(cvSuccess),
           word, type, type_name(type));
    switch (type) {
    case TYPE_GLOBAL_HEADER:
        printf(" event_counter=0x%06" PRIX32 " GEO=%u",
               (word >> 5) & UINT32_C(0x003FFFFF),
               (unsigned)(word & 0x1fu));
        break;
    case TYPE_TDC_HEADER:
        printf(" TDC_ID=%u event_ID=%u bunch_ID=%u",
               (unsigned)((word >> 24) & 0x3u),
               (unsigned)((word >> 12) & 0xfffu),
               (unsigned)(word & 0xfffu));
        break;
    case TYPE_MEASUREMENT:
        printf(" channel=%u edge=%s measurement=%u",
               (unsigned)((word >> 19) & 0x7fu),
               (word & UINT32_C(0x04000000)) ? "trailing" : "leading",
               (unsigned)(word & UINT32_C(0x0007FFFF)));
        break;
    case TYPE_TDC_TRAILER:
        printf(" TDC_ID=%u event_ID=%u word_count=%u",
               (unsigned)((word >> 24) & 0x3u),
               (unsigned)((word >> 12) & 0xfffu),
               (unsigned)(word & 0xfffu));
        break;
    case TYPE_TDC_ERROR:
        printf(" TDC_ID=%u error_flags=0x%04X",
               (unsigned)((word >> 24) & 0x3u),
               (unsigned)(word & 0x7fffu));
        break;
    case TYPE_GLOBAL_TRAILER:
        printf(" status=0x%X word_count=%u GEO=%u",
               (unsigned)((word >> 24) & 0x7u),
               (unsigned)((word >> 5) & 0xffffu),
               (unsigned)(word & 0x1fu));
        break;
    case TYPE_EXT_TRIGGER_TIME:
        printf(" time_tag=0x%07" PRIX32,
               word & UINT32_C(0x07FFFFFF));
        break;
    default:
        break;
    }
    putchar('\n');
}

/* Returns 0 for a complete event, -1 for malformed/bounded failure, and -2
 * for a VME read failure. On -2 the caller performs no additional VME reads.
 */
static int read_single_event(int32_t handle, uint32_t words[MAX_WORDS],
                             struct event_summary *summary)
{
    *summary = (struct event_summary){0};
    for (unsigned i = 0; i < MAX_WORDS; ++i) {
        uint32_t word = 0;
        CVErrorCodes rc = CAENVME_ReadCycle(handle, V1190_BASE, &word,
                                            cvA24_U_DATA, cvD32);
        if (rc != cvSuccess) {
            fprintf(stderr, "FIFO read [%u] failed: rc=%d (%s)\n",
                    i, (int)rc, CAENVME_DecodeError(rc));
            return -2;
        }

        words[i] = word;
        summary->words = i + 1;
        print_word(i, word);
        unsigned type = (word >> 27) & 0x1fu;

        if (i == 0) {
            if (type != TYPE_GLOBAL_HEADER) {
                fprintf(stderr, "First FIFO word is not a Global Header;"
                        " no resynchronization scan performed.\n");
                return -1;
            }
            summary->header_event_counter =
                (word >> 5) & UINT32_C(0x003FFFFF);
            summary->header_geo = word & 0x1fu;
            continue;
        }

        switch (type) {
        case TYPE_MEASUREMENT:
        case TYPE_TDC_HEADER:
        case TYPE_TDC_TRAILER:
        case TYPE_TDC_ERROR:
        case TYPE_EXT_TRIGGER_TIME:
            break;
        case TYPE_GLOBAL_TRAILER:
            summary->trailer_word_count = (word >> 5) & 0xffffu;
            if ((word & 0x1fu) != summary->header_geo) {
                fprintf(stderr, "Global Header/Trailer GEO mismatch: %u vs %u.\n",
                        summary->header_geo, (unsigned)(word & 0x1fu));
                return -1;
            }
            if (summary->trailer_word_count != summary->words) {
                fprintf(stderr, "Global Trailer word count mismatch: trailer %u,"
                        " actually read %u.\n",
                        summary->trailer_word_count, summary->words);
                return -1;
            }
            return 0; /* Trailer is stored and printed; do not read again. */
        case TYPE_GLOBAL_HEADER:
            fprintf(stderr, "Unexpected second Global Header before trailer.\n");
            return -1;
        case TYPE_FILLER:
            fprintf(stderr, "Unexpected Filler before Global Trailer.\n");
            return -1;
        default:
            fprintf(stderr, "Reserved word type 0x%02X.\n", type);
            return -1;
        }
    }
    fprintf(stderr, "Reached %u-word limit without Global Trailer.\n", MAX_WORDS);
    return -1;
}

int main(void)
{
    uint32_t link = 0;
    int32_t handle = -1;
    uint32_t words[MAX_WORDS];
    struct module_state initial = {0}, final = {0};
    struct event_summary event = {0};
    CVErrorCodes rc;
    int result = EXIT_FAILURE;
    int event_status;
    int final_state_valid = 0;

    setbuf(stdout, NULL);
    rc = CAENVME_Init2(cvUSB_V3718, &link, 0, &handle);
    printf("Open V3718 link=0 board=0: rc=%d (%s)\n",
           (int)rc, CAENVME_DecodeError(rc));
    if (rc != cvSuccess)
        return EXIT_FAILURE;

    puts("V1190 base=0x00C10000, A24_U_DATA; registers D16/D32, FIFO D32.");
    if (read_state(handle, "Initial state", &initial) != 0)
        goto close;

    if (!(initial.status & 0x0008u)) {
        puts("Software Trigger not issued: module is not in Trigger Matching mode.");
        result = EXIT_SUCCESS;
        goto close;
    }
    if ((initial.status & 1u) || initial.events_stored != 0) {
        puts("Software Trigger not issued: output buffer is not empty.");
        goto close;
    }
    if (issue_soft_trigger(handle) != 0)
        goto close;

    for (unsigned i = 0; i < MAX_POLLS; ++i) {
        uint16_t status = 0;
        if (read16(handle, 0x1002, &status, "Poll Status") != 0)
            goto close;
        printf("    poll=%u/%u DataReady=%u\n",
               i + 1, MAX_POLLS, (unsigned)(status & 1u));
        if (status & 1u)
            break;
        if (i + 1 == MAX_POLLS) {
            puts("DataReady timeout; no FIFO read performed.");
            result = EXIT_SUCCESS;
            goto final_state;
        }
        const struct timespec delay = {0, 1000000}; /* 1 ms */
        if (nanosleep(&delay, NULL) != 0) {
            perror("poll sleep");
            goto close;
        }
    }

    puts("\nV1190 event words:");
    event_status = read_single_event(handle, words, &event);
    if (event_status == -2)
        goto close;
    if (event_status == 0)
        result = EXIT_SUCCESS;

final_state:
    if (read_state(handle, "Final state (no further FIFO reads)", &final) != 0)
        result = EXIT_FAILURE;
    else
        final_state_valid = 1;

close:
    printf("\nSummary\n");
    printf("  total_words=%u\n", event.words);
    printf("  Global Header event_counter=0x%06" PRIX32 "\n",
           event.header_event_counter);
    printf("  Global Trailer word_count=%u\n", event.trailer_word_count);
    if (final_state_valid) {
        printf("  final DataReady=%u\n", (unsigned)(final.status & 1u));
        printf("  final Event Stored=%u\n", (unsigned)final.events_stored);
    } else {
        puts("  final DataReady/Event Stored=UNAVAILABLE");
    }

    rc = CAENVME_End(handle);
    printf("Close V3718: rc=%d (%s)\n", (int)rc, CAENVME_DecodeError(rc));
    if (rc != cvSuccess)
        result = EXIT_FAILURE;
    return result;
}
