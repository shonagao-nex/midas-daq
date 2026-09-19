#include <string.h>
#include "v1720e.h"

#define REG_CHANNEL_CONFIG       0x8000u
#define REG_BUFFER_ORGANIZATION  0x800Cu
#define REG_CUSTOM_SIZE          0x8020u
#define REG_ACQUISITION_CONTROL  0x8100u
#define REG_ACQUISITION_STATUS   0x8104u
#define REG_TRIGGER_SOURCE       0x810Cu
#define REG_POST_TRIGGER         0x8114u
#define REG_CHANNEL_ENABLE       0x8120u
#define REG_ROC_FIRMWARE         0x8124u
#define REG_EVENT_STORED         0x812Cu
#define REG_BOARD_INFO           0x8140u
#define REG_DC_OFFSET(ch)        (0x1098u + ((DWORD)(ch) << 8))

#define ACQ_RUN                  0x00000004u
#define STATUS_EVENT_READY       0x00000008u
#define STATUS_EXTERNAL_CLOCK    0x00000020u
#define STATUS_PLL_OK            0x00000080u
#define STATUS_BOARD_READY       0x00000100u

#define CHANNEL_CONFIG_ZS_MASK   0x000F0000u
#define CHANNEL_CONFIG_PACK25    0x00000800u

#define CONFIG_BUFFER_ORG        0x0000000Au
#define CONFIG_CUSTOM_SIZE       0x00000040u
#define CONFIG_TRIGGER_SOURCE    0xC0000000u
#define CONFIG_POST_TRIGGER      0x00000030u
#define CONFIG_CHANNEL_ENABLE    0x000000FFu
#define CONFIG_DC_OFFSET         0x00008000u

static int access_begin(MVME_INTERFACE *vme, int *saved_am, int *saved_mode)
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

static int access_end(MVME_INTERFACE *vme, int saved_am, int saved_mode,
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

static int read32(MVME_INTERFACE *vme, DWORD base, DWORD offset, DWORD *value)
{
    int saved_am, saved_mode;
    int status = access_begin(vme, &saved_am, &saved_mode);
    if (status != MVME_SUCCESS)
        return status;
    status = mvme_read(vme, value, base + offset, sizeof(*value));
    return access_end(vme, saved_am, saved_mode, status);
}

static int write32(MVME_INTERFACE *vme, DWORD base, DWORD offset, DWORD value)
{
    int saved_am, saved_mode;
    int status = access_begin(vme, &saved_am, &saved_mode);
    if (status != MVME_SUCCESS)
        return status;
    status = mvme_write(vme, base + offset, &value, sizeof(value));
    return access_end(vme, saved_am, saved_mode, status);
}

static int write_verify(MVME_INTERFACE *vme, DWORD base, DWORD offset,
                        DWORD value)
{
    DWORD readback = 0;
    int status = write32(vme, base, offset, value);
    if (status != MVME_SUCCESS)
        return status;
    status = read32(vme, base, offset, &readback);
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
        status = read32(vme, base, reg, &info->member); \
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

int v1720e_configure(MVME_INTERFACE *vme, DWORD base)
{
    DWORD control = 0, status_reg = 0, channel_config = 0;
    unsigned channel;
    int status;
    if (!vme)
        return MVME_INVALID_PARAM;
    status = read32(vme, base, REG_ACQUISITION_CONTROL, &control);
    if (status != MVME_SUCCESS)
        return status;
    status = read32(vme, base, REG_ACQUISITION_STATUS, &status_reg);
    if (status != MVME_SUCCESS)
        return status;
    if ((control & ACQ_RUN) || (status_reg & ACQ_RUN) ||
        (status_reg & STATUS_EXTERNAL_CLOCK) ||
        (status_reg & (STATUS_PLL_OK | STATUS_BOARD_READY)) !=
            (STATUS_PLL_OK | STATUS_BOARD_READY))
        return MVME_ACCESS_ERROR;
    status = read32(vme, base, REG_CHANNEL_CONFIG, &channel_config);
    if (status != MVME_SUCCESS)
        return status;
    channel_config &= ~(CHANNEL_CONFIG_ZS_MASK | CHANNEL_CONFIG_PACK25);
    status = write_verify(vme, base, REG_CHANNEL_CONFIG, channel_config);
    if (status != MVME_SUCCESS)
        return status;

    /*
     * This exact configuration is verified on ROC FPGA firmware 4.5.
     * In particular, Custom Size 0x40 produces 256 samples/channel.
     * MEB cleanup is verified after RUN start, whose documented memory
     * reset avoids adding a separate Software Clear.
     */
    status = write_verify(vme, base, REG_BUFFER_ORGANIZATION,
                          CONFIG_BUFFER_ORG);
    if (status != MVME_SUCCESS)
        return status;
    status = write_verify(vme, base, REG_CUSTOM_SIZE, CONFIG_CUSTOM_SIZE);
    if (status != MVME_SUCCESS)
        return status;
    status = write_verify(vme, base, REG_TRIGGER_SOURCE,
                          CONFIG_TRIGGER_SOURCE);
    if (status != MVME_SUCCESS)
        return status;
    status = write_verify(vme, base, REG_POST_TRIGGER, CONFIG_POST_TRIGGER);
    if (status != MVME_SUCCESS)
        return status;
    status = write_verify(vme, base, REG_CHANNEL_ENABLE,
                          CONFIG_CHANNEL_ENABLE);
    if (status != MVME_SUCCESS)
        return status;
    for (channel = 0; channel < 8; ++channel) {
        status = write_verify(vme, base, REG_DC_OFFSET(channel),
                              CONFIG_DC_OFFSET);
        if (status != MVME_SUCCESS)
            return status;
    }
    return MVME_SUCCESS;
}

int v1720e_data_ready(MVME_INTERFACE *vme, DWORD base, int *ready,
                      DWORD *event_stored)
{
    DWORD status_reg = 0, stored = 0;
    int status;
    if (!vme || !ready)
        return MVME_INVALID_PARAM;
    status = read32(vme, base, REG_ACQUISITION_STATUS, &status_reg);
    if (status != MVME_SUCCESS)
        return status;
    status = read32(vme, base, REG_EVENT_STORED, &stored);
    if (status != MVME_SUCCESS)
        return status;
    *ready = !!(status_reg & STATUS_EVENT_READY) && stored != 0;
    if (event_stored)
        *event_stored = stored;
    return MVME_SUCCESS;
}

int v1720e_read_event(MVME_INTERFACE *vme, DWORD base, DWORD *data,
                      size_t capacity, V1720E_EVENT_INFO *info)
{
    DWORD size;
    size_t i;
    int saved_am, saved_mode, status;
    if (!vme || !data || !info || capacity < 4)
        return MVME_INVALID_PARAM;
    memset(info, 0, sizeof(*info));
    status = access_begin(vme, &saved_am, &saved_mode);
    if (status != MVME_SUCCESS)
        return status;
    for (i = 0; i < 4; ++i) {
        status = mvme_read(vme, &data[i], base, sizeof(data[i]));
        if (status != MVME_SUCCESS)
            return access_end(vme, saved_am, saved_mode, status);
    }
    size = data[0] & 0x0FFFFFFFu;
    info->event_size = size;
    info->channel_mask = data[1] & 0xFFu;
    info->event_counter = data[2] & 0x00FFFFFFu;
    info->trigger_time_tag = data[3] & 0x7FFFFFFFu;
    info->header_valid = (data[0] >> 28) == 0xAu;
    info->size_valid = size == V1720E_EXPECTED_EVENT_WORDS;
    info->channel_mask_valid = info->channel_mask == 0xFFu;
    if (!info->header_valid || size < 4 || size > capacity) {
        info->words = 4;
        return access_end(vme, saved_am, saved_mode, MVME_ACCESS_ERROR);
    }
    for (i = 4; i < size; ++i) {
        status = mvme_read(vme, &data[i], base, sizeof(data[i]));
        if (status != MVME_SUCCESS) {
            info->words = i;
            return access_end(vme, saved_am, saved_mode, status);
        }
    }
    info->words = size;
    return access_end(vme, saved_am, saved_mode, MVME_SUCCESS);
}

int v1720e_start(MVME_INTERFACE *vme, DWORD base)
{
    DWORD control = 0, status_reg = 0, event_stored = 0;
    int status = read32(vme, base, REG_ACQUISITION_CONTROL, &control);
    if (status != MVME_SUCCESS)
        return status;
    status = write_verify(vme, base, REG_ACQUISITION_CONTROL,
                          control | ACQ_RUN);
    if (status != MVME_SUCCESS)
        return status;
    status = read32(vme, base, REG_ACQUISITION_STATUS, &status_reg);
    if (status != MVME_SUCCESS)
        return status;
    if (!(status_reg & ACQ_RUN) ||
        (status_reg & (STATUS_PLL_OK | STATUS_BOARD_READY)) !=
            (STATUS_PLL_OK | STATUS_BOARD_READY))
        return MVME_ACCESS_ERROR;
    status = read32(vme, base, REG_EVENT_STORED, &event_stored);
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
    int status = read32(vme, base, REG_ACQUISITION_CONTROL, &control);
    if (status != MVME_SUCCESS)
        return status;
    status = write_verify(vme, base, REG_ACQUISITION_CONTROL,
                          control & ~ACQ_RUN);
    if (status != MVME_SUCCESS)
        return status;
    status = read32(vme, base, REG_ACQUISITION_STATUS, &status_reg);
    if (status != MVME_SUCCESS)
        return status;
    return (status_reg & ACQ_RUN) == 0 ? MVME_SUCCESS : MVME_ACCESS_ERROR;
}
