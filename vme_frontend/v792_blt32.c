#include "v792_blt32.h"

#include <CAENVMElib.h>
#include <string.h>

V792_BLT_STATUS v792_validate_raw_event(const uint32_t *data, size_t words,
                                          V792_BLT_RESULT *result)
{
    if (!data || !result || words < 2 || words > V792_EVENT_MAX_WORDS)
        return V792_BLT_INVALID_EVENT;
    const uint32_t header = data[0];
    const unsigned count = (header >> 8) & 0x3fu;
    const unsigned geo = header >> 27;
    if (((header >> 24) & 7u) != 2u || count > V792_EVENT_MAX_CHANNELS ||
        words != (size_t)count + 2u)
        return V792_BLT_INVALID_EVENT;
    uint32_t channels_seen = 0;
    for (unsigned i = 0; i < count; ++i) {
        const uint32_t word = data[i + 1u];
        const unsigned channel = (word >> 16) & 0x1fu;
        if ((word >> 27) != geo || ((word >> 24) & 7u) != 0u ||
            channel >= V792_EVENT_MAX_CHANNELS ||
            (channels_seen & (1u << channel)) != 0u)
            return V792_BLT_INVALID_EVENT;
        channels_seen |= 1u << channel;
    }
    const uint32_t eob = data[words - 1u];
    if ((eob >> 27) != geo || ((eob >> 24) & 7u) != 4u)
        return V792_BLT_INVALID_EVENT;
    result->words = words;
    result->measurements = count;
    result->geo = geo;
    result->event_counter = eob & 0x00ffffffu;
    return V792_BLT_OK;
}

V792_BLT_STATUS v792_read_blt32(const V792_BLT_IO *io, uint32_t base,
                                  uint32_t *data, size_t capacity_words,
                                  V792_BLT_RESULT *result)
{
    if (!io || !io->read_word || !io->blt_read || !data || !result ||
        capacity_words < 2u)
        return V792_BLT_BAD_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (io->read_word(io->context, base, &data[0]) != 0)
        return V792_BLT_HEADER_READ_ERROR;
    const unsigned count = (data[0] >> 8) & 0x3fu;
    const size_t total_words = (size_t)count + 2u;
    if (((data[0] >> 24) & 7u) != 2u ||
        count > V792_EVENT_MAX_CHANNELS ||
        total_words > V792_EVENT_MAX_WORDS || total_words > capacity_words)
        return V792_BLT_INVALID_HEADER;
    const size_t remaining_bytes = ((size_t)count + 1u) * sizeof(uint32_t);
    if (remaining_bytes == 0 || remaining_bytes % 4u != 0 ||
        remaining_bytes > (capacity_words - 1u) * sizeof(uint32_t))
        return V792_BLT_INVALID_HEADER;
    result->requested_bytes = (int)remaining_bytes;
    result->caen_status = io->blt_read(io->context, base, data + 1u,
                                        result->requested_bytes,
                                        &result->actual_bytes);
    if (result->caen_status != cvSuccess)
        return V792_BLT_TRANSFER_ERROR;
    if (result->actual_bytes != result->requested_bytes)
        return V792_BLT_SHORT_TRANSFER;
    return v792_validate_raw_event(data, total_words, result);
}
