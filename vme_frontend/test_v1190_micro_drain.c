/* Drain an already-pending V1190 microcontroller response without issuing
 * any new opcode.  No reset, clear, configuration write, or FIFO access is
 * performed.
 *
 * Build:
 *   gcc -std=c11 -O2 -Wall -Wextra -Wpedantic \
 *       -o test_v1190_micro_drain.exe test_v1190_micro_drain.c -lCAENVME
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include <CAENVMElib.h>

#define V1190_BASE             UINT32_C(0x00C10000)
#define V1190_MICRO_DATA       UINT32_C(0x102E)
#define V1190_MICRO_HANDSHAKE  UINT32_C(0x1030)
#define MICRO_WRITE_OK         UINT16_C(0x0001)
#define MICRO_READ_OK          UINT16_C(0x0002)

enum { MAX_DRAIN_WORDS = 8, MAX_WRITE_READY_POLLS = 1000 };

static int read16(int32_t handle, uint32_t offset, uint16_t *value,
                  const char *name)
{
    CVErrorCodes rc = CAENVME_ReadCycle(handle, V1190_BASE + offset, value,
                                        cvA24_U_DATA, cvD16);
    if (rc != cvSuccess) {
        fprintf(stderr, "%s read at 0x%08" PRIX32
                " failed: rc=%d (%s)\n", name, V1190_BASE + offset,
                (int)rc, CAENVME_DecodeError(rc));
        return -1;
    }
    return 0;
}

int main(void)
{
    int32_t handle = -1;
    int link = 0;
    CVErrorCodes rc;
    uint16_t handshake;
    uint16_t word;
    unsigned count = 0;
    unsigned poll;
    int status = 1;

    rc = CAENVME_Init2(cvUSB_V3718, &link, 0, &handle);
    if (rc != cvSuccess) {
        fprintf(stderr, "CAENVME_Init2(cvUSB_V3718, link=0, board=0) failed: "
                "rc=%d (%s)\n", (int)rc, CAENVME_DecodeError(rc));
        return 1;
    }

    puts("V1190 pending micro-response drain: base=0x00C10000");
    puts("No micro opcode or other VME write will be issued.");

    for (;;) {
        if (read16(handle, V1190_MICRO_HANDSHAKE, &handshake,
                   "Micro Handshake") != 0)
            goto done;
        printf("Handshake before word %u: raw=0x%04X READ_OK=%u WRITE_OK=%u\n",
               count, handshake, (unsigned)((handshake & MICRO_READ_OK) != 0),
               (unsigned)((handshake & MICRO_WRITE_OK) != 0));

        if ((handshake & MICRO_READ_OK) == 0)
            break;
        if (count == MAX_DRAIN_WORDS) {
            fprintf(stderr, "Drain limit (%u words) reached while READ_OK is "
                    "still 1; stopping without another Micro Data read.\n",
                    MAX_DRAIN_WORDS);
            goto done;
        }
        if (read16(handle, V1190_MICRO_DATA, &word, "Micro Data") != 0)
            goto done;
        printf("drained[%u] = 0x%04X\n", count, word);
        ++count;
    }

    printf("Drain complete: %u word(s); READ_OK=0.\n", count);
    for (poll = 0; poll < MAX_WRITE_READY_POLLS; ++poll) {
        if ((handshake & MICRO_WRITE_OK) != 0)
            break;
        if (read16(handle, V1190_MICRO_HANDSHAKE, &handshake,
                   "Micro Handshake final check") != 0)
            goto done;
    }
    if ((handshake & MICRO_WRITE_OK) == 0) {
        fprintf(stderr, "Final check failed: WRITE_OK did not return to 1 "
                "within %u handshake reads.\n", MAX_WRITE_READY_POLLS);
        goto done;
    }
    puts("Final check: WRITE_OK=1.");
    status = 0;

done:
    rc = CAENVME_End(handle);
    if (rc != cvSuccess) {
        fprintf(stderr, "CAENVME_End failed: rc=%d (%s)\n",
                (int)rc, CAENVME_DecodeError(rc));
        status = 1;
    }
    return status;
}
