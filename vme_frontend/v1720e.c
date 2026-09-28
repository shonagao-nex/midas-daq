#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <time.h>
#include "v1720e.h"
#include "caenvme.h"

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
#define REG_SOFTWARE_CLEAR       0xEF28u
#define REG_DC_OFFSET(ch)        (0x1098u + ((DWORD)(ch) << 8))

#define ACQUISITION_CONTROL_RUN_REQUEST  0x00000004u
#define ACQUISITION_STATUS_RUN_ACTIVE    0x00000004u
#define STATUS_EVENT_READY       0x00000008u
#define STATUS_EXTERNAL_CLOCK    0x00000020u
#define STATUS_PLL_OK            0x00000080u
#define STATUS_BOARD_READY       0x00000100u

#define CHANNEL_CONFIG_ZS_MASK   0x000F0000u
#define CHANNEL_CONFIG_PACK25    0x00000800u

static uint64_t readout_monotonic_ns(void)
{
    struct timespec ts = {0, 0};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t readout_elapsed_ns(uint64_t before, uint64_t after)
{
    return before && after >= before ? after - before : 0;
}

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

int v1720e_configure(MVME_INTERFACE *vme, DWORD base,
                     const V1720E_CONFIG *config)
{
    DWORD control = 0, status_reg = 0, channel_config = 0;
    unsigned channel;
    int status;
    if (!vme || !config)
        return MVME_INVALID_PARAM;
    status = read32(vme, base, REG_ACQUISITION_CONTROL, &control);
    if (status != MVME_SUCCESS)
        return status;
    status = read32(vme, base, REG_ACQUISITION_STATUS, &status_reg);
    if (status != MVME_SUCCESS)
        return status;
    if ((control & ACQUISITION_CONTROL_RUN_REQUEST) ||
        (status_reg & ACQUISITION_STATUS_RUN_ACTIVE) ||
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
                          config->buffer_organization);
    if (status != MVME_SUCCESS)
        return status;
    status = write_verify(vme, base, REG_CUSTOM_SIZE, config->custom_size);
    if (status != MVME_SUCCESS)
        return status;
    status = write_verify(vme, base, REG_TRIGGER_SOURCE,
                          config->trigger_source);
    if (status != MVME_SUCCESS)
        return status;
    status = write_verify(vme, base, REG_POST_TRIGGER, config->post_trigger);
    if (status != MVME_SUCCESS)
        return status;
    status = write_verify(vme, base, REG_CHANNEL_ENABLE,
                          config->channel_enable);
    if (status != MVME_SUCCESS)
        return status;
    for (channel = 0; channel < V1720E_CHANNEL_COUNT; ++channel) {
        status = write_verify(vme, base, REG_DC_OFFSET(channel),
                              config->dc_offset[channel]);
        if (status != MVME_SUCCESS)
            return status;
    }
    return MVME_SUCCESS;
}

int v1720e_read_configuration(MVME_INTERFACE *vme, DWORD base,
                              V1720E_CONFIG_READBACK *readback)
{
    DWORD dc_offset;
    unsigned channel;
    int status;
    if (!vme || !readback)
        return MVME_INVALID_PARAM;
    memset(readback, 0, sizeof(*readback));
#define READ_CONFIG(member, reg) \
    do { \
        status = read32(vme, base, reg, &readback->member); \
        if (status != MVME_SUCCESS) return status; \
    } while (0)
    READ_CONFIG(board_info, REG_BOARD_INFO);
    READ_CONFIG(roc_firmware, REG_ROC_FIRMWARE);
    READ_CONFIG(buffer_organization, REG_BUFFER_ORGANIZATION);
    READ_CONFIG(custom_size, REG_CUSTOM_SIZE);
    READ_CONFIG(post_trigger, REG_POST_TRIGGER);
    READ_CONFIG(trigger_source, REG_TRIGGER_SOURCE);
    READ_CONFIG(channel_enable, REG_CHANNEL_ENABLE);
    READ_CONFIG(channel_config, REG_CHANNEL_CONFIG);
#undef READ_CONFIG
    for (channel = 0; channel < V1720E_CHANNEL_COUNT; ++channel) {
        status = read32(vme, base, REG_DC_OFFSET(channel), &dc_offset);
        if (status != MVME_SUCCESS)
            return status;
        readback->dc_offset[channel] = (WORD)(dc_offset & 0xFFFFu);
    }
    return MVME_SUCCESS;
}

int v1720e_data_ready(MVME_INTERFACE *vme, DWORD base, int *ready,
                      DWORD *event_stored, DWORD *acquisition_status)
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
    if (acquisition_status)
        *acquisition_status = status_reg;
    return MVME_SUCCESS;
}

int v1720e_read_event_mode(MVME_INTERFACE *vme, DWORD base, DWORD *data,
                           size_t capacity, DWORD expected_event_words,
                           DWORD expected_channel_mask,
                           V1720E_READOUT_MODE mode, V1720E_EVENT_INFO *info)
{
    DWORD size;
    size_t i;
    int saved_am, saved_mode, status;
    if (!vme || !data || !info || capacity < 4 ||
        (mode != SINGLE_D32 && mode != BLT32))
        return MVME_INVALID_PARAM;
    memset(info, 0, sizeof(*info));
    status = access_begin(vme, &saved_am, &saved_mode);
    if (status != MVME_SUCCESS)
        return status;
    for (i = 0; i < 4; ++i) {
        const uint64_t before = mode == BLT32 ? readout_monotonic_ns() : 0;
        status = mvme_read(vme, &data[i], base, sizeof(data[i]));
        if (mode == BLT32)
            info->timing.header_read_ns[i] =
                readout_elapsed_ns(before, readout_monotonic_ns());
        if (status != MVME_SUCCESS)
            return access_end(vme, saved_am, saved_mode, status);
    }
    const uint64_t decode_before = mode == BLT32 ? readout_monotonic_ns() : 0;
    size = data[0] & 0x0FFFFFFFu;
    info->event_size = size;
    info->channel_mask = data[1] & 0xFFu;
    info->event_counter = data[2] & 0x00FFFFFFu;
    info->trigger_time_tag = data[3] & 0x7FFFFFFFu;
    info->header_valid = (data[0] >> 28) == 0xAu;
    info->size_valid = size == expected_event_words;
    info->channel_mask_valid =
        info->channel_mask == (expected_channel_mask & 0xFFu);
    if (!info->header_valid || size < 4 || size > capacity) {
        info->words = 4;
        if (mode == BLT32)
            info->timing.header_decode_ns =
                readout_elapsed_ns(decode_before, readout_monotonic_ns());
        return access_end(vme, saved_am, saved_mode, MVME_ACCESS_ERROR);
    }
    if (mode == BLT32)
        info->timing.header_decode_ns =
            readout_elapsed_ns(decode_before, readout_monotonic_ns());
    if (mode == BLT32 && size > 4) {
        size_t remaining = (size_t)size - 4;
        /* Decide before the first block cycle; never replay a consumed FIFO. */
        if (remaining <= V1720E_BLT32_MAX_BYTES / sizeof(DWORD)) {
            int count = 0;
            int requested_bytes = (int)(remaining * sizeof(DWORD));
            const uint64_t blt_before = readout_monotonic_ns();
            int caen_status = caenvme_blt_read32(vme->handle, base, &data[4],
                                                 requested_bytes, &count);
            info->timing.blt_ns =
                readout_elapsed_ns(blt_before, readout_monotonic_ns());
            const uint64_t validation_before = readout_monotonic_ns();
            info->blt_status = caen_status;
            info->blt_requested_bytes = requested_bytes;
            info->blt_actual_bytes = count;
            if (caen_status != 0 || count != requested_bytes || count < 0 ||
                (count % sizeof(DWORD)) != 0) {
                info->words = count > 0 && count <= requested_bytes &&
                              (count % sizeof(DWORD)) == 0
                                  ? 4 + (size_t)count / sizeof(DWORD) : 4;
                info->timing.blt_validation_ns = readout_elapsed_ns(
                    validation_before, readout_monotonic_ns());
                return access_end(vme, saved_am, saved_mode, MVME_ACCESS_ERROR);
            }
            info->words = size;
            info->timing.blt_validation_ns = readout_elapsed_ns(
                validation_before, readout_monotonic_ns());
            return access_end(vme, saved_am, saved_mode, MVME_SUCCESS);
        }
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

int v1720e_read_event(MVME_INTERFACE *vme, DWORD base, DWORD *data,
                      size_t capacity, DWORD expected_event_words,
                      DWORD expected_channel_mask, V1720E_EVENT_INFO *info)
{
    return v1720e_read_event_mode(vme, base, data, capacity,
                                  expected_event_words, expected_channel_mask,
                                  SINGLE_D32, info);
}

int v1720e_start(MVME_INTERFACE *vme, DWORD base)
{
    DWORD control = 0, status_reg = 0, event_stored = 0;
    int status = read32(vme, base, REG_ACQUISITION_CONTROL, &control);
    if (status != MVME_SUCCESS)
        return status;
    status = write_verify(vme, base, REG_ACQUISITION_CONTROL,
                          control | ACQUISITION_CONTROL_RUN_REQUEST);
    if (status != MVME_SUCCESS)
        return status;
    status = read32(vme, base, REG_ACQUISITION_STATUS, &status_reg);
    if (status != MVME_SUCCESS)
        return status;
    if (!(status_reg & ACQUISITION_STATUS_RUN_ACTIVE) ||
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
                          control & ~ACQUISITION_CONTROL_RUN_REQUEST);
    if (status != MVME_SUCCESS)
        return status;
    status = read32(vme, base, REG_ACQUISITION_STATUS, &status_reg);
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
    status = read32(vme, base, REG_ACQUISITION_CONTROL, control);
    if (status != MVME_SUCCESS)
        return status;
    return read32(vme, base, REG_ACQUISITION_STATUS, status_reg);
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
    status = read32(vme, base, REG_ACQUISITION_CONTROL, &control);
    if (status != MVME_SUCCESS)
        return status;
    status = read32(vme, base, REG_ACQUISITION_STATUS, &status_reg);
    if (status != MVME_SUCCESS)
        return status;
    if ((control & ACQUISITION_CONTROL_RUN_REQUEST) ||
        (status_reg & ACQUISITION_STATUS_RUN_ACTIVE))
        return MVME_ACCESS_ERROR;

    /* CAEN V1720 SW_CLEAR (0xEF28), documented as write-only D32. This is
     * deliberately not SW_RESET (0xEF24) or CONFIG_RELOAD (0xEF34). */
    status = write32(vme, base, REG_SOFTWARE_CLEAR, 0);
    if (status != MVME_SUCCESS)
        return status;
    status = read32(vme, base, REG_EVENT_STORED, &stored);
    if (status != MVME_SUCCESS)
        return status;
    if (event_stored_after)
        *event_stored_after = stored;
    if (event_stored_after_valid)
        *event_stored_after_valid = 1;
    return stored == 0 ? MVME_SUCCESS : MVME_ACCESS_ERROR;
}
