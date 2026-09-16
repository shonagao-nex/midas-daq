/* Read-only V792/V775 register comparison using CAENVMELib directly.
 * Build: gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -o
 *        test_v7xx_counter.exe test_v7xx_counter.c -lCAENVME
 * Counter halves are separate reads, not an atomic snapshot.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <CAENVMElib.h>

enum { COUNTER_LOW = 6, COUNTER_HIGH = 7, REGISTER_COUNT = 8 };

static const struct {
    const char *name;
    uint32_t offset;
} registers[REGISTER_COUNT] = {
    {"Firmware Revision",  0x1000},
    {"GEO Address",        0x1002},
    {"CSR1",               0x100E},
    {"CSR2",               0x1022},
    {"BIT SET 1",          0x1006},
    {"BIT SET 2",          0x1032},
    {"Counter Low (raw)",   0x1024},
    {"Counter High (raw)",  0x1026}
};

struct sample {
    uint16_t value;
    CVErrorCodes status;
};

static void format_sample(char *text, size_t size, const struct sample *sample)
{
    if (sample->status == cvSuccess)
        snprintf(text, size, "0x%04X  rc=%d (%s)",
                 (unsigned int)sample->value, (int)sample->status,
                 CAENVME_DecodeError(sample->status));
    else
        snprintf(text, size, "INVALID rc=%d (%s)",
                 (int)sample->status, CAENVME_DecodeError(sample->status));
}

int main(void)
{
    const uint32_t bases[2] = {0x00600000, 0x00000000};
    struct sample samples[2][REGISTER_COUNT] = {0};
    uint32_t link = 0;
    int32_t handle = -1;
    int result = EXIT_SUCCESS;
    CVErrorCodes status;
    char columns[2][160];

    status = CAENVME_Init2(cvUSB_V3718, &link, 0, &handle);
    printf("CAENVME_Init2(cvUSB_V3718, link=0, board=0): rc=%d (%s)\n",
           (int)status, CAENVME_DecodeError(status));
    if (status != cvSuccess)
        return EXIT_FAILURE;

    puts("Read-only comparison: cvA24_U_DATA / cvD16; no VME writes.");
    printf("%-22s | %-48s | %-48s\n", "Register (offset)",
           "V792 base=0x00600000", "V775 base=0x00000000");

    for (int i = 0; i < REGISTER_COUNT; ++i) {
        for (int module = 0; module < 2; ++module) {
            struct sample *sample = &samples[module][i];
            sample->status = CAENVME_ReadCycle(handle,
                bases[module] + registers[i].offset, &sample->value,
                cvA24_U_DATA, cvD16);
            if (sample->status != cvSuccess)
                result = EXIT_FAILURE;
            format_sample(columns[module], sizeof columns[module], sample);
        }
        printf("%-18s %04" PRIX32 " | %-48s | %-48s\n",
               registers[i].name, registers[i].offset, columns[0], columns[1]);
    }

    for (int module = 0; module < 2; ++module) {
        const struct sample *high = &samples[module][COUNTER_HIGH];
        if (high->status == cvSuccess)
            snprintf(columns[module], sizeof columns[module], "0x%02X",
                     (unsigned int)(high->value & 0xFFu));
        else
            snprintf(columns[module], sizeof columns[module], "INVALID (High read failed)");
    }
    printf("%-22s | %-48s | %-48s\n", "Counter High & 0xFF",
           columns[0], columns[1]);

    for (int module = 0; module < 2; ++module) {
        const struct sample *low = &samples[module][COUNTER_LOW];
        const struct sample *high = &samples[module][COUNTER_HIGH];
        if (low->status == cvSuccess && high->status == cvSuccess) {
            uint32_t counter = ((uint32_t)(high->value & 0xFFu) << 16)
                             | (uint32_t)low->value;
            snprintf(columns[module], sizeof columns[module], "0x%06" PRIX32,
                     counter);
        } else {
            snprintf(columns[module], sizeof columns[module], "INVALID (counter read failed)");
        }
    }
    printf("%-22s | %-48s | %-48s\n", "Counter combined (24b)",
           columns[0], columns[1]);
    puts("Note: Low/High were read separately; live counters may change between reads.");

    status = CAENVME_End(handle);
    printf("CAENVME_End: rc=%d (%s)\n", (int)status, CAENVME_DecodeError(status));
    if (status != cvSuccess)
        result = EXIT_FAILURE;
    return result;
}
