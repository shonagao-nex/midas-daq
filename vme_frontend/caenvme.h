#ifndef CAENVME_MVMESTD_H
#define CAENVME_MVMESTD_H

#include "mvmestd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Raw CAEN return code and byte count are both reported to the caller. */
int caenvme_blt_read32(int handle, mvme_addr_t address, void *destination,
                       int requested_bytes, int *actual_bytes);
int caenvme_a24_blt_read32(int handle, mvme_addr_t address, void *destination,
                           int requested_bytes, int *actual_bytes);

#ifdef __cplusplus
}
#endif

#endif
