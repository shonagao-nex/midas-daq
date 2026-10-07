#ifndef V1720E_H
#define V1720E_H

#include <stddef.h>
#include <stdint.h>
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
    int running;
    int event_ready;
    int external_clock;
    int pll_locked;
    int board_ready;
} V1720E_ACQUISITION_STATE;

typedef struct {
    unsigned low_byte;
    unsigned model_byte;
    unsigned channels;
    int supported;
} V1720E_BOARD_ID;

/* Decode raw acquisition status bits without reading registers. */
V1720E_ACQUISITION_STATE v1720e_decode_acquisition_status(DWORD status);
/* Validate the three BoardInfo bytes used by this frontend. */
V1720E_BOARD_ID v1720e_decode_board_info(DWORD board_info);

int v1720e_probe(MVME_INTERFACE *vme, DWORD base, V1720E_BOARD_INFO *info);
int v1720e_data_ready(MVME_INTERFACE *vme, DWORD base, int *ready,
                      DWORD *event_stored, DWORD *acquisition_status);
/* BOR gate: require the RUN-start memory reset to leave no stored event. */
int v1720e_verify_empty_after_start(MVME_INTERFACE *vme, DWORD base,
                                    int *ready, DWORD *event_stored,
                                    DWORD *acquisition_status);
int v1720e_start(MVME_INTERFACE *vme, DWORD base);
int v1720e_stop(MVME_INTERFACE *vme, DWORD base);
/* Read only: return the Acquisition Control and Status RUN bits with their
 * complete register values. */
int v1720e_read_run_state(MVME_INTERFACE *vme, DWORD base,
                          DWORD *control, DWORD *status);
/* Stop only when either hardware RUN bit is set. stop_attempted is set before
 * calling v1720e_stop(); both RUN bits are checked again after a successful
 * stop. No write is performed when the module is already stopped. */
int v1720e_stop_if_running(MVME_INTERFACE *vme, DWORD base,
                           DWORD *control_after, DWORD *status_after,
                           int *stop_attempted);
/* Clears event memory without resetting or reloading board configuration.
 * event_stored_after_valid is false unless Event Stored was read successfully. */
int v1720e_software_clear(MVME_INTERFACE *vme, DWORD base,
                          DWORD *event_stored_after,
                          int *event_stored_after_valid);

#ifdef __cplusplus
}
#endif

#endif
