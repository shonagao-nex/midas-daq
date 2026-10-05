#ifndef V1720E_INTERNAL_H
#define V1720E_INTERNAL_H

#include "v1720e.h"

#define REG_CHANNEL_CONFIG       0x8000u
#define REG_BUFFER_ORGANIZATION  0x800Cu
#define REG_CUSTOM_SIZE          0x8020u
#define REG_ACQUISITION_CONTROL  0x8100u
#define REG_ACQUISITION_STATUS   0x8104u
#define REG_TRIGGER_SOURCE       0x810Cu
#define REG_POST_TRIGGER         0x8114u
#define REG_CHANNEL_ENABLE       0x8120u
#define REG_ROC_FIRMWARE         0x8124u
#define REG_EVENT_STORED         0x812Cu
#define REG_BOARD_INFO           0x8140u
#define REG_SOFTWARE_CLEAR       0xEF28u
#define REG_DC_OFFSET(ch)        (0x1098u + ((DWORD)(ch) << 8))

#define ACQUISITION_CONTROL_RUN_REQUEST  0x00000004u
#define ACQUISITION_STATUS_RUN_ACTIVE    0x00000004u
#define STATUS_EVENT_READY       0x00000008u
#define STATUS_EXTERNAL_CLOCK    0x00000020u
#define STATUS_PLL_OK            0x00000080u
#define STATUS_BOARD_READY       0x00000100u

#define CHANNEL_CONFIG_ZS_MASK   0x000F0000u
#define CHANNEL_CONFIG_PACK25    0x00000800u

#ifdef __cplusplus
extern "C" {
#endif

int v1720e_access_begin(MVME_INTERFACE *vme, int *saved_am, int *saved_mode);
int v1720e_access_end(MVME_INTERFACE *vme, int saved_am, int saved_mode, int operation_status);
int v1720e_read32(MVME_INTERFACE *vme, DWORD base, DWORD offset, DWORD *value);
int v1720e_write32(MVME_INTERFACE *vme, DWORD base, DWORD offset, DWORD value);
int v1720e_write_verify(MVME_INTERFACE *vme, DWORD base, DWORD offset, DWORD value);

#ifdef __cplusplus
}
#endif

#endif
