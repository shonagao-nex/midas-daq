#include "v1190_fifo_blt32.h"

#include <CAENVMElib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { BASE = 0x00c10000, CAPACITY = 4096 };

typedef struct {
    uint32_t entry;
    uint32_t event[CAPACITY];
    uint16_t status;
    uint16_t stored_before;
    uint16_t stored_after;
    int fifo_reads;
    int blt_reads;
    int stored_reads;
    int status_reads;
    int fast_mode;
    char access_order[8];
    int access_count;
    int requested;
    int caen_status;
    int short_bytes;
} Mock;

static int read16(void *context, uint32_t address, uint16_t *value)
{
    Mock *m = context;
    if (address == BASE + V1190_FIFO_STATUS_OFFSET) {
        m->access_order[m->access_count++] = 'S';
        ++m->status_reads;
        *value = m->status;
    } else {
        assert(address == BASE + V1190_FIFO_STORED_OFFSET);
        m->access_order[m->access_count++] = m->fast_mode ? 'A' :
            (m->stored_reads == 0 ? 'B' : 'A');
        *value = m->fast_mode ? m->stored_after :
            (m->stored_reads == 0 ? m->stored_before : m->stored_after);
        ++m->stored_reads;
    }
    return 0;
}

static int read32(void *context, uint32_t address, uint32_t *value)
{
    Mock *m = context;
    assert(address == BASE + V1190_FIFO_ENTRY_OFFSET);
    m->access_order[m->access_count++] = 'F';
    ++m->fifo_reads;
    *value = m->entry;
    return 0;
}

static int blt_read(void *context, uint32_t address, void *destination,
                    int requested_bytes, int *actual_bytes)
{
    Mock *m = context;
    assert(address == BASE && requested_bytes > 0 && requested_bytes % 4 == 0);
    m->access_order[m->access_count++] = 'L';
    ++m->blt_reads;
    m->requested = requested_bytes;
    *actual_bytes = requested_bytes - m->short_bytes;
    if (*actual_bytes > 0)
        memcpy(destination, m->event, (size_t)*actual_bytes);
    return m->caen_status;
}

static void prepare(Mock *m)
{
    memset(m, 0, sizeof(*m));
    m->status = 1;
    m->stored_before = 1;
    m->stored_after = 0;
    m->entry = (0x3456u << 16) | 10u;
    m->event[0] = 0x4000001fu | (0x123456u << 5);
    for (unsigned id = 0; id < 4; ++id) {
        m->event[1 + 2 * id] = 0x08000000u | (id << 24) | (0x123u << 12);
        m->event[2 + 2 * id] = 0x18000002u | (id << 24) | (0x123u << 12);
    }
    m->event[9] = 0x8000015fu;
}

static V1190_FIFO_BLT_STATUS run(Mock *m, size_t capacity,
    uint32_t *data, V1190_FIFO_BLT_RESULT *result)
{
    V1190_FIFO_BLT_STATE state;
    v1190_fifo_blt_state_reset(&state);
    const V1190_FIFO_BLT_IO io = {read16, read32, blt_read, m};
    return v1190_fifo_read_blt32(&io, BASE, data, capacity, 1, &state, result);
}

static V1190_FIFO_BLT_STATUS run_fast(Mock *m, size_t capacity,
    uint32_t *data, V1190_FIFO_BLT_STATE *state,
    V1190_FIFO_BLT_RESULT *result)
{
    m->fast_mode = 1;
    const V1190_FIFO_BLT_IO io = {read16, read32, blt_read, m};
    return v1190_fifo_read_blt32(&io, BASE, data, capacity, 0, state, result);
}

int main(void)
{
    Mock m;
    uint32_t data[CAPACITY] = {0};
    V1190_FIFO_BLT_RESULT result;

    prepare(&m);
    assert(run(&m, CAPACITY, data, &result) == V1190_FIFO_BLT_OK);
    assert(result.words == 10 && result.fifo_word_count == 10 &&
           result.fifo_event_counter == 0x3456 &&
           result.event_counter == 0x123456 && result.counter_consistent &&
           result.trailer_word_count == 10 && result.requested_bytes == 40 &&
           result.actual_bytes == 40 && m.requested == 40 &&
           m.fifo_reads == 1 && m.blt_reads == 1 && m.stored_reads == 2 &&
           memcmp(data, m.event, 40) == 0);
    assert(strcmp(m.access_order, "SBFLA") == 0);
    assert(result.timing.stored_before_checked &&
           result.timing.stored_after_checked);

    prepare(&m);
    m.status = 0;
    assert(run(&m, CAPACITY, data, &result) == V1190_FIFO_BLT_EMPTY);
    assert(m.fifo_reads == 0 && m.blt_reads == 0);

    prepare(&m);
    m.entry &= 0xffff0000u;
    assert(run(&m, CAPACITY, data, &result) == V1190_FIFO_BLT_INVALID_COUNT);
    assert(m.fifo_reads == 1 && m.blt_reads == 0);

    prepare(&m);
    m.entry = (m.entry & 0xffff0000u) | (V1190_BLT_APERTURE_WORDS + 1u);
    assert(run(&m, CAPACITY, data, &result) == V1190_FIFO_BLT_INVALID_COUNT);
    assert(m.blt_reads == 0);

    prepare(&m);
    m.entry = (m.entry & 0xffff0000u) | (CAPACITY + 1u);
    assert(run(&m, CAPACITY, data, &result) == V1190_FIFO_BLT_INVALID_COUNT);
    assert(m.fifo_reads == 1 && m.blt_reads == 0);

    prepare(&m);
    assert(run(&m, 9, data, &result) == V1190_FIFO_BLT_INVALID_COUNT);
    assert(m.blt_reads == 0);

    prepare(&m);
    m.short_bytes = 4;
    assert(run(&m, CAPACITY, data, &result) == V1190_FIFO_BLT_SHORT_TRANSFER);
    assert(m.fifo_reads == 1 && m.blt_reads == 1 && m.stored_reads == 1);

    prepare(&m);
    m.caen_status = cvBusError;
    assert(run(&m, CAPACITY, data, &result) == V1190_FIFO_BLT_TRANSFER_ERROR);
    assert(result.caen_status == cvBusError && m.fifo_reads == 1 &&
           m.blt_reads == 1 && m.stored_reads == 1);

    prepare(&m);
    m.event[9] += 1u << 5;
    assert(run(&m, CAPACITY, data, &result) == V1190_FIFO_BLT_INVALID_EVENT);
    assert(result.trailer_word_count == 11);

    prepare(&m);
    m.entry += 1u << 16;
    assert(run(&m, CAPACITY, data, &result) == V1190_FIFO_BLT_INVALID_EVENT);
    assert(!result.counter_consistent);

    prepare(&m);
    m.event[1] = 0x00000000u;
    assert(run(&m, CAPACITY, data, &result) == V1190_FIFO_BLT_INVALID_EVENT);

    prepare(&m);
    m.event[9] = 0x4000015fu;
    assert(run(&m, CAPACITY, data, &result) == V1190_FIFO_BLT_INVALID_EVENT);

    prepare(&m);
    m.event[2] = 0x19000002u | (0x123u << 12);
    assert(run(&m, CAPACITY, data, &result) == V1190_FIFO_BLT_INVALID_EVENT);

    /* An empty valid event (Global Header + Trailer) has two words. */
    prepare(&m);
    m.entry = (0x3456u << 16) | 2u;
    m.event[1] = 0x8000005fu;
    assert(run(&m, CAPACITY, data, &result) == V1190_FIFO_BLT_OK);
    assert(m.requested == 8 && result.words == 2);

    /* TDC measurement/error and optional global time tag are preserved. */
    prepare(&m);
    m.entry = (0x3456u << 16) | 7u;
    m.event[1] = 0x08000000u | (0x123u << 12);
    m.event[2] = 0x00080042u;
    m.event[3] = 0x20000001u;
    m.event[4] = 0x18000004u | (0x123u << 12);
    m.event[5] = 0x88001234u;
    m.event[6] = 0x800000ffu;
    assert(run(&m, CAPACITY, data, &result) == V1190_FIFO_BLT_OK);
    assert(result.words == 7 && memcmp(data, m.event, 28) == 0);

    prepare(&m);
    m.event[2] = 0x18000003u | (0x123u << 12);
    assert(run(&m, CAPACITY, data, &result) == V1190_FIFO_BLT_INVALID_EVENT);

    prepare(&m);
    m.stored_after = 1;
    assert(run(&m, CAPACITY, data, &result) ==
           V1190_FIFO_BLT_STORED_MISMATCH);
    assert(m.fifo_reads == 1 && m.blt_reads == 1);

    V1190_FIFO_BLT_STATE fast_state;
    v1190_fifo_blt_state_reset(&fast_state); /* BOR reset API. */
    assert(fast_state.successful_events_mod100 == 0 &&
           fast_state.successful_event_count == 0);
    for (unsigned event = 1; event <= 99; ++event) {
        prepare(&m);
        assert(run_fast(&m, CAPACITY, data, &fast_state, &result) ==
               V1190_FIFO_BLT_OK);
        assert(strcmp(m.access_order, "SFL") == 0);
        assert(m.status_reads == 1 && m.stored_reads == 0);
        assert(!result.timing.stored_before_checked &&
               !result.timing.stored_after_checked);
        assert(fast_state.successful_events_mod100 == event);
        assert(fast_state.successful_event_count == event);
    }
    prepare(&m);
    assert(run_fast(&m, CAPACITY, data, &fast_state, &result) ==
           V1190_FIFO_BLT_OK);
    assert(strcmp(m.access_order, "SFLA") == 0);
    assert(m.stored_reads == 1 && result.timing.stored_after_checked &&
           result.stored_after == 0);
    assert(fast_state.successful_events_mod100 == 0);
    assert(fast_state.successful_event_count == 100);
    for (unsigned event = 1; event <= 100; ++event) {
        prepare(&m);
        assert(run_fast(&m, CAPACITY, data, &fast_state, &result) ==
               V1190_FIFO_BLT_OK);
        assert(m.stored_reads == (event == 100 ? 1 : 0));
    }
    assert(fast_state.successful_events_mod100 == 0);
    assert(fast_state.successful_event_count == 200);

    prepare(&m);
    m.status = 0;
    assert(run_fast(&m, CAPACITY, data, &fast_state, &result) ==
           V1190_FIFO_BLT_EMPTY);
    assert(strcmp(m.access_order, "S") == 0 &&
           fast_state.successful_events_mod100 == 0 &&
           fast_state.successful_event_count == 200);

    prepare(&m);
    m.short_bytes = 4;
    assert(run_fast(&m, CAPACITY, data, &fast_state, &result) ==
           V1190_FIFO_BLT_SHORT_TRANSFER);
    assert(strcmp(m.access_order, "SFL") == 0 && m.fifo_reads == 1 &&
           m.blt_reads == 1 && fast_state.successful_events_mod100 == 0 &&
           fast_state.successful_event_count == 200);

    prepare(&m);
    m.caen_status = cvBusError;
    assert(run_fast(&m, CAPACITY, data, &fast_state, &result) ==
           V1190_FIFO_BLT_TRANSFER_ERROR);
    assert(strcmp(m.access_order, "SFL") == 0 && m.fifo_reads == 1 &&
           m.blt_reads == 1 && fast_state.successful_events_mod100 == 0 &&
           fast_state.successful_event_count == 200);

    prepare(&m);
    m.event[9] += 1u << 5;
    assert(run_fast(&m, CAPACITY, data, &fast_state, &result) ==
           V1190_FIFO_BLT_INVALID_EVENT);
    assert(result.trailer_word_count == 11 &&
           fast_state.successful_events_mod100 == 0);
    prepare(&m);
    m.entry += 1u << 16;
    assert(run_fast(&m, CAPACITY, data, &fast_state, &result) ==
           V1190_FIFO_BLT_INVALID_EVENT);
    assert(!result.counter_consistent &&
           fast_state.successful_events_mod100 == 0);
    prepare(&m);
    m.event[9] = 0x4000015fu; /* Same malformed structure in both modes. */
    assert(run_fast(&m, CAPACITY, data, &fast_state, &result) ==
           V1190_FIFO_BLT_INVALID_EVENT);
    assert(fast_state.successful_events_mod100 == 0);

    fast_state.successful_events_mod100 = 99;
    fast_state.successful_event_count = 299; /* Next success is event 300. */
    prepare(&m);
    m.stored_after = 1;
    assert(run_fast(&m, CAPACITY, data, &fast_state, &result) ==
           V1190_FIFO_BLT_STORED_MISMATCH);
    assert(strcmp(m.access_order, "SFLA") == 0 &&
           fast_state.successful_events_mod100 == 99 &&
           fast_state.successful_event_count == 299);
    prepare(&m);
    assert(run_fast(&m, CAPACITY, data, &fast_state, &result) ==
           V1190_FIFO_BLT_OK);
    assert(strcmp(m.access_order, "SFLA") == 0 &&
           fast_state.successful_events_mod100 == 0 &&
           fast_state.successful_event_count == 300);

    fast_state.successful_events_mod100 = 45;
    v1190_fifo_blt_state_reset(&fast_state);
    assert(fast_state.successful_events_mod100 == 0 &&
           fast_state.successful_event_count == 0);

    puts("test_v1190_fifo_blt32: passed");
    return 0;
}
