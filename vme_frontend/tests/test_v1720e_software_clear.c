#include "v1720e.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE 0x11110000u

static DWORD acquisition_control;
static DWORD acquisition_status;
static DWORD event_stored;
static DWORD write_address;
static DWORD write_value;
static unsigned write_count;
static int fail_event_stored_read;
static int leave_event_ready_after_clear;
static int leave_event_stored_after_clear;
static int run_after_clear;
static int current_am = MVME_AM_A24_ND;
static int current_mode = MVME_DMODE_D16;

int caenvme_blt_read32(int handle, mvme_addr_t address, void *destination,
                       int requested_bytes, int *actual_bytes) {
    (void)handle; (void)address; (void)destination;
    (void)requested_bytes; (void)actual_bytes;
    return -1;
}

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
    DWORD value = 0;
    (void)vme;
    if (size != sizeof(value)) return MVME_ACCESS_ERROR;
    if (address == BASE + 0x8100u) value = acquisition_control;
    else if (address == BASE + 0x8104u) value = acquisition_status;
    else if (address == BASE + 0x812Cu) {
        if (fail_event_stored_read) return MVME_ACCESS_ERROR;
        value = event_stored;
    }
    else return MVME_ACCESS_ERROR;
    memcpy(dst, &value, sizeof(value));
    return MVME_SUCCESS;
}
int mvme_write(MVME_INTERFACE *vme, mvme_addr_t address, void *src,
               mvme_size_t size) {
    (void)vme;
    if (size != sizeof(write_value)) return MVME_ACCESS_ERROR;
    memcpy(&write_value, src, sizeof(write_value));
    write_address = address;
    ++write_count;
    if (address == BASE + 0xEF28u) {
        if (!leave_event_stored_after_clear) event_stored = 0;
        if (!leave_event_ready_after_clear) acquisition_status &= ~0x8u;
        if (run_after_clear) acquisition_status |= 0x4u;
    }
    return MVME_SUCCESS;
}

static int require(int condition, const char *message) {
    if (condition) return 1;
    fprintf(stderr, "FAIL: %s\n", message);
    return 0;
}

int main(void) {
    MVME_INTERFACE *mock = (MVME_INTERFACE *)(uintptr_t)1;
    DWORD stored = 99;
    int stored_valid = 0;
    int ok = 1;
    acquisition_control = 0;
    acquisition_status = 0;
    event_stored = 12;
    write_count = 0;
    fail_event_stored_read = 0;
    ok &= require(v1720e_software_clear(mock, BASE, &stored, &stored_valid) ==
                      MVME_SUCCESS,
                  "stopped Software Clear failed");
    ok &= require(write_count == 1 && write_address == BASE + 0xEF28u,
                  "clear wrote anything other than SW_CLEAR 0xEF28");
    ok &= require(write_value == 0 && stored == 0 && stored_valid,
                  "clear value or Event Stored verification is wrong");
    ok &= require(current_am == MVME_AM_A24_ND &&
                      current_mode == MVME_DMODE_D16,
                  "VME access mode was not restored");

    /* RUN start may accept one event; the BOR gate must still reject it. */
    {
        int ready = -1;
        DWORD post_start_stored = 99, post_start_status = 99;
        acquisition_status = 0x184u;
        event_stored = 1;
        ok &= require(v1720e_verify_empty_after_start(
                          mock, BASE, &ready, &post_start_stored,
                          &post_start_status) == MVME_ACCESS_ERROR &&
                          post_start_stored == 1 && ready == 0,
                      "BOR accepted Event Stored=1 after RUN start");
        event_stored = 0;
        ok &= require(v1720e_verify_empty_after_start(
                          mock, BASE, &ready, &post_start_stored,
                          &post_start_status) == MVME_SUCCESS &&
                          post_start_stored == 0 && post_start_status == 0x184u,
                      "BOR rejected an empty RUN-start buffer");
    }

    acquisition_status = 0x8u;
    event_stored = 2;
    leave_event_ready_after_clear = 1;
    ok &= require(v1720e_software_clear(mock, BASE, &stored, &stored_valid) ==
                      MVME_ACCESS_ERROR,
                  "EVENT READY surviving Software Clear was accepted");
    ok &= require(stored_valid && stored == 0,
                  "EVENT READY failure did not report Event Stored");
    leave_event_ready_after_clear = 0;

    acquisition_status = 0;
    event_stored = 3;
    leave_event_stored_after_clear = 1;
    ok &= require(v1720e_software_clear(mock, BASE, &stored, &stored_valid) ==
                      MVME_ACCESS_ERROR && stored_valid && stored == 3,
                  "nonempty Event Stored after clear was accepted");
    leave_event_stored_after_clear = 0;

    acquisition_status = 0;
    run_after_clear = 1;
    ok &= require(v1720e_software_clear(mock, BASE, &stored, &stored_valid) ==
                      MVME_ACCESS_ERROR,
                  "RUN_ACTIVE after clear was accepted");
    run_after_clear = 0;

    acquisition_control = 4;
    acquisition_status = 0;
    event_stored = 7;
    write_count = 0;
    stored_valid = 1;
    ok &= require(v1720e_software_clear(mock, BASE, &stored, &stored_valid) ==
                      MVME_ACCESS_ERROR,
                  "clear was not rejected for Acquisition Control RUN");
    ok &= require(write_count == 0 && event_stored == 7 && !stored_valid,
                  "Control RUN rejection wrote memory or reported Event Stored");

    acquisition_control = 0;
    acquisition_status = 4;
    write_count = 0;
    stored_valid = 1;
    ok &= require(v1720e_software_clear(mock, BASE, &stored, &stored_valid) ==
                      MVME_ACCESS_ERROR,
                  "clear was not rejected for Acquisition Status RUN");
    ok &= require(write_count == 0 && event_stored == 7 && !stored_valid,
                  "Status RUN rejection wrote memory or reported Event Stored");

    acquisition_status = 0;
    event_stored = 5;
    write_count = 0;
    stored = 99;
    stored_valid = 1;
    fail_event_stored_read = 1;
    ok &= require(v1720e_software_clear(mock, BASE, &stored, &stored_valid) ==
                      MVME_ACCESS_ERROR,
                  "Event Stored read failure was not returned");
    ok &= require(write_count == 1 && write_address == BASE + 0xEF28u,
                  "Event Stored read failure changed the clear sequence");
    ok &= require(stored == 99 && !stored_valid,
                  "failed Event Stored read reported a value");
    if (!ok) return 1;
    puts("test_v1720e_software_clear: passed");
    return 0;
}
