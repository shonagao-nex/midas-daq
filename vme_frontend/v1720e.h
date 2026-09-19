#ifndef V1720E_H
#define V1720E_H

#include <stddef.h>
#include "mvmestd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define V1720E_BASE_ADDRESS             0x11110000u
#define V1720E_EXPECTED_EVENT_WORDS     1028u
#define V1720E_MAX_EVENT_WORDS          4096u

typedef struct {
    DWORD board_info;
    DWORD roc_firmware;
    DWORD acquisition_control;
    DWORD acquisition_status;
    DWORD event_stored;
} V1720E_BOARD_INFO;

typedef struct {
    size_t words;
    DWORD event_size;
    DWORD channel_mask;
    DWORD event_counter;
    DWORD trigger_time_tag;
    int header_valid;
    int size_valid;
    int channel_mask_valid;
} V1720E_EVENT_INFO;

int v1720e_probe(MVME_INTERFACE *vme, DWORD base, V1720E_BOARD_INFO *info);
int v1720e_configure(MVME_INTERFACE *vme, DWORD base);
int v1720e_data_ready(MVME_INTERFACE *vme, DWORD base, int *ready,
                      DWORD *event_stored);
int v1720e_read_event(MVME_INTERFACE *vme, DWORD base, DWORD *data,
                      size_t capacity, V1720E_EVENT_INFO *info);
int v1720e_start(MVME_INTERFACE *vme, DWORD base);
int v1720e_stop(MVME_INTERFACE *vme, DWORD base);

#ifdef __cplusplus
}
#endif

#endif
