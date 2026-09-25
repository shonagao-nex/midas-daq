#include "v1720e.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE V1720E_BASE_ADDRESS
#define ACQ_CONTROL (BASE + 0x8100u)
#define ACQ_STATUS  (BASE + 0x8104u)

static DWORD acquisition_control;
static DWORD acquisition_status;
static unsigned write_count;
static int fail_write;
static int fail_control_readback;
static int keep_run_active;
static int current_am = MVME_AM_A24_ND;
static int current_mode = MVME_DMODE_D16;

int mvme_get_am(MVME_INTERFACE *vme, int *am) {
    (void)vme; *am = current_am; return MVME_SUCCESS;
}
int mvme_set_am(MVME_INTERFACE *vme, int am) {
    (void)vme; current_am = am; return MVME_SUCCESS;
}
int mvme_get_dmode(MVME_INTERFACE *vme, int *mode) {
    (void)vme; *mode = current_mode; return MVME_SUCCESS;
}
int mvme_set_dmode(MVME_INTERFACE *vme, int mode) {
    (void)vme; current_mode = mode; return MVME_SUCCESS;
}
int mvme_read(MVME_INTERFACE *vme, void *dst, mvme_addr_t address,
              mvme_size_t size) {
    DWORD value;
    (void)vme;
    if (size != sizeof(value)) return MVME_ACCESS_ERROR;
    if (address == ACQ_CONTROL) {
        if (fail_control_readback && write_count) return MVME_ACCESS_ERROR;
        value = acquisition_control;
    } else if (address == ACQ_STATUS) {
        value = acquisition_status;
    } else {
        return MVME_ACCESS_ERROR;
    }
    memcpy(dst, &value, sizeof(value));
    return MVME_SUCCESS;
}
int mvme_write(MVME_INTERFACE *vme, mvme_addr_t address, void *src,
               mvme_size_t size) {
    DWORD value;
    (void)vme;
    if (address != ACQ_CONTROL || size != sizeof(value))
        return MVME_ACCESS_ERROR;
    ++write_count;
    if (fail_write) return MVME_ACCESS_ERROR;
    memcpy(&value, src, sizeof(value));
    acquisition_control = value;
    if (!keep_run_active && !(value & 4u)) acquisition_status &= ~4u;
    return MVME_SUCCESS;
}

static int require(int condition, const char *message) {
    if (condition) return 1;
    fprintf(stderr, "FAIL: %s\n", message);
    return 0;
}

static void reset_mock(DWORD control, DWORD status) {
    acquisition_control = control;
    acquisition_status = status;
    write_count = 0;
    fail_write = 0;
    fail_control_readback = 0;
    keep_run_active = 0;
}

int main(void) {
    MVME_INTERFACE *mock = (MVME_INTERFACE *)(uintptr_t)1;
    DWORD control = 0, status = 0;
    int attempted = -1;
    int ok = 1;

    /* Models fevme flags=false after restart: hardware state alone triggers stop. */
    reset_mock(4u, 0x184u);
    ok &= require(v1720e_stop_if_running(mock, BASE, &control, &status,
                                         &attempted) == MVME_SUCCESS,
                  "untracked hardware RUN was not stopped");
    ok &= require(attempted == 1 && write_count == 1 &&
                      control == 0 && status == 0x180u,
                  "RUN stop or both-bit readback incorrect");

    reset_mock(0u, 0x184u);
    ok &= require(v1720e_stop_if_running(mock, BASE, &control, &status,
                                         &attempted) == MVME_SUCCESS &&
                      attempted == 1 && write_count == 1 && status == 0x180u,
                  "RUN_ACTIVE alone did not trigger stop");

    reset_mock(4u, 0x180u);
    ok &= require(v1720e_stop_if_running(mock, BASE, &control, &status,
                                         &attempted) == MVME_SUCCESS &&
                      attempted == 1 && write_count == 1 && control == 0,
                  "RUN_REQUEST alone did not trigger stop");

    reset_mock(0u, 0x180u);
    attempted = -1;
    ok &= require(v1720e_stop_if_running(mock, BASE, &control, &status,
                                         &attempted) == MVME_SUCCESS,
                  "already stopped hardware was rejected");
    ok &= require(attempted == 0 && write_count == 0,
                  "already stopped hardware was written");

    reset_mock(4u, 0x184u);
    fail_write = 1;
    ok &= require(v1720e_stop_if_running(mock, BASE, &control, &status,
                                         &attempted) == MVME_ACCESS_ERROR,
                  "stop write failure was hidden");
    ok &= require(attempted == 1 && write_count == 1 &&
                      acquisition_control == 4u,
                  "write failure did not preserve RUN state");

    reset_mock(4u, 0x184u);
    fail_control_readback = 1;
    ok &= require(v1720e_stop_if_running(mock, BASE, &control, &status,
                                         &attempted) == MVME_ACCESS_ERROR,
                  "stop control readback failure was hidden");
    ok &= require(attempted == 1 && write_count == 1,
                  "readback failure changed stop write count");

    reset_mock(4u, 0x184u);
    keep_run_active = 1;
    ok &= require(v1720e_stop_if_running(mock, BASE, &control, &status,
                                         &attempted) == MVME_ACCESS_ERROR,
                  "RUN_ACTIVE remaining high was accepted");
    ok &= require(attempted == 1 && write_count == 1,
                  "RUN_ACTIVE failure changed stop write count");

    ok &= require(current_am == MVME_AM_A24_ND &&
                      current_mode == MVME_DMODE_D16,
                  "VME address/data modes were not restored");
    if (!ok) return 1;
    puts("test_v1720e_stop_recovery: 13 checks passed");
    return 0;
}
