/* CAEN V775 32-channel multievent TDC local driver. */
#include <stdio.h>

#include "v775.h"

WORD v775_Read16(MVME_INTERFACE *mvme, DWORD base, int offset)
{
    mvme_set_dmode(mvme, MVME_DMODE_D16);
    return (WORD)mvme_read_value(mvme, base + (DWORD)offset);
}

void v775_Write16(MVME_INTERFACE *mvme, DWORD base, int offset, WORD value)
{
    mvme_set_dmode(mvme, MVME_DMODE_D16);
    mvme_write_value(mvme, base + (DWORD)offset, value);
}

int v775_Status1Read(MVME_INTERFACE *mvme, DWORD base)
{
    return (int)v775_Read16(mvme, base, V775_STATUS1);
}

int v775_Status2Read(MVME_INTERFACE *mvme, DWORD base)
{
    return (int)v775_Read16(mvme, base, V775_STATUS2);
}

int v775_DataReady(MVME_INTERFACE *mvme, DWORD base)
{
    return v775_Status1Read(mvme, base) & V775_STATUS1_DATA_READY;
}

void v775_EvtCntRead(MVME_INTERFACE *mvme, DWORD base, DWORD *evtcnt)
{
    WORD low;
    WORD high;

    if (evtcnt == NULL)
        return;
    low = v775_Read16(mvme, base, V775_EVENT_COUNTER_LOW);
    high = v775_Read16(mvme, base, V775_EVENT_COUNTER_HIGH);
    *evtcnt = ((DWORD)(high & 0x00FFu) << 16) | (DWORD)low;
}

WORD v775_FullScaleRead(MVME_INTERFACE *mvme, DWORD base)
{
    return v775_Read16(mvme, base, V775_FULL_SCALE_RANGE);
}

WORD v775_FclrWindowRead(MVME_INTERFACE *mvme, DWORD base)
{
    return v775_Read16(mvme, base, V775_FCLR_WINDOW);
}

int v775_ThresholdRead(MVME_INTERFACE *mvme, DWORD base, WORD *threshold)
{
    if (threshold == NULL)
        return 0;
    for (DWORD channel = 0; channel < V775_MAX_CHANNELS; ++channel)
        threshold[channel] = v775_Read16(
            mvme, base, V775_THRESHOLD_BASE + 2 * channel) & 0x01FFu;
    return (int)V775_MAX_CHANNELS;
}

void v775_Status(MVME_INTERFACE *mvme, DWORD base)
{
    WORD firmware;
    WORD status1;
    WORD status2;
    WORD bitset2;
    WORD counter_low;
    WORD counter_high;
    WORD thresholds[V775_MAX_CHANNELS];
    DWORD counter;

    firmware = v775_Read16(mvme, base, V775_FIRMWARE_REVISION);
    status1 = (WORD)v775_Status1Read(mvme, base);
    status2 = (WORD)v775_Status2Read(mvme, base);
    bitset2 = v775_Read16(mvme, base, V775_BIT_SET2);
    counter_low = v775_Read16(mvme, base, V775_EVENT_COUNTER_LOW);
    counter_high = v775_Read16(mvme, base, V775_EVENT_COUNTER_HIGH);
    counter = ((DWORD)(counter_high & 0x00FFu) << 16) | counter_low;

    printf("V775 at VME A24 0x%08x:\n", base);
    printf("Firmware Revision: 0x%04x\n", firmware);
    printf("Status 1: 0x%04x\n", status1);
    printf("  DataReady: %s\n",
           status1 & V775_STATUS1_DATA_READY ? "Y" : "N");
    printf("  Busy:      %s\n",
           status1 & V775_STATUS1_BUSY ? "Y" : "N");
    printf("  Amnesia:   %s\n",
           status1 & V775_STATUS1_AMNESIA ? "Y" : "N");
    printf("Status 2: 0x%04x\n", status2);
    printf("  Buffer Empty: %s\n",
           status2 & V775_STATUS2_BUFFER_EMPTY ? "Y" : "N");
    printf("  Buffer Full:  %s\n",
           status2 & V775_STATUS2_BUFFER_FULL ? "Y" : "N");
    printf("Bit Set 2: 0x%04x\n", bitset2);
    printf("  Common Start/Stop: %s\n",
           bitset2 & V775_BIT2_COMMON_STOP ? "Common Stop" : "Common Start");
    printf("  Empty Program: %s\n",
           bitset2 & V775_BIT2_EMPTY_PROGRAM ? "Y" : "N");
    printf("  All Trigger: %s\n",
           bitset2 & V775_BIT2_ALL_TRIGGER ? "Y" : "N");
    printf("Event Counter Low:  0x%04x\n", counter_low);
    printf("Event Counter High: 0x%04x\n", counter_high);
    printf("Event Counter (24-bit): 0x%06x\n", counter);
    printf("Full Scale Range:  0x%04x\n",
           v775_FullScaleRead(mvme, base));
    printf("Fast Clear Window: 0x%04x\n",
           v775_FclrWindowRead(mvme, base));

    v775_ThresholdRead(mvme, base, thresholds);
    for (DWORD channel = 0; channel < V775_MAX_CHANNELS; channel += 2) {
        printf("Threshold[%2u] = 0x%04x   Threshold[%2u] = 0x%04x\n",
               (unsigned)channel, thresholds[channel],
               (unsigned)(channel + 1), thresholds[channel + 1]);
    }
}

int v775_isPresent(MVME_INTERFACE *mvme, DWORD base)
{
    return v775_Read16(mvme, base, V775_FIRMWARE_REVISION) != 0xFFFFu;
}

void v775_Trigger(MVME_INTERFACE *mvme, DWORD base)
{
    v775_Write16(mvme, base, V775_SW_COMM, 0);
}

/* Future configuration operations; not used by Phase 1 frontend code. */
void v775_DataClear(MVME_INTERFACE *mvme, DWORD base)
{
    v775_Write16(mvme, base, V775_BIT_SET2, V775_BIT2_CLEAR_DATA);
    v775_Write16(mvme, base, V775_BIT_CLEAR2, V775_BIT2_CLEAR_DATA);
}

void v775_SoftReset(MVME_INTERFACE *mvme, DWORD base)
{
    v775_Write16(mvme, base, V775_BIT_SET1, V775_BIT1_SOFTWARE_RESET);
    v775_Write16(mvme, base, V775_BIT_CLEAR1, V775_BIT1_SOFTWARE_RESET);
}

void v775_SingleShotReset(MVME_INTERFACE *mvme, DWORD base)
{
    v775_Write16(mvme, base, V775_SINGLE_SHOT_RESET, 1);
}

void v775_EvtCntReset(MVME_INTERFACE *mvme, DWORD base)
{
    v775_Write16(mvme, base, V775_EVENT_COUNTER_RESET, 1);
}

int v775_ThresholdWrite(MVME_INTERFACE *mvme, DWORD base, WORD *threshold)
{
    if (threshold == NULL)
        return 0;
    for (DWORD channel = 0; channel < V775_MAX_CHANNELS; ++channel)
        v775_Write16(mvme, base, V775_THRESHOLD_BASE + 2 * channel,
                     threshold[channel] & 0x01FFu);
    return (int)V775_MAX_CHANNELS;
}

void v775_FullScaleSet(MVME_INTERFACE *mvme, DWORD base, WORD value)
{
    v775_Write16(mvme, base, V775_FULL_SCALE_RANGE, value & 0x00FFu);
}

void v775_FclrWindowSet(MVME_INTERFACE *mvme, DWORD base, WORD value)
{
    v775_Write16(mvme, base, V775_FCLR_WINDOW, value & 0x03FFu);
}
