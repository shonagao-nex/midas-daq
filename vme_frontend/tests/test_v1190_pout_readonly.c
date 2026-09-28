/* V1190A POUT and buffer snapshot. VME read cycles only.
 * No micro opcodes: even a micro read command requires a VME write.
 * Do not read the output buffer (0x0000) or Event FIFO (0x1038).
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <CAENVMElib.h>

#define BASE UINT32_C(0x00C10000)

static int read16(int32_t handle, uint32_t offset, uint16_t *value)
{
    CVErrorCodes rc = CAENVME_ReadCycle(handle, BASE + offset, value,
                                        cvA24_U_DATA, cvD16);
    if (rc != cvSuccess) {
        fprintf(stderr, "VME D16 read at 0x%08" PRIX32 " failed: %d (%s)\n",
                BASE + offset, (int)rc, CAENVME_DecodeError(rc));
        return -1;
    }
    return 0;
}

static const char *pout_name(unsigned select)
{
    static const char *const names[] = {
        "DATA_READY", "FULL", "ALMOST_FULL", "ERROR"
    };
    return names[select & 3u];
}

static const char *state(unsigned value)
{
    return value ? "asserted" : "deasserted";
}

int main(void)
{
    int32_t handle = -1;
    int link = 0;
    CVErrorCodes rc = CAENVME_Init2(cvUSB_V3718, &link, 0, &handle);
    uint16_t rom, firmware, pout, control, status, stored, almost_level;
    uint16_t fifo_stored, fifo_status;
    unsigned select;
    int result = 1;

    if (rc != cvSuccess) {
        fprintf(stderr, "CAENVME_Init2 failed: %d (%s)\n",
                (int)rc, CAENVME_DecodeError(rc));
        return 1;
    }
    if (read16(handle, 0x4030, &rom) || read16(handle, 0x1026, &firmware) ||
        read16(handle, 0x102C, &pout) || read16(handle, 0x1000, &control) ||
        read16(handle, 0x1002, &status) || read16(handle, 0x1020, &stored) ||
        read16(handle, 0x1022, &almost_level) ||
        read16(handle, 0x103C, &fifo_stored) ||
        read16(handle, 0x103E, &fifo_status))
        goto done;

    printf("V1190 diagnostic (base=0x%08" PRIX32 ", A24/D16):\n", BASE);
    printf("  Board version       : 0x%04X (%s)\n", rom,
           (rom & 0xFFu) == 0 ? "V1190A" :
           (rom & 0xFFu) == 1 ? "V1190B" : "unknown");
    printf("  Firmware revision   : 0x%04X\n", firmware);
    printf("  POUT configuration  : 0x%04X (register 0x102C)\n", pout);
    select = pout & 3u;
    if (pout & (uint16_t)~3u)
        printf("  POUT function       : unknown (unexpected bits 0x%04X; low bits select %s)\n",
               pout & (uint16_t)~3u, pout_name(select));
    else
        printf("  POUT function       : %s (selection %u)\n",
               pout_name(select), select);
    printf("  Control             : 0x%04X; Empty Event %s, Event FIFO %s, Extended Trigger Time Tag %s\n",
           control, (control & 0x0008u) ? "on" : "off",
           (control & 0x0100u) ? "on" : "off",
           (control & 0x0200u) ? "on" : "off");
    printf("  Status              : 0x%04X\n", status);
    printf("  Trigger mode        : %s\n", (status & 0x0008u) ?
           "Trigger Matching" : "Continuous Storage");
    printf("  DATA_READY          : %s (event/data in Output Buffer)\n", state(status & 0x0001u));
    printf("  ALMOST_FULL         : %s\n", state(status & 0x0002u));
    printf("  FULL                : %s\n", state(status & 0x0004u));
    printf("  TDC ERROR bits      : 0x%X\n", (status >> 6) & 0xFu);
    if (!(pout & (uint16_t)~3u)) {
        unsigned active = select == 0 ? (status & 0x0001u) :
                          select == 1 ? (status & 0x0004u) :
                          select == 2 ? (status & 0x0002u) :
                                        (status & 0x03C0u);
        printf("  Selected condition  : %s at Status read\n", state(active));
    }
    printf("  TRIGGER_LOST        : %s\n", state(status & 0x8000u));
    printf("  Events stored       : %u\n", stored);
    printf("  Almost Full level   : %u Output Buffer words\n", almost_level);
    printf("  Event FIFO stored   : %u\n", fifo_stored);
    printf("  Event FIFO status   : 0x%04X (data ready %s, full %s)\n",
           fifo_status, state(fifo_status & 1u), state(fifo_status & 2u));
    puts("  Window/margins      : unavailable without writing a micro read opcode; use existing BOR readback/log if available");
    puts("  Note                : Status read clears its BERR flag; no acquisition data is consumed");
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
