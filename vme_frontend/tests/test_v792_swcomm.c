/* V792 SW comm diagnostic using CAENVMELib directly.
 * Build: gcc -std=c11 -O2 -Wall -Wextra -Wpedantic
 *        -o test_v792_swcomm.exe test_v792_swcomm.c -lCAENVME
 * Two SW comm writes only; no reset, configuration changes or FIFO reads.
 * State registers and counter halves are sequential, not atomic snapshots.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <CAENVMElib.h>

static const uint32_t v792_base = 0x00600000;
enum { CSR1, CSR2, BIT_SET2, COUNTER_LOW, COUNTER_HIGH, REGISTER_COUNT };
static const struct {
    const char *name;
    uint32_t offset;
} registers[REGISTER_COUNT] = {
    {"CSR1",               0x100E},
    {"CSR2",               0x1022},
    {"BIT SET 2",          0x1032},
    {"Counter Low (raw)",  0x1024},
    {"Counter High (raw)", 0x1026}
};

static int read_state(int32_t handle, const char *label)
{
    uint16_t values[REGISTER_COUNT] = {0};
    CVErrorCodes statuses[REGISTER_COUNT];
    int result = EXIT_SUCCESS;

    printf("\n%s\n", label);
    for (int i = 0; i < REGISTER_COUNT; ++i) {
        statuses[i] = CAENVME_ReadCycle(handle,
            v792_base + registers[i].offset, &values[i], cvA24_U_DATA, cvD16);
        printf("  %-18s offset=0x%04" PRIX32 " ",
               registers[i].name, registers[i].offset);
        if (statuses[i] == cvSuccess)
            printf("raw=0x%04X ", (unsigned int)values[i]);
        else {
            printf("INVALID ");
            result = EXIT_FAILURE;
        }
        printf("rc=%d (%s)\n", (int)statuses[i], CAENVME_DecodeError(statuses[i]));
    }
    if (statuses[CSR1] == cvSuccess)
        printf("  DataReady (CSR1 bit 0) = %u\n", (unsigned int)(values[CSR1] & 1u));
    else
        puts("  DataReady = INVALID (CSR1 read failed)");

    if (statuses[COUNTER_LOW] == cvSuccess && statuses[COUNTER_HIGH] == cvSuccess) {
        uint32_t counter = ((uint32_t)(values[COUNTER_HIGH] & 0xFFu) << 16)
                         | (uint32_t)values[COUNTER_LOW];
        printf("  Event Counter (24 bit) = 0x%06" PRIX32 "\n", counter);
    } else {
        puts("  Event Counter (24 bit) = INVALID (counter read failed)");
    }
    return result;
}

static int write_swcomm(int32_t handle, unsigned int number)
{
    const uint16_t value = 0x0000;
    CVErrorCodes status = CAENVME_WriteCycle(handle, v792_base + 0x1068,
                                            &value, cvA24_U_DATA, cvD16);
    printf("\nSW comm #%u: address=0x%08" PRIX32
           " value=0x0000 rc=%d (%s)\n",
           number, v792_base + 0x1068, (int)status, CAENVME_DecodeError(status));
    return status == cvSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(void)
{
    uint32_t link = 0;
    int32_t handle = -1;
    int result = EXIT_FAILURE;
    CVErrorCodes status;
    struct timespec delay = {0, 10 * 1000 * 1000};

    setbuf(stdout, NULL);
    status = CAENVME_Init2(cvUSB_V3718, &link, 0, &handle);
    printf("CAENVME_Init2(cvUSB_V3718, link=0, board=0): rc=%d (%s)\n",
           (int)status, CAENVME_DecodeError(status));
    if (status != cvSuccess)
        return EXIT_FAILURE;

    puts("V792 base=0x00600000; cvA24_U_DATA / cvD16.");
    puts("Register samples are sequential; counter Low/High are not atomic.");
    if (read_state(handle, "Before SW comm") != EXIT_SUCCESS)
        goto close;
    if (write_swcomm(handle, 1) != EXIT_SUCCESS)
        goto close;
    if (read_state(handle, "Immediately after SW comm #1") != EXIT_SUCCESS)
        goto close;

    /* Wait 10 ms after the immediate sample; resume the wait if interrupted. */
    while (nanosleep(&delay, &delay) != 0) {
        if (errno != EINTR) {
            perror("nanosleep");
            goto close;
        }
    }
    if (read_state(handle, "10 ms after the immediate sample") != EXIT_SUCCESS)
        goto close;
    if (write_swcomm(handle, 2) != EXIT_SUCCESS)
        goto close;
    result = read_state(handle, "Immediately after SW comm #2");

close:
    if (result != EXIT_SUCCESS)
        puts("Diagnostic aborted after an error; no further SW comm writes.");
    status = CAENVME_End(handle);
    printf("\nCAENVME_End: rc=%d (%s)\n", (int)status, CAENVME_DecodeError(status));
    if (status != cvSuccess)
        result = EXIT_FAILURE;
    return result;
}
