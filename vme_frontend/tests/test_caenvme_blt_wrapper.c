#include "caenvme.h"

#include <CAENVMElib.h>
#include <stdio.h>

static int calls;
static int requested;
static CVAddressModifier modifier;
static CVDataWidth width;

CVErrorCodes __wrap_CAENVME_BLTReadCycle(int32_t handle, uint32_t address,
                                         void *buffer, int size,
                                         CVAddressModifier am, CVDataWidth dw,
                                         int *count) {
    if (handle != 7 || address != 0x11110000u || !buffer || !count)
        return cvInvalidParam;
    ++calls;
    requested = size;
    modifier = am;
    width = dw;
    *count = size - 4;
    return cvBusError;
}

int main(void) {
    DWORD data[1024] = {0};
    int count = -1;
    int result = caenvme_blt_read32(7, 0x11110000u, data, 4096, &count);
    if (result != cvBusError || count != 4092 || calls != 1 ||
        requested != 4096 || modifier != cvA32_U_BLT || width != cvD32) {
        fputs("FAIL: CAEN BLT arguments or raw result/count\n", stderr);
        return 1;
    }
    result = caenvme_blt_read32(7, 0x11110000u, data, 4095, &count);
    if (result != cvInvalidParam || count != 0 || calls != 1) {
        fputs("FAIL: non-word-aligned request reached CAEN API\n", stderr);
        return 1;
    }
    result = caenvme_a24_blt_read32(7, 0x11110000u, data, 132, &count);
    if (result != cvBusError || count != 128 || calls != 2 ||
        requested != 132 || modifier != cvA24_U_BLT || width != cvD32) {
        fputs("FAIL: A24 BLT arguments or raw result/count\n", stderr);
        return 1;
    }
    result = caenvme_a24_blt_read32(7, 0x11110000u, data, 131, &count);
    if (result != cvInvalidParam || count != 0 || calls != 2) {
        fputs("FAIL: A24 non-word-aligned request reached CAEN API\n", stderr);
        return 1;
    }
    puts("test_caenvme_blt_wrapper: passed");
    return 0;
}
