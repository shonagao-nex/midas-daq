#ifndef V1720E_H
#define V1720E_H

#include <stddef.h>
#include "mvmestd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define V1720E_BASE_ADDRESS             0x11110000u
#define V1720E_DEFAULT_RECORD_SAMPLES   256u
#define V1720E_DEFAULT_CUSTOM_SIZE      0x40u
#define V1720E_DEFAULT_CHANNEL_MASK     0xFFu
#define V1720E_DEFAULT_EVENT_WORDS      1028u
#define V1720E_MAX_EVENT_WORDS          4096u
#define V1720E_CHANNEL_COUNT            8u

typedef struct {
    DWORD buffer_organization;
    DWORD custom_size;
    DWORD post_trigger;
    DWORD trigger_source;
    DWORD channel_enable;
    WORD dc_offset[V1720E_CHANNEL_COUNT];
} V1720E_CONFIG;

typedef struct {
    DWORD board_info;
    DWORD roc_firmware;
    DWORD buffer_organization;
    DWORD custom_size;
    DWORD post_trigger;
    DWORD trigger_source;
    DWORD channel_enable;
    DWORD channel_config;
    WORD dc_offset[V1720E_CHANNEL_COUNT];
} V1720E_CONFIG_READBACK;

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
int v1720e_configure(MVME_INTERFACE *vme, DWORD base,
                     const V1720E_CONFIG *config);
int v1720e_read_configuration(MVME_INTERFACE *vme, DWORD base,
                              V1720E_CONFIG_READBACK *readback);
int v1720e_data_ready(MVME_INTERFACE *vme, DWORD base, int *ready,
                      DWORD *event_stored, DWORD *acquisition_status);
int v1720e_read_event(MVME_INTERFACE *vme, DWORD base, DWORD *data,
                      size_t capacity, DWORD expected_event_words,
                      DWORD expected_channel_mask, V1720E_EVENT_INFO *info);
int v1720e_start(MVME_INTERFACE *vme, DWORD base);
int v1720e_stop(MVME_INTERFACE *vme, DWORD base);
/* Clears event memory without resetting or reloading board configuration.
 * event_stored_after_valid is false unless Event Stored was read successfully. */
int v1720e_software_clear(MVME_INTERFACE *vme, DWORD base,
                          DWORD *event_stored_after,
                          int *event_stored_after_valid);

#ifdef __cplusplus
}
#endif

#endif
