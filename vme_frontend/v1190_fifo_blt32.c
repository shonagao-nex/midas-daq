#define _POSIX_C_SOURCE 200809L
#include "v1190_fifo_blt32.h"

#include <CAENVMElib.h>
#include <limits.h>
#include <string.h>
#include <time.h>

static uint64_t v1190_monotonic_ns(void)
{
    struct timespec ts = {0, 0};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t v1190_elapsed_ns(uint64_t before, uint64_t after)
{
    return before && after >= before ? after - before : 0;
}

void v1190_fifo_blt_state_reset(V1190_FIFO_BLT_STATE *state)
{
    if (state) {
        state->successful_events_mod100 = 0;
        state->successful_event_count = 0;
    }
}

V1190_FIFO_BLT_STATUS v1190_fifo_validate_event(const uint32_t *data,
    size_t words, uint16_t fifo_counter, V1190_FIFO_BLT_RESULT *result)
{
    if (!data || !result || words < 2 || words > V1190_FIFO_WORD_COUNT_MAX)
        return V1190_FIFO_BLT_INVALID_EVENT;
    const uint32_t header = data[0];
    const uint32_t trailer = data[words - 1];
    result->event_counter = (header >> 5) & 0x003fffffu;
    result->trailer_word_count = (uint16_t)((trailer >> 5) & 0xffffu);
    result->counter_consistent =
        (result->event_counter & 0xffffu) == fifo_counter;
    if ((header >> 27) != 0x08u || (trailer >> 27) != 0x10u ||
        (header & 0x1fu) != (trailer & 0x1fu) ||
        result->trailer_word_count != words ||
        result->fifo_word_count != words || !result->counter_consistent)
        return V1190_FIFO_BLT_INVALID_EVENT;

    /* The optional TDC Header/Trailer pair encloses its measurements/errors. */
    int in_tdc = 0;
    int saw_tdc_header = 0;
    int saw_time_tag = 0;
    unsigned tdc_id = 0;
    uint32_t tdc_event_id = 0;
    size_t tdc_start = 0;
    for (size_t i = 1; i + 1 < words; ++i) {
        const uint32_t word = data[i];
        const unsigned type = word >> 27;
        switch (type) {
        case 0x01: /* TDC Header */
            if (in_tdc || saw_time_tag) return V1190_FIFO_BLT_INVALID_EVENT;
            in_tdc = saw_tdc_header = 1;
            tdc_id = (word >> 24) & 3u;
            tdc_event_id = (word >> 12) & 0xfffu;
            tdc_start = i;
            break;
        case 0x00: /* Measurement */
        case 0x04: /* TDC Error */
            if (saw_time_tag || (saw_tdc_header && !in_tdc))
                return V1190_FIFO_BLT_INVALID_EVENT;
            if (type == 0x04 && in_tdc && ((word >> 24) & 3u) != tdc_id)
                return V1190_FIFO_BLT_INVALID_EVENT;
            break;
        case 0x03: /* TDC Trailer */
            if (!in_tdc || ((word >> 24) & 3u) != tdc_id ||
                ((word >> 12) & 0xfffu) != tdc_event_id ||
                (word & 0xfffu) != i - tdc_start + 1u)
                return V1190_FIFO_BLT_INVALID_EVENT;
            in_tdc = 0;
            break;
        case 0x11: /* Extended Trigger Time Tag */
            if (in_tdc || saw_time_tag) return V1190_FIFO_BLT_INVALID_EVENT;
            saw_time_tag = 1;
            break;
        default: /* Includes an early Global Trailer/Header and filler. */
            return V1190_FIFO_BLT_INVALID_EVENT;
        }
    }
    if (in_tdc) return V1190_FIFO_BLT_INVALID_EVENT;
    result->words = words;
    return V1190_FIFO_BLT_OK;
}

V1190_FIFO_BLT_STATUS v1190_fifo_read_blt32(const V1190_FIFO_BLT_IO *io,
    uint32_t base, uint32_t *data, size_t capacity_words,
    int strict_sync_check, V1190_FIFO_BLT_STATE *state,
    V1190_FIFO_BLT_RESULT *result)
{
    if (!io || !io->read16 || !io->read32 || !io->blt_read || !data ||
        !state || !result || capacity_words < 2u ||
        state->successful_events_mod100 >= V1190_FIFO_SANITY_PERIOD)
        return V1190_FIFO_BLT_BAD_ARGUMENT;
    memset(result, 0, sizeof(*result));
    uint16_t status = 0;
    uint64_t before = v1190_monotonic_ns();
    const int status_read = io->read16(io->context,
        base + V1190_FIFO_STATUS_OFFSET, &status);
    result->timing.fifo_status_ns =
        v1190_elapsed_ns(before, v1190_monotonic_ns());
    if (status_read != 0)
        return V1190_FIFO_BLT_STATUS_ERROR;
    /*
     * Fast mode retains EV FIFO DREADY as the nonempty guard. A VME register
     * read costs about 60-70 us on this setup, so it skips Stored before.
     * Strict mode retains the original status, Stored before, entry order for
     * safety checks, debugging, and source-selected fallback on later runs.
     * No failed FIFO/BLT event is retried through single D32 readout.
     */
    if (strict_sync_check) {
        before = v1190_monotonic_ns();
        const int stored_before_read = io->read16(io->context,
            base + V1190_FIFO_STORED_OFFSET, &result->stored_before);
        result->timing.stored_before_ns =
            v1190_elapsed_ns(before, v1190_monotonic_ns());
        if (stored_before_read != 0)
            return V1190_FIFO_BLT_STATUS_ERROR;
        result->timing.stored_before_checked = 1;
    }
    if (!(status & 1u) ||
        (strict_sync_check && result->stored_before == 0))
        return V1190_FIFO_BLT_EMPTY;

    /* Reading this D32 entry pops exactly one Event FIFO record. */
    uint32_t entry = 0;
    before = v1190_monotonic_ns();
    const int entry_read = io->read32(io->context,
        base + V1190_FIFO_ENTRY_OFFSET, &entry);
    result->timing.fifo_read_ns =
        v1190_elapsed_ns(before, v1190_monotonic_ns());
    if (entry_read != 0)
        return V1190_FIFO_BLT_ENTRY_ERROR;
    before = v1190_monotonic_ns();
    result->fifo_event_counter = (uint16_t)(entry >> 16);
    result->fifo_word_count = (uint16_t)(entry & 0xffffu);
    const size_t count = result->fifo_word_count;
    if (count < 2u || count > V1190_FIFO_WORD_COUNT_MAX ||
        count > capacity_words || count > V1190_BLT_APERTURE_WORDS ||
        count > (size_t)INT_MAX / 4u) {
        result->timing.decode_ns =
            v1190_elapsed_ns(before, v1190_monotonic_ns());
        return V1190_FIFO_BLT_INVALID_COUNT;
    }
    const size_t bytes = count * sizeof(uint32_t);
    if (bytes % 4u != 0) {
        result->timing.decode_ns =
            v1190_elapsed_ns(before, v1190_monotonic_ns());
        return V1190_FIFO_BLT_INVALID_COUNT;
    }
    result->requested_bytes = (int)bytes;
    result->timing.decode_ns =
        v1190_elapsed_ns(before, v1190_monotonic_ns());
    before = v1190_monotonic_ns();
    result->caen_status = io->blt_read(io->context, base, data,
        result->requested_bytes, &result->actual_bytes);
    result->timing.blt_ns = v1190_elapsed_ns(before, v1190_monotonic_ns());
    if (result->caen_status != cvSuccess)
        return V1190_FIFO_BLT_TRANSFER_ERROR;
    if (result->actual_bytes != result->requested_bytes)
        return V1190_FIFO_BLT_SHORT_TRANSFER;
    before = v1190_monotonic_ns();
    const V1190_FIFO_BLT_STATUS validation = v1190_fifo_validate_event(
        data, count, result->fifo_event_counter, result);
    result->timing.validate_ns =
        v1190_elapsed_ns(before, v1190_monotonic_ns());
    if (validation != V1190_FIFO_BLT_OK) return validation;
    /*
     * Per-event Stored after is a useful strict check, but adds another
     * measured 60-70 us/event of deadtime. In fast mode check every 100th
     * successful V1190 event instead. Single Event BUSY prevents another
     * event from entering the FIFO, so zero is expected after readout.
     * Neither zero nor the strict before-minus-one condition applies as-is
     * to MEB readout, where new triggers may arrive during the VME reads.
     */
    if (strict_sync_check ||
        state->successful_events_mod100 == V1190_FIFO_SANITY_PERIOD - 1u) {
        before = v1190_monotonic_ns();
        const int stored_after_read = io->read16(io->context,
            base + V1190_FIFO_STORED_OFFSET, &result->stored_after);
        result->timing.stored_after_ns =
            v1190_elapsed_ns(before, v1190_monotonic_ns());
        if (stored_after_read != 0)
            return V1190_FIFO_BLT_STORED_ERROR;
        result->timing.stored_after_checked = 1;
        if (strict_sync_check) {
            if ((unsigned)result->stored_after + 1u != result->stored_before)
                return V1190_FIFO_BLT_STORED_MISMATCH;
        } else if (result->stored_after != 0) {
            return V1190_FIFO_BLT_STORED_MISMATCH;
        }
    }
    state->successful_events_mod100 =
        (state->successful_events_mod100 + 1u) % V1190_FIFO_SANITY_PERIOD;
    /* This count is diagnostic only; the modulo counter controls checks. */
    if (state->successful_event_count != UINT64_MAX)
        ++state->successful_event_count;
    return V1190_FIFO_BLT_OK;
}
