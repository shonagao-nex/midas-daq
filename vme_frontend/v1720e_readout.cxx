#define _POSIX_C_SOURCE 200809L
#include <cstring>
#include <ctime>

#include "v1720e_readout.h"
#include "v1720e_internal.h"
#include "caenvme.h"

extern "C" {

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
    status = v1720e_access_begin(vme, &saved_am, &saved_mode);
    if (status != MVME_SUCCESS)
        return status;
    for (i = 0; i < 4; ++i) {
        const uint64_t before = mode == BLT32 ? readout_monotonic_ns() : 0;
        status = mvme_read(vme, &data[i], base, sizeof(data[i]));
        if (mode == BLT32)
            info->timing.header_read_ns[i] =
                readout_elapsed_ns(before, readout_monotonic_ns());
        if (status != MVME_SUCCESS)
            return v1720e_access_end(vme, saved_am, saved_mode, status);
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
        return v1720e_access_end(vme, saved_am, saved_mode, MVME_ACCESS_ERROR);
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
                return v1720e_access_end(vme, saved_am, saved_mode, MVME_ACCESS_ERROR);
            }
            info->words = size;
            info->timing.blt_validation_ns = readout_elapsed_ns(
                validation_before, readout_monotonic_ns());
            return v1720e_access_end(vme, saved_am, saved_mode, MVME_SUCCESS);
        }
    }
    for (i = 4; i < size; ++i) {
        status = mvme_read(vme, &data[i], base, sizeof(data[i]));
        if (status != MVME_SUCCESS) {
            info->words = i;
            return v1720e_access_end(vme, saved_am, saved_mode, status);
        }
    }
    info->words = size;
    return v1720e_access_end(vme, saved_am, saved_mode, MVME_SUCCESS);
}

int v1720e_read_event(MVME_INTERFACE *vme, DWORD base, DWORD *data,
                      size_t capacity, DWORD expected_event_words,
                      DWORD expected_channel_mask, V1720E_EVENT_INFO *info)
{
    return v1720e_read_event_mode(vme, base, data, capacity,
                                  expected_event_words, expected_channel_mask,
                                  SINGLE_D32, info);
}

}
