#ifndef V1720E_READOUT_H
#define V1720E_READOUT_H

#include "v1720e.h"

#define V1720E_BLT32_MAX_BYTES           4096u

typedef enum {
    SINGLE_D32,
    BLT32
} V1720E_READOUT_MODE;

typedef struct {
    uint64_t header_read_ns[4];
    uint64_t header_decode_ns;
    uint64_t blt_ns;
    uint64_t blt_validation_ns;
} V1720E_READ_TIMING;

typedef struct {
    size_t words;
    DWORD event_size;
    DWORD channel_mask;
    DWORD event_counter;
    DWORD trigger_time_tag;
    int header_valid;
    int size_valid;
    int channel_mask_valid;
    int blt_status;
    int blt_requested_bytes;
    int blt_actual_bytes;
    V1720E_READ_TIMING timing;
} V1720E_EVENT_INFO;


#ifdef __cplusplus
extern "C" {
#endif

int v1720e_read_event(MVME_INTERFACE *vme, DWORD base, DWORD *data,
                      size_t capacity, DWORD expected_event_words,
                      DWORD expected_channel_mask, V1720E_EVENT_INFO *info);
int v1720e_read_event_mode(MVME_INTERFACE *vme, DWORD base, DWORD *data,
                           size_t capacity, DWORD expected_event_words,
                           DWORD expected_channel_mask,
                           V1720E_READOUT_MODE mode, V1720E_EVENT_INFO *info);
#ifdef __cplusplus
}
#endif

#endif
