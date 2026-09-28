#ifndef V792_BLT32_H
#define V792_BLT32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { V792_EVENT_MAX_WORDS = 34, V792_EVENT_MAX_CHANNELS = 32 };

typedef struct {
    int (*read_word)(void *context, uint32_t address, uint32_t *word);
    int (*blt_read)(void *context, uint32_t address, void *destination,
                    int requested_bytes, int *actual_bytes);
    void *context;
} V792_BLT_IO;

typedef struct {
    size_t words;
    unsigned measurements;
    unsigned geo;
    uint32_t event_counter;
    int requested_bytes;
    int actual_bytes;
    int caen_status;
} V792_BLT_RESULT;

/* All results other than V792_BLT_OK are fatal for the current FIFO event. */
typedef enum {
    V792_BLT_OK = 0,
    V792_BLT_BAD_ARGUMENT,
    V792_BLT_HEADER_READ_ERROR,
    V792_BLT_INVALID_HEADER,
    V792_BLT_TRANSFER_ERROR,
    V792_BLT_SHORT_TRANSFER,
    V792_BLT_INVALID_EVENT
} V792_BLT_STATUS;

V792_BLT_STATUS v792_validate_raw_event(const uint32_t *data, size_t words,
                                          V792_BLT_RESULT *result);
V792_BLT_STATUS v792_read_blt32(const V792_BLT_IO *io, uint32_t base,
                                  uint32_t *data, size_t capacity_words,
                                  V792_BLT_RESULT *result);

#ifdef __cplusplus
}
#endif
#endif
