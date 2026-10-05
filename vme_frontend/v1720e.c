#include <string.h>

#include "v1720e.h"
#include "v1720e_internal.h"

V1720E_ACQUISITION_STATE v1720e_decode_acquisition_status(DWORD status)
{
    V1720E_ACQUISITION_STATE state;
    state.running = (status & ACQUISITION_STATUS_RUN_ACTIVE) != 0;
    state.event_ready = (status & STATUS_EVENT_READY) != 0;
    state.external_clock = (status & STATUS_EXTERNAL_CLOCK) != 0;
    state.pll_locked = (status & STATUS_PLL_OK) != 0;
    state.board_ready = (status & STATUS_BOARD_READY) != 0;
    return state;
}

V1720E_BOARD_ID v1720e_decode_board_info(DWORD board_info)
{
    V1720E_BOARD_ID id;
    id.low_byte = board_info & 0xFFu;
    id.model_byte = (board_info >> 8) & 0xFFu;
    id.channels = (board_info >> 16) & 0xFFu;
    id.supported = id.low_byte == 0x03u && id.model_byte == 0x02u && id.channels == 8u;
    return id;
}

int v1720e_access_begin(MVME_INTERFACE *vme, int *saved_am, int *saved_mode)
{
    int status;
    status = mvme_get_am(vme, saved_am);
    if (status != MVME_SUCCESS)
        return status;
    status = mvme_get_dmode(vme, saved_mode);
    if (status != MVME_SUCCESS)
        return status;
    status = mvme_set_am(vme, MVME_AM_A32_ND);
    if (status != MVME_SUCCESS)
        return status;
    status = mvme_set_dmode(vme, MVME_DMODE_D32);
    if (status != MVME_SUCCESS) {
        mvme_set_am(vme, *saved_am);
        return status;
    }
    return MVME_SUCCESS;
}

int v1720e_access_end(MVME_INTERFACE *vme, int saved_am, int saved_mode,
                      int operation_status)
{
    int status = mvme_set_dmode(vme, saved_mode);
    int am_status = mvme_set_am(vme, saved_am);
    if (operation_status != MVME_SUCCESS)
        return operation_status;
    if (status != MVME_SUCCESS)
        return status;
    return am_status;
}

int v1720e_read32(MVME_INTERFACE *vme, DWORD base, DWORD offset, DWORD *value)
{
    int saved_am, saved_mode;
    int status = v1720e_access_begin(vme, &saved_am, &saved_mode);
    if (status != MVME_SUCCESS)
        return status;
    status = mvme_read(vme, value, base + offset, sizeof(*value));
    return v1720e_access_end(vme, saved_am, saved_mode, status);
}

int v1720e_write32(MVME_INTERFACE *vme, DWORD base, DWORD offset, DWORD value)
{
    int saved_am, saved_mode;
    int status = v1720e_access_begin(vme, &saved_am, &saved_mode);
    if (status != MVME_SUCCESS)
        return status;
    status = mvme_write(vme, base + offset, &value, sizeof(value));
    return v1720e_access_end(vme, saved_am, saved_mode, status);
}

int v1720e_write_verify(MVME_INTERFACE *vme, DWORD base, DWORD offset,
                        DWORD value)
{
    DWORD readback = 0;
    int status = v1720e_write32(vme, base, offset, value);
    if (status != MVME_SUCCESS)
        return status;
    status = v1720e_read32(vme, base, offset, &readback);
    if (status != MVME_SUCCESS)
        return status;
    return readback == value ? MVME_SUCCESS : MVME_ACCESS_ERROR;
}

int v1720e_probe(MVME_INTERFACE *vme, DWORD base, V1720E_BOARD_INFO *info)
{
    int status;
    if (!vme || !info)
        return MVME_INVALID_PARAM;
    memset(info, 0, sizeof(*info));
#define READ_INFO(member, reg) \
    do { \
        status = v1720e_read32(vme, base, reg, &info->member); \
        if (status != MVME_SUCCESS) return status; \
    } while (0)
    READ_INFO(board_info, REG_BOARD_INFO);
    READ_INFO(roc_firmware, REG_ROC_FIRMWARE);
    READ_INFO(acquisition_control, REG_ACQUISITION_CONTROL);
    READ_INFO(acquisition_status, REG_ACQUISITION_STATUS);
    READ_INFO(event_stored, REG_EVENT_STORED);
#undef READ_INFO
    return MVME_SUCCESS;
}

int v1720e_data_ready(MVME_INTERFACE *vme, DWORD base, int *ready,
                      DWORD *event_stored, DWORD *acquisition_status)
{
    DWORD status_reg = 0, stored = 0;
    int status;
    if (!vme || !ready)
        return MVME_INVALID_PARAM;
    status = v1720e_read32(vme, base, REG_ACQUISITION_STATUS, &status_reg);
    if (status != MVME_SUCCESS)
        return status;
    status = v1720e_read32(vme, base, REG_EVENT_STORED, &stored);
    if (status != MVME_SUCCESS)
        return status;
    *ready = !!(status_reg & STATUS_EVENT_READY) && stored != 0;
    if (event_stored)
        *event_stored = stored;
    if (acquisition_status)
        *acquisition_status = status_reg;
    return MVME_SUCCESS;
}

int v1720e_start(MVME_INTERFACE *vme, DWORD base)
{
    DWORD control = 0, status_reg = 0, event_stored = 0;
    int status = v1720e_read32(vme, base, REG_ACQUISITION_CONTROL, &control);
    if (status != MVME_SUCCESS)
        return status;
    status = v1720e_write_verify(vme, base, REG_ACQUISITION_CONTROL,
                          control | ACQUISITION_CONTROL_RUN_REQUEST);
    if (status != MVME_SUCCESS)
        return status;
    status = v1720e_read32(vme, base, REG_ACQUISITION_STATUS, &status_reg);
    if (status != MVME_SUCCESS)
        return status;
    if (!(status_reg & ACQUISITION_STATUS_RUN_ACTIVE) ||
        (status_reg & (STATUS_PLL_OK | STATUS_BOARD_READY)) !=
            (STATUS_PLL_OK | STATUS_BOARD_READY))
        return MVME_ACCESS_ERROR;
    status = v1720e_read32(vme, base, REG_EVENT_STORED, &event_stored);
    if (status != MVME_SUCCESS)
        return status;
    /*
     * RUN start performs the documented memory reset. One new external
     * trigger may race this immediate check at 10 Hz, but older events must
     * not survive.
     */
    return event_stored <= 1 ? MVME_SUCCESS : MVME_ACCESS_ERROR;
}

int v1720e_stop(MVME_INTERFACE *vme, DWORD base)
{
    DWORD control = 0, status_reg = 0;
    int status = v1720e_read32(vme, base, REG_ACQUISITION_CONTROL, &control);
    if (status != MVME_SUCCESS)
        return status;
    status = v1720e_write_verify(vme, base, REG_ACQUISITION_CONTROL,
                          control & ~ACQUISITION_CONTROL_RUN_REQUEST);
    if (status != MVME_SUCCESS)
        return status;
    status = v1720e_read32(vme, base, REG_ACQUISITION_STATUS, &status_reg);
    if (status != MVME_SUCCESS)
        return status;
    return (status_reg & ACQUISITION_STATUS_RUN_ACTIVE) == 0
               ? MVME_SUCCESS
               : MVME_ACCESS_ERROR;
}

int v1720e_read_run_state(MVME_INTERFACE *vme, DWORD base,
                          DWORD *control, DWORD *status_reg)
{
    int status;
    if (!vme || !control || !status_reg)
        return MVME_INVALID_PARAM;
    status = v1720e_read32(vme, base, REG_ACQUISITION_CONTROL, control);
    if (status != MVME_SUCCESS)
        return status;
    return v1720e_read32(vme, base, REG_ACQUISITION_STATUS, status_reg);
}

int v1720e_stop_if_running(MVME_INTERFACE *vme, DWORD base,
                           DWORD *control_after, DWORD *status_after,
                           int *stop_attempted)
{
    int status;
    if (!control_after || !status_after || !stop_attempted)
        return MVME_INVALID_PARAM;
    *stop_attempted = 0;
    status = v1720e_read_run_state(vme, base, control_after, status_after);
    if (status != MVME_SUCCESS)
        return status;
    if (((*control_after & ACQUISITION_CONTROL_RUN_REQUEST) == 0) &&
        ((*status_after & ACQUISITION_STATUS_RUN_ACTIVE) == 0))
        return MVME_SUCCESS;

    *stop_attempted = 1;
    status = v1720e_stop(vme, base);
    if (status != MVME_SUCCESS)
        return status;
    status = v1720e_read_run_state(vme, base, control_after, status_after);
    if (status != MVME_SUCCESS)
        return status;
    return ((*control_after & ACQUISITION_CONTROL_RUN_REQUEST) == 0 &&
            (*status_after & ACQUISITION_STATUS_RUN_ACTIVE) == 0)
               ? MVME_SUCCESS
               : MVME_ACCESS_ERROR;
}

int v1720e_software_clear(MVME_INTERFACE *vme, DWORD base,
                          DWORD *event_stored_after,
                          int *event_stored_after_valid)
{
    DWORD control = 0, status_reg = 0, stored = 0;
    int status;
    if (!vme)
        return MVME_INVALID_PARAM;
    if (event_stored_after_valid)
        *event_stored_after_valid = 0;
    status = v1720e_read32(vme, base, REG_ACQUISITION_CONTROL, &control);
    if (status != MVME_SUCCESS)
        return status;
    status = v1720e_read32(vme, base, REG_ACQUISITION_STATUS, &status_reg);
    if (status != MVME_SUCCESS)
        return status;
    if ((control & ACQUISITION_CONTROL_RUN_REQUEST) ||
        (status_reg & ACQUISITION_STATUS_RUN_ACTIVE))
        return MVME_ACCESS_ERROR;

    /* CAEN V1720 SW_CLEAR (0xEF28), documented as write-only D32. This is
     * deliberately not SW_RESET (0xEF24) or CONFIG_RELOAD (0xEF34). */
    status = v1720e_write32(vme, base, REG_SOFTWARE_CLEAR, 0);
    if (status != MVME_SUCCESS)
        return status;
    status = v1720e_read32(vme, base, REG_EVENT_STORED, &stored);
    if (status != MVME_SUCCESS)
        return status;
    if (event_stored_after)
        *event_stored_after = stored;
    if (event_stored_after_valid)
        *event_stored_after_valid = 1;
    return stored == 0 ? MVME_SUCCESS : MVME_ACCESS_ERROR;
}
