/* One-shot V1720E Output Buffer readout diagnostic for an isolated crate.
 *
 * Build (no frontend objects or MIDAS connection):
 *   cc -std=c11 -O2 -Wall -Wextra -Wpedantic \
 *      -o /tmp/v1720e_blt_window_diagnostic \
 *      vme_frontend/tests/v1720e_blt_window_diagnostic.c -lCAENVME
 *
 * Run only with fevme stopped and Global BUSY asserted or external triggers
 * stopped. Prepare exactly one stored 1028-word event for EACH invocation.
 * The --isolated flag acknowledges these operator-verified prerequisites.
 * Output Buffer reads consume data. No VME writes, reset, or retry are made.
 */
#define _POSIX_C_SOURCE 200809L
#include <CAENVMElib.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum { EVENT_WORDS = 1028, EVENT_BYTES = EVENT_WORDS * 4 };
#define BASE UINT32_C(0x11110000)
#define CANARY UINT32_C(0xD15EA5ED)

typedef enum { MODE_NORMAL, MODE_FIFO, MODE_CURRENT } Mode;

static uint64_t monotonic_ns(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static int read_register(int32_t handle, uint32_t offset, uint32_t *value)
{
    CVErrorCodes rc = CAENVME_ReadCycle(handle, BASE + offset, value,
                                         cvA32_U_DATA, cvD32);
    if (rc == cvSuccess) return 0;
    fprintf(stderr, "D32 read at 0x%08" PRIX32 " failed: %d (%s)\n",
            BASE + offset, (int)rc, CAENVME_DecodeError(rc));
    return -1;
}

static void print_event(const uint32_t *data, size_t words)
{
    if (words < 4) return;
    printf("first: %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 "\n",
           data[0], data[1], data[2], data[3]);
    printf("last received (%zu/%d words): %08" PRIX32 " %08" PRIX32
           " %08" PRIX32 " %08" PRIX32 "\n", words, EVENT_WORDS,
           data[words - 4], data[words - 3], data[words - 2], data[words - 1]);
    printf("header: marker=0x%X size=%" PRIu32 " mask=0x%02" PRIX32
           " counter=%" PRIu32 " TTT=%" PRIu32 "\n",
           (unsigned)(data[0] >> 28), data[0] & UINT32_C(0x0FFFFFFF),
           data[1] & UINT32_C(0xFF), data[2] & UINT32_C(0x00FFFFFF),
           data[3] & UINT32_C(0x7FFFFFFF));
}

int main(int argc, char **argv)
{
    Mode mode;
    int32_t handle = -1;
    uint32_t link = 0;
    uint32_t acquisition_status = 0, stored = 0, size = 0, firmware = 0;
    struct { uint32_t before, data[EVENT_WORDS], after; } guarded = {0};
    CVErrorCodes rc;
    int count = 0, requested = 0, result = 1;
    uint64_t start, end;

    if (argc != 3 || strcmp(argv[2], "--isolated") != 0 ||
        (strcmp(argv[1], "normal") && strcmp(argv[1], "fifo") &&
         strcmp(argv[1], "current"))) {
        fprintf(stderr, "Usage: %s {normal|fifo|current} --isolated\n"
                "Requires fevme stopped, triggers blocked, and exactly one stored event.\n",
                argv[0]);
        return 2;
    }
    mode = strcmp(argv[1], "normal") == 0 ? MODE_NORMAL :
           strcmp(argv[1], "fifo") == 0 ? MODE_FIFO : MODE_CURRENT;
    guarded.before = guarded.after = CANARY;

    rc = CAENVME_Init2(cvUSB_V3718, &link, 0, &handle);
    if (rc != cvSuccess) {
        fprintf(stderr, "CAENVME_Init2 failed: %d (%s)\n",
                (int)rc, CAENVME_DecodeError(rc));
        return 1;
    }
    if (read_register(handle, 0x8124, &firmware) ||
        read_register(handle, 0x8104, &acquisition_status) ||
        read_register(handle, 0x812C, &stored) ||
        read_register(handle, 0x814C, &size))
        goto done;

    printf("mode=%s base=0x%08" PRIX32 " ROC-FW=0x%08" PRIX32
           " AcqStatus=0x%08" PRIX32 " EventStored=%" PRIu32
           " EventSize=%" PRIu32 "\n", argv[1], BASE, firmware,
           acquisition_status, stored, size);
    fflush(stdout);
    if (!(acquisition_status & UINT32_C(0x8)) || stored != 1 ||
        size != EVENT_WORDS || (firmware & UINT32_C(0xFFFF)) != UINT32_C(0x0405)) {
        fputs("Preflight failed: require Data Ready, exactly one event, "
              "1028 words, and ROC firmware 4.5. No Output Buffer read.\n", stderr);
        goto done;
    }

    puts("Starting one destructive Output Buffer read; no retry on failure.");
    fflush(stdout);
    start = monotonic_ns();
    if (mode == MODE_NORMAL) {
        requested = EVENT_BYTES;
        rc = CAENVME_BLTReadCycle(handle, BASE, guarded.data, requested,
                                  cvA32_U_BLT, cvD32, &count);
    } else if (mode == MODE_FIFO) {
        requested = EVENT_BYTES;
        rc = CAENVME_FIFOBLTReadCycle(handle, BASE, guarded.data, requested,
                                      cvA32_U_BLT, cvD32, &count);
    } else {
        int i;
        for (i = 0; i < 4; ++i) {
            if (read_register(handle, 0, &guarded.data[i])) {
                fprintf(stderr, "Header D32 failed after %d words; stop.\n", i);
                goto done;
            }
        }
        if ((guarded.data[0] >> 28) != 0xA ||
            (guarded.data[0] & UINT32_C(0x0FFFFFFF)) != size) {
            fputs("Header size mismatch after four D32 reads; stop.\n", stderr);
            goto done;
        }
        requested = EVENT_BYTES - 16;
        rc = CAENVME_BLTReadCycle(handle, BASE, &guarded.data[4], requested,
                                  cvA32_U_BLT, cvD32, &count);
    }
    end = monotonic_ns();
    printf("BLT: rc=%d (%s) requested=%d actual=%d bytes\n",
           (int)rc, CAENVME_DecodeError(rc), requested, count);
    if (start && end >= start)
        printf("readout duration: %.3f us (CLOCK_MONOTONIC)\n",
               (double)(end - start) / 1000.0);
    printf("canaries: before=%08" PRIX32 " after=%08" PRIX32 "\n",
           guarded.before, guarded.after);
    if (count >= 0 && count <= requested && count % 4 == 0)
        print_event(guarded.data,
                    (mode == MODE_CURRENT ? 4u : 0u) + (size_t)count / 4u);

    if (rc != cvSuccess || count != requested ||
        guarded.before != CANARY || guarded.after != CANARY ||
        (guarded.data[0] >> 28) != 0xA ||
        (guarded.data[0] & UINT32_C(0x0FFFFFFF)) != size ||
        (guarded.data[1] & UINT32_C(0xFF)) != UINT32_C(0xFF)) {
        fputs("FAIL: partial/malformed transfer; do not read this event again.\n", stderr);
        goto done;
    }
    puts("PASS: exactly one 1028-word event transferred with a valid header and mask.");
    result = 0;
done:
    rc = CAENVME_End(handle);
    if (rc != cvSuccess) {
        fprintf(stderr, "CAENVME_End failed: %d (%s)\n",
                (int)rc, CAENVME_DecodeError(rc));
        result = 1;
    }
    return result;
}
