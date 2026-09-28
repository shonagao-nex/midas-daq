#ifndef V1190_FIFO_BLT32_H
#define V1190_FIFO_BLT32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* V1190 manual rev.13, sections 6.31-6.33. */
enum {
    V1190_FIFO_ENTRY_OFFSET = 0x1038,
    V1190_FIFO_STORED_OFFSET = 0x103c,
    V1190_FIFO_STATUS_OFFSET = 0x103e,
    /* Output Buffer VME aperture: Base+0x0000 through Base+0x0ffc. */
    V1190_BLT_APERTURE_WORDS = 1024,
    V1190_FIFO_WORD_COUNT_MAX = 0xffff,
    V1190_FIFO_SANITY_PERIOD = 100
};

typedef struct {
    unsigned successful_events_mod100;
    uint64_t successful_event_count;
} V1190_FIFO_BLT_STATE;

typedef struct {
    int (*read16)(void *context, uint32_t address, uint16_t *value);
    int (*read32)(void *context, uint32_t address, uint32_t *value);
    int (*blt_read)(void *context, uint32_t address, void *destination,
                    int requested_bytes, int *actual_bytes);
    void *context;
} V1190_FIFO_BLT_IO;

typedef struct {
    uint64_t fifo_status_ns;
    uint64_t stored_before_ns;
    uint64_t fifo_read_ns;
    uint64_t decode_ns;
    uint64_t blt_ns;
    uint64_t validate_ns;
    uint64_t stored_after_ns;
    int stored_before_checked;
    int stored_after_checked;
} V1190_FIFO_BLT_TIMING;

typedef struct {
    size_t words;
    uint32_t event_counter;
    uint16_t fifo_word_count;
    uint16_t fifo_event_counter;
    uint16_t trailer_word_count;
    uint16_t stored_before;
    uint16_t stored_after;
    int requested_bytes;
    int actual_bytes;
    int caen_status;
    int counter_consistent;
    V1190_FIFO_BLT_TIMING timing;
} V1190_FIFO_BLT_RESULT;

typedef enum {
    V1190_FIFO_BLT_OK = 0,
    V1190_FIFO_BLT_BAD_ARGUMENT,
    V1190_FIFO_BLT_STATUS_ERROR,
    V1190_FIFO_BLT_EMPTY,
    V1190_FIFO_BLT_ENTRY_ERROR,
    V1190_FIFO_BLT_INVALID_COUNT,
    V1190_FIFO_BLT_TRANSFER_ERROR,
    V1190_FIFO_BLT_SHORT_TRANSFER,
    V1190_FIFO_BLT_INVALID_EVENT,
    V1190_FIFO_BLT_STORED_ERROR,
    V1190_FIFO_BLT_STORED_MISMATCH
} V1190_FIFO_BLT_STATUS;

V1190_FIFO_BLT_STATUS v1190_fifo_validate_event(const uint32_t *data,
    size_t words, uint16_t fifo_counter, V1190_FIFO_BLT_RESULT *result);
void v1190_fifo_blt_state_reset(V1190_FIFO_BLT_STATE *state);
V1190_FIFO_BLT_STATUS v1190_fifo_read_blt32(const V1190_FIFO_BLT_IO *io,
    uint32_t base, uint32_t *data, size_t capacity_words,
    int strict_sync_check, V1190_FIFO_BLT_STATE *state,
    V1190_FIFO_BLT_RESULT *result);

#ifdef __cplusplus
}
#endif
#endif
