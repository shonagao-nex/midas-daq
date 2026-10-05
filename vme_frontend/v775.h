/*
 * CAEN V775 32-channel multievent TDC, local frontend driver.
 *
 * Register definitions and bit meanings follow the CAEN V775 user manual.
 * This is intentionally separate from v792.h: although many offsets are
 * shared, 0x1060, the Bit Set 2 meanings, thresholds, and data semantics are
 * V775-specific.
 */
#ifndef V775_LOCAL_DRIVER_H
#define V775_LOCAL_DRIVER_H

#include "mvmestd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define V775_MAX_CHANNELS       ((DWORD)32)

/* VME register offsets. */
#define V775_OUTPUT_BUFFER      ((DWORD)0x0000)
#define V775_FIRMWARE_REVISION  ((DWORD)0x1000)
#define V775_GEO_ADDRESS        ((DWORD)0x1002)
#define V775_MCST_CBLT_ADDRESS  ((DWORD)0x1004)
#define V775_BIT_SET1           ((DWORD)0x1006)
#define V775_BIT_CLEAR1         ((DWORD)0x1008)
#define V775_INTERRUPT_LEVEL    ((DWORD)0x100A)
#define V775_INTERRUPT_VECTOR   ((DWORD)0x100C)
#define V775_STATUS1            ((DWORD)0x100E)
#define V775_CONTROL1           ((DWORD)0x1010)
#define V775_ADER_HIGH          ((DWORD)0x1012)
#define V775_ADER_LOW           ((DWORD)0x1014)
#define V775_SINGLE_SHOT_RESET  ((DWORD)0x1016)
#define V775_MCST_CBLT_CONTROL  ((DWORD)0x101A)
#define V775_EVENT_TRIGGER      ((DWORD)0x1020)
#define V775_STATUS2            ((DWORD)0x1022)
#define V775_EVENT_COUNTER_LOW  ((DWORD)0x1024)
#define V775_EVENT_COUNTER_HIGH ((DWORD)0x1026)
#define V775_INCREMENT_EVENT    ((DWORD)0x1028)
#define V775_INCREMENT_OFFSET  ((DWORD)0x102A)
#define V775_LOAD_TEST          ((DWORD)0x102C)
#define V775_FCLR_WINDOW        ((DWORD)0x102E)
#define V775_BIT_SET2           ((DWORD)0x1032)
#define V775_BIT_CLEAR2         ((DWORD)0x1034)
#define V775_W_TEST_ADDRESS     ((DWORD)0x1036)
#define V775_TEST_WORD_HIGH     ((DWORD)0x1038)
#define V775_TEST_WORD_LOW      ((DWORD)0x103A)
#define V775_CRATE_SELECT       ((DWORD)0x103C)
#define V775_TEST_EVENT_WRITE   ((DWORD)0x103E)
#define V775_EVENT_COUNTER_RESET ((DWORD)0x1040)
#define V775_FULL_SCALE_RANGE   ((DWORD)0x1060)
#define V775_R_TEST_ADDRESS     ((DWORD)0x1064)
#define V775_SW_COMM            ((DWORD)0x1068)
#define V775_SLIDE_CONSTANT     ((DWORD)0x106A)
#define V775_AAD               ((DWORD)0x1070)
#define V775_BAD               ((DWORD)0x1072)
#define V775_THRESHOLD_BASE     ((DWORD)0x1080)

/* Status Register 1 bits. */
#define V775_STATUS1_DATA_READY ((WORD)0x0001)
#define V775_STATUS1_BUSY       ((WORD)0x0004)
#define V775_STATUS1_AMNESIA    ((WORD)0x0010)

/* Status Register 2 bits. */
#define V775_STATUS2_BUFFER_EMPTY ((WORD)0x0002)
#define V775_STATUS2_BUFFER_FULL  ((WORD)0x0004)

/* Bit Set/Clear 1 bits. */
#define V775_BIT1_BERR_FLAG      ((WORD)0x0008)
#define V775_BIT1_SELECT_ADDRESS ((WORD)0x0010)
#define V775_BIT1_SOFTWARE_RESET ((WORD)0x0080)

/* Bit Set/Clear 2 bits. */
#define V775_BIT2_MEMORY_TEST       ((WORD)0x0001)
#define V775_BIT2_OFFLINE           ((WORD)0x0002)
#define V775_BIT2_CLEAR_DATA        ((WORD)0x0004)
#define V775_BIT2_OVER_RANGE        ((WORD)0x0008)
#define V775_BIT2_LOW_THRESHOLD     ((WORD)0x0010)
#define V775_BIT2_VALID_CONTROL     ((WORD)0x0020)
#define V775_BIT2_TEST_ACQUISITION  ((WORD)0x0040)
#define V775_BIT2_SLIDE_ENABLE      ((WORD)0x0080)
#define V775_BIT2_STEP_THRESHOLD    ((WORD)0x0100)
#define V775_BIT2_COMMON_STOP       ((WORD)0x0400)
#define V775_BIT2_AUTO_INCREMENT    ((WORD)0x0800)
#define V775_BIT2_EMPTY_PROGRAM     ((WORD)0x1000)
#define V775_BIT2_SLIDE_SUB_ENABLE  ((WORD)0x2000)
#define V775_BIT2_ALL_TRIGGER       ((WORD)0x4000)

/* Output-buffer type field (bits 26..24). */
#define V775_DATA_TYPE_MEASUREMENT ((DWORD)0u)
#define V775_DATA_TYPE_HEADER      ((DWORD)2u)
#define V775_DATA_TYPE_EOB         ((DWORD)4u)
#define V775_DATA_TYPE_INVALID     ((DWORD)6u)

WORD v775_Read16(MVME_INTERFACE *mvme, DWORD base, int offset);
void v775_Write16(MVME_INTERFACE *mvme, DWORD base, int offset, WORD value);

int  v775_DataReady(MVME_INTERFACE *mvme, DWORD base);
int  v775_Status1Read(MVME_INTERFACE *mvme, DWORD base);
int  v775_Status2Read(MVME_INTERFACE *mvme, DWORD base);

void v775_EvtCntRead(MVME_INTERFACE *mvme, DWORD base, DWORD *evtcnt);
WORD v775_FullScaleRead(MVME_INTERFACE *mvme, DWORD base);
WORD v775_FclrWindowRead(MVME_INTERFACE *mvme, DWORD base);
int  v775_ThresholdRead(MVME_INTERFACE *mvme, DWORD base, WORD *threshold);

void v775_Trigger(MVME_INTERFACE *mvme, DWORD base);
int  v775_isPresent(MVME_INTERFACE *mvme, DWORD base);
void v775_Status(MVME_INTERFACE *mvme, DWORD base);

/* Future configuration APIs. Phase 1 frontend code does not call these. */
void v775_DataClear(MVME_INTERFACE *mvme, DWORD base);
void v775_SoftReset(MVME_INTERFACE *mvme, DWORD base);
void v775_SingleShotReset(MVME_INTERFACE *mvme, DWORD base);
void v775_EvtCntReset(MVME_INTERFACE *mvme, DWORD base);
int  v775_ThresholdWrite(MVME_INTERFACE *mvme, DWORD base, WORD *threshold);
void v775_FullScaleSet(MVME_INTERFACE *mvme, DWORD base, WORD value);
void v775_FclrWindowSet(MVME_INTERFACE *mvme, DWORD base, WORD value);

#ifdef __cplusplus
}

namespace v775_basic {

// Preserve checked writes and the established Bit Set/Clear 2 order.
inline bool clear_data(MVME_INTERFACE *vme, DWORD base,
                       bool (*write16)(MVME_INTERFACE *, DWORD, WORD, const char *),
                       bool manual)
{
  const char *set_desc = manual ? "V775 manual Data Clear set" : "V775 Data Clear set";
  const char *clear_desc = manual ? "V775 manual Data Clear clear" : "V775 Data Clear clear";
  return write16(vme, base + V775_BIT_SET2, V775_BIT2_CLEAR_DATA, set_desc) &&
         write16(vme, base + V775_BIT_CLEAR2, V775_BIT2_CLEAR_DATA, clear_desc);
}

}
#endif

#endif /* V775_LOCAL_DRIVER_H */
