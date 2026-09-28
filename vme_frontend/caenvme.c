#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <CAENVMElib.h>

#include "mvmestd.h"
#include "caenvme.h"

int caenvme_blt_read32(int handle, mvme_addr_t address, void *destination,
                       int requested_bytes, int *actual_bytes)
{
    if (actual_bytes)
        *actual_bytes = 0;
    if (!destination || !actual_bytes || requested_bytes < 0 ||
        (requested_bytes % 4) != 0)
        return cvInvalidParam;
    return CAENVME_BLTReadCycle(handle, address, destination, requested_bytes,
                                cvA32_U_BLT, cvD32, actual_bytes);
}

int caenvme_a24_blt_read32(int handle, mvme_addr_t address, void *destination,
                           int requested_bytes, int *actual_bytes)
{
    if (actual_bytes)
        *actual_bytes = 0;
    if (!destination || !actual_bytes || requested_bytes <= 0 ||
        (requested_bytes % 4) != 0)
        return cvInvalidParam;
    return CAENVME_BLTReadCycle(handle, address, destination, requested_bytes,
                                cvA24_U_BLT, cvD32, actual_bytes);
}


/* Convert a MIDAS address modifier to the corresponding CAEN address modifier. */
static int caen_get_am(int am, CVAddressModifier *caen_am)
{
    switch (am) {
    case MVME_AM_A32_SD:
        *caen_am = cvA32_S_DATA;
        return MVME_SUCCESS;

    case MVME_AM_A32_ND:
        *caen_am = cvA32_U_DATA;
        return MVME_SUCCESS;

    case MVME_AM_A24_SD:
        *caen_am = cvA24_S_DATA;
        return MVME_SUCCESS;

    case MVME_AM_A24_ND:
        *caen_am = cvA24_U_DATA;
        return MVME_SUCCESS;

    case MVME_AM_A16_SD:
        *caen_am = cvA16_S;
        return MVME_SUCCESS;

    case MVME_AM_A16_ND:
        *caen_am = cvA16_U;
        return MVME_SUCCESS;

    default:
        return MVME_UNSUPPORTED;
    }
}


/* Convert a MIDAS data mode to the corresponding CAEN data width. */
static int caen_get_dmode(int dmode, CVDataWidth *caen_dmode, unsigned int *width)
{
    switch (dmode) {
    case MVME_DMODE_D8:
        *caen_dmode = cvD8;
        *width = 1;
        return MVME_SUCCESS;

    case MVME_DMODE_D16:
        *caen_dmode = cvD16;
        *width = 2;
        return MVME_SUCCESS;

    case MVME_DMODE_D32:
        *caen_dmode = cvD32;
        *width = 4;
        return MVME_SUCCESS;

    default:
        return MVME_UNSUPPORTED;
    }
}


/* Open the CAEN V3718 and initialize the MIDAS VME interface structure. */
int mvme_open(MVME_INTERFACE **mvme, int index)
{
    uint32_t link = 0;                             // V3718 USB link number
    int32_t handle = -1;                           // CAENVMELib device handle
    CVErrorCodes ret;                              // CAENVMELib return code

    if (mvme == NULL)
        return MVME_INVALID_PARAM;

    if (index != 0)
        return MVME_INVALID_PARAM;

    *mvme = (MVME_INTERFACE *)calloc(1, sizeof(MVME_INTERFACE));

    if (*mvme == NULL)
        return MVME_NO_MEM;

    ret = CAENVME_Init2(cvUSB_V3718, &link, 0, &handle);

    if (ret != cvSuccess) {
        fprintf(stderr, "mvme_open: CAENVME_Init2 failed: %d (%s)\n",
                ret, CAENVME_DecodeError(ret));

        free(*mvme);
        *mvme = NULL;

        return MVME_NO_INTERFACE;
    }

    (*mvme)->initialized = 1;
    (*mvme)->handle = handle;
    (*mvme)->index = index;
    (*mvme)->info = NULL;
    (*mvme)->am = MVME_AM_DEFAULT;
    (*mvme)->dmode = MVME_DMODE_DEFAULT;
    (*mvme)->blt_mode = MVME_BLT_NONE;
    (*mvme)->table = NULL;

    printf("mvme_open: CAEN V3718 opened successfully. Handle = %d\n", handle);

    return MVME_SUCCESS;
}


/* Close the CAEN V3718 and release the MIDAS VME interface structure. */
int mvme_close(MVME_INTERFACE *mvme)
{
    CVErrorCodes ret;

    if (mvme == NULL)
        return MVME_INVALID_PARAM;

    if (mvme->initialized) {
        ret = CAENVME_End(mvme->handle);

        if (ret != cvSuccess) {
            fprintf(stderr, "mvme_close: CAENVME_End failed: %d (%s)\n",
                    ret, CAENVME_DecodeError(ret));

            free(mvme);
            return MVME_ACCESS_ERROR;
        }
    }

    free(mvme);

    return MVME_SUCCESS;
}


/* Return unsupported for VME system reset in the initial V3718 backend implementation. */
int mvme_sysreset(MVME_INTERFACE *mvme)
{
    if (mvme == NULL)
        return MVME_INVALID_PARAM;

    return MVME_UNSUPPORTED;
}


/* Read a block of data using repeated CAEN single-cycle VME accesses. */
int mvme_read(MVME_INTERFACE *mvme, void *dst, mvme_addr_t vme_addr, mvme_size_t n_bytes)
{
    CVAddressModifier caen_am;
    CVDataWidth caen_dmode;
    CVErrorCodes ret;
    unsigned int width;
    unsigned int offset;

    if (mvme == NULL || dst == NULL)
        return MVME_INVALID_PARAM;

    if (caen_get_am(mvme->am, &caen_am) != MVME_SUCCESS)
        return MVME_UNSUPPORTED;

    if (caen_get_dmode(mvme->dmode, &caen_dmode, &width) != MVME_SUCCESS)
        return MVME_UNSUPPORTED;

    if (n_bytes % width != 0)
        return MVME_INVALID_PARAM;

    for (offset = 0; offset < n_bytes; offset += width) {
        ret = CAENVME_ReadCycle(
            mvme->handle,
            vme_addr + offset,
            (uint8_t *)dst + offset,
            caen_am,
            caen_dmode
        );

        if (ret != cvSuccess) {
            fprintf(stderr,
                    "mvme_read: read failed at 0x%08X: %d (%s)\n",
                    vme_addr + offset, ret, CAENVME_DecodeError(ret));

            return MVME_ACCESS_ERROR;
        }
    }

    return MVME_SUCCESS;
}


/* Read a single VME value using the currently selected address and data modes. */
unsigned int mvme_read_value(MVME_INTERFACE *mvme, mvme_addr_t vme_addr)
{
    CVAddressModifier caen_am;
    CVDataWidth caen_dmode;
    CVErrorCodes ret;
    unsigned int width;
    uint32_t data = 0;

    if (mvme == NULL)
        return 0;

    if (caen_get_am(mvme->am, &caen_am) != MVME_SUCCESS)
        return 0;

    if (caen_get_dmode(mvme->dmode, &caen_dmode, &width) != MVME_SUCCESS)
        return 0;

    ret = CAENVME_ReadCycle(
        mvme->handle,
        vme_addr,
        &data,
        caen_am,
        caen_dmode
    );

    if (ret != cvSuccess) {
        fprintf(stderr,
                "mvme_read_value: read failed at 0x%08X: %d (%s)\n",
                vme_addr, ret, CAENVME_DecodeError(ret));

        return 0;
    }

    if (width == 1)
        return data & 0xFF;

    if (width == 2)
        return data & 0xFFFF;

    return data;
}


/* Write a block of data using repeated CAEN single-cycle VME accesses. */
int mvme_write(MVME_INTERFACE *mvme, mvme_addr_t vme_addr, void *src, mvme_size_t n_bytes)
{
    CVAddressModifier caen_am;
    CVDataWidth caen_dmode;
    CVErrorCodes ret;
    unsigned int width;
    unsigned int offset;

    if (mvme == NULL || src == NULL)
        return MVME_INVALID_PARAM;

    if (caen_get_am(mvme->am, &caen_am) != MVME_SUCCESS)
        return MVME_UNSUPPORTED;

    if (caen_get_dmode(mvme->dmode, &caen_dmode, &width) != MVME_SUCCESS)
        return MVME_UNSUPPORTED;

    if (n_bytes % width != 0)
        return MVME_INVALID_PARAM;

    for (offset = 0; offset < n_bytes; offset += width) {
        ret = CAENVME_WriteCycle(
            mvme->handle,
            vme_addr + offset,
            (uint8_t *)src + offset,
            caen_am,
            caen_dmode
        );

        if (ret != cvSuccess) {
            fprintf(stderr,
                    "mvme_write: write failed at 0x%08X: %d (%s)\n",
                    vme_addr + offset, ret, CAENVME_DecodeError(ret));

            return MVME_ACCESS_ERROR;
        }
    }

    return MVME_SUCCESS;
}


/* Write a single VME value using the currently selected address and data modes. */
int mvme_write_value(MVME_INTERFACE *mvme, mvme_addr_t vme_addr, unsigned int value)
{
    CVAddressModifier caen_am;
    CVDataWidth caen_dmode;
    CVErrorCodes ret;
    unsigned int width;
    uint32_t data = value;

    if (mvme == NULL)
        return MVME_INVALID_PARAM;

    if (caen_get_am(mvme->am, &caen_am) != MVME_SUCCESS)
        return MVME_UNSUPPORTED;

    if (caen_get_dmode(mvme->dmode, &caen_dmode, &width) != MVME_SUCCESS)
        return MVME_UNSUPPORTED;

    ret = CAENVME_WriteCycle(
        mvme->handle,
        vme_addr,
        &data,
        caen_am,
        caen_dmode
    );

    if (ret != cvSuccess) {
        fprintf(stderr,
                "mvme_write_value: write failed at 0x%08X: %d (%s)\n",
                vme_addr, ret, CAENVME_DecodeError(ret));

        return MVME_ACCESS_ERROR;
    }

    return MVME_SUCCESS;
}


/* Set the VME address modifier used for subsequent accesses. */
int mvme_set_am(MVME_INTERFACE *mvme, int am)
{
    CVAddressModifier caen_am;

    if (mvme == NULL)
        return MVME_INVALID_PARAM;

    if (caen_get_am(am, &caen_am) != MVME_SUCCESS)
        return MVME_UNSUPPORTED;

    mvme->am = am;

    return MVME_SUCCESS;
}


/* Return the currently selected VME address modifier. */
int mvme_get_am(MVME_INTERFACE *mvme, int *am)
{
    if (mvme == NULL || am == NULL)
        return MVME_INVALID_PARAM;

    *am = mvme->am;

    return MVME_SUCCESS;
}


/* Set the VME data width used for subsequent accesses. */
int mvme_set_dmode(MVME_INTERFACE *mvme, int dmode)
{
    CVDataWidth caen_dmode;
    unsigned int width;

    if (mvme == NULL)
        return MVME_INVALID_PARAM;

    if (caen_get_dmode(dmode, &caen_dmode, &width) != MVME_SUCCESS)
        return MVME_UNSUPPORTED;

    mvme->dmode = dmode;

    return MVME_SUCCESS;
}


/* Return the currently selected VME data width. */
int mvme_get_dmode(MVME_INTERFACE *mvme, int *dmode)
{
    if (mvme == NULL || dmode == NULL)
        return MVME_INVALID_PARAM;

    *dmode = mvme->dmode;

    return MVME_SUCCESS;
}


/* Set the block-transfer mode; only programmed I/O is supported in this initial implementation. */
int mvme_set_blt(MVME_INTERFACE *mvme, int mode)
{
    if (mvme == NULL)
        return MVME_INVALID_PARAM;

    if (mode != MVME_BLT_NONE)
        return MVME_UNSUPPORTED;

    mvme->blt_mode = mode;

    return MVME_SUCCESS;
}


/* Return the currently selected block-transfer mode. */
int mvme_get_blt(MVME_INTERFACE *mvme, int *mode)
{
    if (mvme == NULL || mode == NULL)
        return MVME_INVALID_PARAM;

    *mode = mvme->blt_mode;

    return MVME_SUCCESS;
}
