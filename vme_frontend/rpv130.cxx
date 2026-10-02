#include "rpv130.h"

#include <time.h>

namespace {

const mvme_addr_t RPV130_LATCH1  = 0x00;
const mvme_addr_t RPV130_LATCH2  = 0x02;
const mvme_addr_t RPV130_RSFF    = 0x04;
const mvme_addr_t RPV130_THROUGH = 0x06;
const mvme_addr_t RPV130_CSR1    = 0x0C;
const mvme_addr_t RPV130_CSR2    = 0x0E;

uint64_t monotonic_ns()
{
    struct timespec ts = {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
}

int read_d16(MVME_INTERFACE *mvme, mvme_addr_t address, uint8_t *value)
{
    uint16_t raw = 0;
    const int result = mvme_read(mvme, &raw, address, sizeof(raw));
    if (result == MVME_SUCCESS)
        *value = static_cast<uint8_t>(raw & 0x00FFu);
    return result;
}

int write_d16(MVME_INTERFACE *mvme, mvme_addr_t address, uint8_t value)
{
    uint16_t raw = value;
    return mvme_write(mvme, address, &raw, sizeof(raw));
}

int select_rpv130_mode(MVME_INTERFACE *mvme, int *saved_am, int *saved_dmode)
{
    if (!mvme || !saved_am || !saved_dmode)
        return MVME_INVALID_PARAM;
    int result = mvme_get_am(mvme, saved_am);
    if (result != MVME_SUCCESS)
        return result;
    result = mvme_get_dmode(mvme, saved_dmode);
    if (result != MVME_SUCCESS)
        return result;
    result = mvme_set_am(mvme, MVME_AM_A16_ND);
    if (result != MVME_SUCCESS)
        return result;
    result = mvme_set_dmode(mvme, MVME_DMODE_D16);
    if (result != MVME_SUCCESS) {
        mvme_set_am(mvme, *saved_am);
        return result;
    }
    return MVME_SUCCESS;
}

int restore_mode(MVME_INTERFACE *mvme, int saved_am, int saved_dmode)
{
    const int dmode_result = mvme_set_dmode(mvme, saved_dmode);
    const int am_result = mvme_set_am(mvme, saved_am);
    return dmode_result != MVME_SUCCESS ? dmode_result : am_result;
}

uint8_t read_register(MVME_INTERFACE *mvme, mvme_addr_t address, int *status)
{
    int saved_am = 0;
    int saved_dmode = 0;
    uint8_t value = 0;
    int result = select_rpv130_mode(mvme, &saved_am, &saved_dmode);
    const bool mode_selected = result == MVME_SUCCESS;
    if (mode_selected)
        result = read_d16(mvme, address, &value);
    if (mode_selected) {
        const int restore_result = restore_mode(mvme, saved_am, saved_dmode);
        if (result == MVME_SUCCESS && restore_result != MVME_SUCCESS)
            result = restore_result;
    }
    if (status)
        *status = result;
    return value;
}

} // namespace

uint8_t rpv130_read_csr1(MVME_INTERFACE *mvme, mvme_addr_t base, int *status)
{
    return read_register(mvme, base + RPV130_CSR1, status);
}

int rpv130_read_status(MVME_INTERFACE *mvme, mvme_addr_t base,
                       RPV130_STATUS *status)
{
    if (!mvme || !status)
        return MVME_INVALID_PARAM;

    int saved_am = 0;
    int saved_dmode = 0;
    int result = select_rpv130_mode(mvme, &saved_am, &saved_dmode);
    if (result != MVME_SUCCESS)
        return result;

    RPV130_STATUS values = {};
    const mvme_addr_t offsets[] = {
        RPV130_LATCH1, RPV130_LATCH2, RPV130_RSFF,
        RPV130_THROUGH, RPV130_CSR1, RPV130_CSR2
    };
    uint8_t *destinations[] = {
        &values.latch1, &values.latch2, &values.rsff,
        &values.through, &values.csr1, &values.csr2
    };
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        result = read_d16(mvme, base + offsets[i], destinations[i]);
        if (result != MVME_SUCCESS)
            break;
    }

    const int restore_result = restore_mode(mvme, saved_am, saved_dmode);
    if (result == MVME_SUCCESS && restore_result != MVME_SUCCESS)
        result = restore_result;
    if (result == MVME_SUCCESS)
        *status = values;
    return result;
}

int rpv130_read_busy1(MVME_INTERFACE *mvme, mvme_addr_t base,
                      bool *busy, uint8_t *csr1)
{
    if (!busy) return MVME_INVALID_PARAM;
    int result = MVME_SUCCESS;
    const uint8_t raw = rpv130_read_csr1(mvme, base, &result);
    if (result == MVME_SUCCESS) {
        *busy = (raw & RPV130_CSR1_BUSY1) != 0;
        if (csr1) *csr1 = raw;
    }
    return result;
}

static int write_channel1_sequence(MVME_INTERFACE *mvme, mvme_addr_t base,
                                    bool rearm, uint8_t *csr1)
{
    if (!mvme) return MVME_INVALID_PARAM;
    int saved_am = 0, saved_dmode = 0;
    int result = select_rpv130_mode(mvme, &saved_am, &saved_dmode);
    if (result != MVME_SUCCESS) return result;

    uint8_t before = 0;
    result = read_d16(mvme, base + RPV130_CSR1, &before);
    const uint8_t preserve = before & RPV130_CSR1_ENABLE3;
    if (result == MVME_SUCCESS) {
        result = write_d16(mvme, base + RPV130_CSR1,
                           preserve | RPV130_CSR1_CLR1);
    }
    if (result == MVME_SUCCESS) {
        result = write_d16(mvme, base + RPV130_CSR1,
                           preserve | (rearm ? RPV130_CSR1_CHANNEL1_ARMED : 0));
    }
    uint8_t raw = 0;
    if (result == MVME_SUCCESS)
        result = read_d16(mvme, base + RPV130_CSR1, &raw);
    if (result == MVME_SUCCESS) {
        const uint8_t expected = preserve |
            (rearm ? RPV130_CSR1_CHANNEL1_ARMED : 0);
        // A new FIN1 edge may assert BUSY1 immediately after re-arm.
        // At BOR the caller checks BUSY1 separately while Global BUSY is on.
        const uint8_t check_bits = RPV130_CSR1_ENABLE3 |
            RPV130_CSR1_CHANNEL1_ARMED |
            (rearm ? 0 : RPV130_CSR1_BUSY1);
        if ((raw & check_bits) != expected)
            result = MVME_ACCESS_ERROR;
        if (csr1) *csr1 = raw;
    }
    const int restore_result = restore_mode(mvme, saved_am, saved_dmode);
    if (result == MVME_SUCCESS) result = restore_result;
    return result;
}

int rpv130_clear_busy1_and_rearm(MVME_INTERFACE *mvme, mvme_addr_t base,
                                  uint8_t *csr1)
{
    return write_channel1_sequence(mvme, base, true, csr1);
}

static int clear_busy1_preserving_enable_state(
    MVME_INTERFACE *mvme, mvme_addr_t base, uint8_t *csr1,
    RPV130_BUSY_TIMING *timing)
{
    if (timing) *timing = {};
    if (!mvme) return MVME_INVALID_PARAM;
    int saved_am = 0, saved_dmode = 0;
    int result = select_rpv130_mode(mvme, &saved_am, &saved_dmode);
    if (result != MVME_SUCCESS) return result;

    uint8_t before = 0;
    result = read_d16(mvme, base + RPV130_CSR1, &before);
    const uint8_t settings = before &
        (RPV130_CSR1_ENABLE3 | RPV130_CSR1_CHANNEL1_ARMED);
    if (result == MVME_SUCCESS &&
        (settings & RPV130_CSR1_CHANNEL1_ARMED) != RPV130_CSR1_CHANNEL1_ARMED)
        result = MVME_ACCESS_ERROR;
    if (result == MVME_SUCCESS) {
        // BUSY status bits are read-only; write only settings and CLR1.
        if (timing) timing->clr1_before_ns = monotonic_ns();
        result = write_d16(mvme, base + RPV130_CSR1,
                           settings | RPV130_CSR1_CLR1);
        if (timing) timing->clr1_after_ns = monotonic_ns();
    }
    uint8_t raw = 0;
    if (result == MVME_SUCCESS)
        result = read_d16(mvme, base + RPV130_CSR1, &raw);
    if (result == MVME_SUCCESS) {
        // A new FIN1 may set BUSY1 before this readback.
        if ((raw & (RPV130_CSR1_ENABLE3 | RPV130_CSR1_CHANNEL1_ARMED)) !=
            settings)
            result = MVME_ACCESS_ERROR;
        if (csr1) *csr1 = raw;
    }
    const int restore_result = restore_mode(mvme, saved_am, saved_dmode);
    if (result == MVME_SUCCESS) result = restore_result;
    return result;
}

int rpv130_clear_busy1_preserving_enable_state(
    MVME_INTERFACE *mvme, mvme_addr_t base, uint8_t *csr1)
{
    return clear_busy1_preserving_enable_state(mvme, base, csr1, NULL);
}

int rpv130_clear_busy1_preserving_enable_state_timed(
    MVME_INTERFACE *mvme, mvme_addr_t base, uint8_t *csr1,
    RPV130_BUSY_TIMING *timing)
{
    return clear_busy1_preserving_enable_state(mvme, base, csr1, timing);
}

int rpv130_clear_busy1_and_disable(MVME_INTERFACE *mvme, mvme_addr_t base,
                                    uint8_t *csr1)
{
    return write_channel1_sequence(mvme, base, false, csr1);
}
