#ifndef RPV130_H
#define RPV130_H

#include <stdint.h>

#include "mvmestd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RPV130_BASE_ADDRESS 0x00008FF0u
#define RPV130_CSR1_CLR1    0x02u
#define RPV130_CSR1_MASK1   0x08u
#define RPV130_CSR1_ENABLE1 0x10u
#define RPV130_CSR1_BUSY1   0x20u
#define RPV130_CSR1_ENABLE3 0x40u
#define RPV130_CSR1_CHANNEL1_ARMED \
    (RPV130_CSR1_MASK1 | RPV130_CSR1_ENABLE1)

typedef struct {
    uint8_t latch1;
    uint8_t latch2;
    uint8_t rsff;
    uint8_t through;
    uint8_t csr1;
    uint8_t csr2;
} RPV130_STATUS;

/* CLOCK_MONOTONIC timestamps around CSR1 writes (nanoseconds). */
typedef struct {
    uint64_t clr1_before_ns;
    uint64_t clr1_after_ns;
    uint64_t rearm_after_ns;
} RPV130_BUSY_TIMING;

uint8_t rpv130_read_latch1(MVME_INTERFACE *mvme, mvme_addr_t base, int *status);
uint8_t rpv130_read_latch2(MVME_INTERFACE *mvme, mvme_addr_t base, int *status);
uint8_t rpv130_read_rsff(MVME_INTERFACE *mvme, mvme_addr_t base, int *status);
uint8_t rpv130_read_through(MVME_INTERFACE *mvme, mvme_addr_t base, int *status);
uint8_t rpv130_read_csr1(MVME_INTERFACE *mvme, mvme_addr_t base, int *status);
uint8_t rpv130_read_csr2(MVME_INTERFACE *mvme, mvme_addr_t base, int *status);

/* Read all read-capable registers in one A16/D16 access-mode session. */
int rpv130_read_status(MVME_INTERFACE *mvme, mvme_addr_t base,
                       RPV130_STATUS *status);

/* CSR1 D5 is the FIN1-driven hardware BUSY1 state. */
int rpv130_read_busy1(MVME_INTERFACE *mvme, mvme_addr_t base,
                      bool *busy, uint8_t *csr1);
/* KEK sequence: CLR1, then ENABLE1|MASK1; preserve unrelated ENABLE3. */
int rpv130_clear_busy1_and_rearm(MVME_INTERFACE *mvme, mvme_addr_t base,
                                  uint8_t *csr1);
int rpv130_clear_busy1_and_rearm_timed(MVME_INTERFACE *mvme, mvme_addr_t base,
                                        uint8_t *csr1,
                                        RPV130_BUSY_TIMING *timing);
/* Normal event clear: keep the current channel enables/mask in one CSR1 write. */
int rpv130_clear_busy1_preserving_arm(MVME_INTERFACE *mvme, mvme_addr_t base,
                                      uint8_t *csr1);
int rpv130_clear_busy1_preserving_arm_timed(MVME_INTERFACE *mvme,
                                            mvme_addr_t base, uint8_t *csr1,
                                            RPV130_BUSY_TIMING *timing);
/* Call only while an independent trigger veto is already asserted. */
int rpv130_clear_busy1_and_disable(MVME_INTERFACE *mvme, mvme_addr_t base,
                                    uint8_t *csr1);

#ifdef __cplusplus
}
#endif

#endif
