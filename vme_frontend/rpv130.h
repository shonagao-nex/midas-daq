#ifndef RPV130_H
#define RPV130_H

#include <stdint.h>

#include "mvmestd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RPV130_BASE_ADDRESS 0x00008FF0u

typedef struct {
    uint8_t latch1;
    uint8_t latch2;
    uint8_t rsff;
    uint8_t through;
    uint8_t csr1;
    uint8_t csr2;
} RPV130_STATUS;

uint8_t rpv130_read_latch1(MVME_INTERFACE *mvme, mvme_addr_t base, int *status);
uint8_t rpv130_read_latch2(MVME_INTERFACE *mvme, mvme_addr_t base, int *status);
uint8_t rpv130_read_rsff(MVME_INTERFACE *mvme, mvme_addr_t base, int *status);
uint8_t rpv130_read_through(MVME_INTERFACE *mvme, mvme_addr_t base, int *status);
uint8_t rpv130_read_csr1(MVME_INTERFACE *mvme, mvme_addr_t base, int *status);
uint8_t rpv130_read_csr2(MVME_INTERFACE *mvme, mvme_addr_t base, int *status);

/* Read all read-capable registers in one A16/D16 access-mode session. */
int rpv130_read_status(MVME_INTERFACE *mvme, mvme_addr_t base,
                       RPV130_STATUS *status);

#ifdef __cplusplus
}
#endif

#endif
