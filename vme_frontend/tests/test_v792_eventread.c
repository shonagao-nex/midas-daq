/* Standalone V792 single-event diagnostic. Does not use MIDAS.
 * Build: gcc -std=c11 -O2 -Wall -Wextra -Wpedantic
 *        -o test_v792_eventread.exe test_v792_eventread.c -lCAENVME
 * Writes only Data Clear set/clear, Event Counter Reset, and one SW comm.
 */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <CAENVMElib.h>

#define V792_BASE UINT32_C(0x00600000)
enum { MAX_WORDS = 64, MAX_POLLS = 100 };

static int read16(int32_t handle, uint32_t offset, uint16_t *value)
{
    CVErrorCodes rc = CAENVME_ReadCycle(handle, V792_BASE + offset,
                                       value, cvA24_U_DATA, cvD16);
    if (rc != cvSuccess) {
        fprintf(stderr, "Read D16 at 0x%08" PRIX32 " failed: rc=%d (%s)\n",
                V792_BASE + offset, (int)rc, CAENVME_DecodeError(rc));
        return -1;
    }
    return 0;
}

static int write16(int32_t handle, uint32_t offset, uint16_t value,
                   const char *name)
{
    CVErrorCodes rc = CAENVME_WriteCycle(handle, V792_BASE + offset,
                                        &value, cvA24_U_DATA, cvD16);
    printf("%s: write D16 address=0x%08" PRIX32 " value=0x%04X rc=%d (%s)\n",
           name, V792_BASE + offset, (unsigned)value, (int)rc,
           CAENVME_DecodeError(rc));
    return rc == cvSuccess ? 0 : -1;
}

static int print_state(int32_t handle, const char *label, uint16_t *csr1)
{
    uint16_t csr2, low, high;
    printf("\n%s\n", label);
    /* Stop immediately on any failed read; never display a failed value. */
    if (read16(handle, 0x100E, csr1) != 0)
        return -1;
    printf("CSR1=0x%04X DataReady=%u\n", (unsigned)*csr1, (unsigned)(*csr1 & 1u));
    if (read16(handle, 0x1022, &csr2) != 0)
        return -1;
    printf("CSR2=0x%04X\n", (unsigned)csr2);
    if (read16(handle, 0x1024, &low) != 0)
        return -1;
    printf("Event Counter Low(raw)=0x%04X\n", (unsigned)low);
    if (read16(handle, 0x1026, &high) != 0)
        return -1;
    printf("Event Counter High(raw)=0x%04X combined(24bit)=0x%06" PRIX32 "\n",
           (unsigned)high, ((uint32_t)(high & 0xFFu) << 16) | low);
    return 0;
}

static void print_word(unsigned index, uint32_t word)
{
    unsigned type = (word >> 24) & 7u;
    printf("[%02u] raw=0x%08" PRIX32 " type=%u ", index, word, type);
    switch (type) {
    case 2:
        printf("Header GEO=%u crate=%u converted_channels=%u\n",
               (unsigned)(word >> 27), (unsigned)((word >> 16) & 0xFFu),
               (unsigned)((word >> 8) & 0x3Fu));
        break;
    case 0:
        printf("Measurement channel=%u ADC=%u under_threshold=%u overflow=%u\n",
               (unsigned)((word >> 16) & 0x1Fu), (unsigned)(word & 0xFFFu),
               (unsigned)((word >> 13) & 1u), (unsigned)((word >> 12) & 1u));
        break;
    case 4:
        printf("EOB/Footer GEO=%u event_counter=0x%06" PRIX32 "\n",
               (unsigned)(word >> 27), word & UINT32_C(0xFFFFFF));
        break;
    case 6:
        puts("Invalid/Filler");
        break;
    default:
        puts("Reserved/invalid type");
        break;
    }
}

/* Same type, count and GEO checks as fevme_test.cxx.
 * Return -2 on a bus read failure (caller must make no further VME accesses).
 */
static int read_event(int32_t handle, unsigned *nwords)
{
    unsigned expected = 0, measurements = 0, geo = 0;
    *nwords = 0;
    for (unsigned i = 0; i < MAX_WORDS; ++i) {
        uint32_t word = 0;
        CVErrorCodes rc = CAENVME_ReadCycle(handle, V792_BASE, &word,
                                           cvA24_U_DATA, cvD32);
        if (rc != cvSuccess) {
            fprintf(stderr, "FIFO read [%u] failed: rc=%d (%s)\n",
                    i, (int)rc, CAENVME_DecodeError(rc));
            return -2;
        }
        ++*nwords;
        print_word(i, word);
        unsigned type = (word >> 24) & 7u;
        if (i == 0) {
            if (type != 2) {
                fprintf(stderr, "Expected Header at first word; no resynchronization scan.\n");
                return -1;
            }
            expected = (word >> 8) & 0x3Fu;
            geo = word >> 27;
            if (expected > 32 || expected + 2 > MAX_WORDS) {
                fprintf(stderr, "Invalid Header count: %u\n", expected);
                return -1;
            }
        } else {
            if ((word >> 27) != geo || (type != 0 && type != 4)) {
                fprintf(stderr, "Invalid word type/order or GEO mismatch.\n");
                return -1;
            }
            if (type == 4) {
                if (measurements != expected) {
                    fprintf(stderr, "Footer count mismatch: expected %u, received %u\n",
                            expected, measurements);
                    return -1;
                }
                return 0; /* No FIFO access after Footer. */
            }
            if (measurements >= expected) {
                fprintf(stderr, "Expected Footer after %u measurements.\n", measurements);
                return -1;
            }
            ++measurements;
        }
    }
    fprintf(stderr, "Reached %u-word limit without Footer.\n", MAX_WORDS);
    return -1;
}

int main(void)
{
    uint32_t link = 0;
    int32_t handle = -1;
    uint16_t csr1 = 0;
    unsigned nwords = 0;
    int result = EXIT_FAILURE;
    int event_status;
    CVErrorCodes rc;

    setbuf(stdout, NULL);
    rc = CAENVME_Init2(cvUSB_V3718, &link, 0, &handle);
    printf("Open V3718 link=0 board=0: rc=%d (%s)\n", (int)rc, CAENVME_DecodeError(rc));
    if (rc != cvSuccess)
        return EXIT_FAILURE;
    puts("V792 base=0x00600000, A24_U_DATA; registers D16, FIFO D32.");
    puts("State and counter halves are sequential reads, not atomic snapshots.");
    if (print_state(handle, "Initial state", &csr1) != 0)
        goto close;

    if (write16(handle, 0x1032, 0x0004, "Data Clear assert") != 0)
        goto close;
    if (write16(handle, 0x1034, 0x0004, "Data Clear release") != 0)
        goto close;
    if (write16(handle, 0x1040, 0x0001, "Event Counter Reset") != 0)
        goto close;
    if (print_state(handle, "After initialization", &csr1) != 0)
        goto close;
    if (csr1 & 1u) {
        fprintf(stderr, "DataReady is not zero after initialization; aborting.\n");
        goto close;
    }
    if (write16(handle, 0x1068, 0x0000, "SW comm (once)") != 0)
        goto close;

    for (unsigned i = 0; i < MAX_POLLS; ++i) {
        if (read16(handle, 0x100E, &csr1) != 0)
            goto close;
        printf("Poll %u/%u: CSR1=0x%04X DataReady=%u\n",
               i + 1, MAX_POLLS, (unsigned)csr1, (unsigned)(csr1 & 1u));
        if (csr1 & 1u)
            break;
        if (i + 1 < MAX_POLLS) {
            const struct timespec delay = {0, 1000000}; /* 1 ms */
            if (nanosleep(&delay, NULL) != 0) {
                perror("poll sleep");
                goto close;
            }
        }
    }
    if (!(csr1 & 1u)) {
        fprintf(stderr, "DataReady timeout after %u polls.\n", MAX_POLLS);
        goto final_state;
    }
    event_status = read_event(handle, &nwords);
    if (event_status == -2)
        goto close; /* Read failure: do not attempt even final register reads. */
    if (event_status == 0)
        result = EXIT_SUCCESS;

final_state:
    if (print_state(handle, "Final state (no further FIFO reads)", &csr1) != 0)
        result = EXIT_FAILURE;
close:
    printf("\nSuccessfully read FIFO words: %u; diagnostic %s\n",
           nwords, result == EXIT_SUCCESS ? "completed" : "failed");
    rc = CAENVME_End(handle);
    printf("Close V3718: rc=%d (%s)\n", (int)rc, CAENVME_DecodeError(rc));
    if (rc != cvSuccess)
        result = EXIT_FAILURE;
    return result;
}
