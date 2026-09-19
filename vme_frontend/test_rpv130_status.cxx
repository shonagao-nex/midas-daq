#include <stdio.h>

#include "caenvme.h"
#include "rpv130.h"

int main()
{
    MVME_INTERFACE *mvme = NULL;
    int result = mvme_open(&mvme, 0);
    if (result != MVME_SUCCESS) {
        fprintf(stderr, "Cannot open VME interface: status %d\n", result);
        return 1;
    }

    RPV130_STATUS status = {};
    result = rpv130_read_status(mvme, RPV130_BASE_ADDRESS, &status);
    if (result == MVME_SUCCESS) {
        printf("RPV130 read-only status (A16 non-privileged, D16):\n"
               "  0x8FF0 LATCH1  = 0x%02X (%u)\n"
               "  0x8FF2 LATCH2  = 0x%02X (%u)\n"
               "  0x8FF4 R/S FF  = 0x%02X (%u)\n"
               "  0x8FF6 THROUGH = 0x%02X (%u)\n"
               "  0x8FFC CSR1    = 0x%02X (%u)\n"
               "  0x8FFE CSR2    = 0x%02X (%u)\n"
               "  Communication OK = true\n",
               status.latch1, status.latch1, status.latch2, status.latch2,
               status.rsff, status.rsff, status.through, status.through,
               status.csr1, status.csr1, status.csr2, status.csr2);
    } else {
        fprintf(stderr, "RPV130 read failed: status %d\n"
                        "Communication OK = false\n", result);
    }

    const int close_result = mvme_close(mvme);
    if (close_result != MVME_SUCCESS) {
        fprintf(stderr, "Cannot close VME interface: status %d\n", close_result);
        return 1;
    }
    return result == MVME_SUCCESS ? 0 : 1;
}
