#include "v792_blt32.h"

#include <CAENVMElib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { BASE = 0x00600000 };

typedef struct {
    uint32_t header;
    uint32_t remainder[33];
    int header_reads;
    int blt_reads;
    int requested;
    int caen_status;
    int short_bytes;
    int header_failure;
} Mock;

static int read_header(void *context, uint32_t address, uint32_t *word)
{
    Mock *m = context;
    assert(address == BASE);
    ++m->header_reads;
    if (m->header_failure) return -1;
    *word = m->header;
    return 0;
}

static int read_block(void *context, uint32_t address, void *destination,
                      int requested_bytes, int *actual_bytes)
{
    Mock *m = context;
    assert(address == BASE);
    assert(requested_bytes > 0 && requested_bytes % 4 == 0);
    ++m->blt_reads;
    m->requested = requested_bytes;
    *actual_bytes = requested_bytes - m->short_bytes;
    memcpy(destination, m->remainder, (size_t)*actual_bytes);
    return m->caen_status;
}

static void prepare(Mock *m, unsigned count)
{
    memset(m, 0, sizeof(*m));
    m->header = 0xfa000000u | (count << 8);
    for (unsigned i = 0; i < count && i < 32; ++i)
        m->remainder[i] = 0xf8000000u | (i << 16) | i;
    if (count <= 32) m->remainder[count] = 0xfc123456u;
}

static V792_BLT_STATUS run(Mock *m, size_t capacity, uint32_t *words,
                            V792_BLT_RESULT *result)
{
    V792_BLT_IO io = {read_header, read_block, m};
    return v792_read_blt32(&io, BASE, words, capacity, result);
}

int main(void)
{
    Mock m;
    uint32_t words[64] = {0};
    V792_BLT_RESULT result;

    prepare(&m, 0);
    assert(run(&m, 64, words, &result) == V792_BLT_OK);
    assert(result.words == 2 && result.requested_bytes == 4 &&
           result.actual_bytes == 4 && result.event_counter == 0x123456u);
    assert(m.header_reads == 1 && m.blt_reads == 1);

    prepare(&m, 32);
    assert(run(&m, 64, words, &result) == V792_BLT_OK);
    assert(result.words == 34 && result.measurements == 32 &&
           result.requested_bytes == 132 && result.actual_bytes == 132 &&
           m.requested == 132 && words[33] == 0xfc123456u);

    prepare(&m, 33);
    assert(run(&m, 64, words, &result) == V792_BLT_INVALID_HEADER);
    assert(m.header_reads == 1 && m.blt_reads == 0);
    prepare(&m, 32);
    assert(run(&m, 33, words, &result) == V792_BLT_INVALID_HEADER);
    assert(m.blt_reads == 0);

    prepare(&m, 32);
    m.header = 0xf8002000u;
    assert(run(&m, 64, words, &result) == V792_BLT_INVALID_HEADER);
    assert(m.blt_reads == 0);
    prepare(&m, 32);
    m.header_failure = 1;
    assert(run(&m, 64, words, &result) == V792_BLT_HEADER_READ_ERROR);
    assert(m.blt_reads == 0);

    prepare(&m, 32);
    m.short_bytes = 4;
    assert(run(&m, 64, words, &result) == V792_BLT_SHORT_TRANSFER);
    assert(m.header_reads == 1 && m.blt_reads == 1);
    prepare(&m, 32);
    m.caen_status = cvBusError;
    assert(run(&m, 64, words, &result) == V792_BLT_TRANSFER_ERROR);
    assert(result.caen_status == cvBusError && m.header_reads == 1 &&
           m.blt_reads == 1);

    prepare(&m, 32);
    m.remainder[32] = 0xf8000000u;
    assert(run(&m, 64, words, &result) == V792_BLT_INVALID_EVENT);
    prepare(&m, 32);
    m.remainder[0] = 0xf9000000u;
    assert(run(&m, 64, words, &result) == V792_BLT_INVALID_EVENT);
    prepare(&m, 32);
    m.remainder[0] = 0x00000000u;
    assert(run(&m, 64, words, &result) == V792_BLT_INVALID_EVENT);
    prepare(&m, 32);
    m.remainder[1] = m.remainder[0];
    assert(run(&m, 64, words, &result) == V792_BLT_INVALID_EVENT);

    puts("test_v792_blt32: passed");
    return 0;
}
