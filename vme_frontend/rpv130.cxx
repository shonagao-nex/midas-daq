#include "rpv130.h"

namespace {

const mvme_addr_t RPV130_LATCH1  = 0x00;
const mvme_addr_t RPV130_LATCH2  = 0x02;
const mvme_addr_t RPV130_RSFF    = 0x04;
const mvme_addr_t RPV130_THROUGH = 0x06;
const mvme_addr_t RPV130_CSR1    = 0x0C;
const mvme_addr_t RPV130_CSR2    = 0x0E;

int read_d16(MVME_INTERFACE *mvme, mvme_addr_t address, uint8_t *value)
{
    uint16_t raw = 0;
    const int result = mvme_read(mvme, &raw, address, sizeof(raw));
    if (result == MVME_SUCCESS)
        *value = static_cast<uint8_t>(raw & 0x00FFu);
    return result;
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

uint8_t rpv130_read_latch1(MVME_INTERFACE *mvme, mvme_addr_t base, int *status)
{
    return read_register(mvme, base + RPV130_LATCH1, status);
}

uint8_t rpv130_read_latch2(MVME_INTERFACE *mvme, mvme_addr_t base, int *status)
{
    return read_register(mvme, base + RPV130_LATCH2, status);
}

uint8_t rpv130_read_rsff(MVME_INTERFACE *mvme, mvme_addr_t base, int *status)
{
    return read_register(mvme, base + RPV130_RSFF, status);
}

uint8_t rpv130_read_through(MVME_INTERFACE *mvme, mvme_addr_t base, int *status)
{
    return read_register(mvme, base + RPV130_THROUGH, status);
}

uint8_t rpv130_read_csr1(MVME_INTERFACE *mvme, mvme_addr_t base, int *status)
{
    return read_register(mvme, base + RPV130_CSR1, status);
}

uint8_t rpv130_read_csr2(MVME_INTERFACE *mvme, mvme_addr_t base, int *status)
{
    return read_register(mvme, base + RPV130_CSR2, status);
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
