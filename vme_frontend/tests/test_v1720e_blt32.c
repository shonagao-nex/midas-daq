#include "v1720e.h"

#include <stdint.h>
#include <stdio.h>

static DWORD event_size;
static unsigned single_reads;
static unsigned blt_calls;
static int blt_result;
static int blt_count;
static int requested;
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
              mvme_size_t bytes) {
    DWORD *word = (DWORD *)dst;
    (void)vme;
    if (address != V1720E_BASE_ADDRESS || bytes != sizeof(DWORD))
        return MVME_ACCESS_ERROR;
    switch (single_reads++) {
    case 0: *word = 0xA0000000u | event_size; break;
    case 1: *word = 0xFFu; break;
    case 2: *word = 17u; break;
    case 3: *word = 23u; break;
    default: *word = 0x12345678u; break;
    }
    return MVME_SUCCESS;
}
int mvme_write(MVME_INTERFACE *vme, mvme_addr_t address, void *src,
               mvme_size_t bytes) {
    (void)vme; (void)address; (void)src; (void)bytes;
    return MVME_ACCESS_ERROR;
}

int caenvme_blt_read32(int handle, mvme_addr_t address, void *destination,
                       int requested_bytes, int *actual_bytes) {
    DWORD *words = (DWORD *)destination;
    int i;
    ++blt_calls;
    requested = requested_bytes;
    if (handle != 7 || address != V1720E_BASE_ADDRESS)
        return -1;
    *actual_bytes = blt_count;
    for (i = 0; i < blt_count / 4 && i < requested_bytes / 4; ++i)
        words[i] = 0x12345678u;
    return blt_result;
}

static int check(int condition, const char *message) {
    if (condition) return 1;
    fprintf(stderr, "FAIL: %s\n", message);
    return 0;
}

static void reset(DWORD size, int result, int count) {
    event_size = size;
    single_reads = 0;
    blt_calls = 0;
    blt_result = result;
    blt_count = count;
    requested = -1;
}

int main(void) {
    MVME_INTERFACE vme = {0};
    DWORD data[V1720E_MAX_EVENT_WORDS] = {0};
    V1720E_EVENT_INFO info;
    int status;
    int ok = 1;
    vme.handle = 7;

    reset(1028, 0, 4096);
    status = v1720e_read_event_mode(&vme, V1720E_BASE_ADDRESS, data,
                                    V1720E_MAX_EVENT_WORDS, 1028, 0xFF,
                                    BLT32, &info);
    ok &= check(status == MVME_SUCCESS && single_reads == 4 &&
                    blt_calls == 1 && requested == 4096 &&
                    info.words == 1028 && info.size_valid &&
                    info.channel_mask_valid && info.event_counter == 17 &&
                    info.trigger_time_tag == 23 &&
                    data[4] == 0x12345678u && data[1027] == 0x12345678u,
                "1028-word exact BLT or common event metadata");

    reset(3, 0, 0);
    status = v1720e_read_event_mode(&vme, V1720E_BASE_ADDRESS, data,
                                    V1720E_MAX_EVENT_WORDS, 1028, 0xFF,
                                    BLT32, &info);
    ok &= check(status == MVME_ACCESS_ERROR && single_reads == 4 &&
                    blt_calls == 0, "size below header was read");

    reset(V1720E_MAX_EVENT_WORDS + 1, 0, 0);
    status = v1720e_read_event_mode(&vme, V1720E_BASE_ADDRESS, data,
                                    V1720E_MAX_EVENT_WORDS, 1028, 0xFF,
                                    BLT32, &info);
    ok &= check(status == MVME_ACCESS_ERROR && single_reads == 4 &&
                    blt_calls == 0, "capacity overflow was read");

    reset(1028, 0, 4000);
    status = v1720e_read_event_mode(&vme, V1720E_BASE_ADDRESS, data,
                                    V1720E_MAX_EVENT_WORDS, 1028, 0xFF,
                                    BLT32, &info);
    ok &= check(status == MVME_ACCESS_ERROR && single_reads == 4 &&
                    blt_calls == 1 && info.words == 1004 &&
                    info.blt_status == 0 &&
                    info.blt_requested_bytes == 4096 &&
                    info.blt_actual_bytes == 4000,
                "short BLT retried or accepted");

    reset(1028, -2, 4096);
    status = v1720e_read_event_mode(&vme, V1720E_BASE_ADDRESS, data,
                                    V1720E_MAX_EVENT_WORDS, 1028, 0xFF,
                                    BLT32, &info);
    ok &= check(status == MVME_ACCESS_ERROR && single_reads == 4 &&
                    blt_calls == 1 && info.blt_status == -2 &&
                    info.blt_requested_bytes == 4096 &&
                    info.blt_actual_bytes == 4096,
                "failed BLT retried or accepted");

    reset(1028, 0, 4095);
    status = v1720e_read_event_mode(&vme, V1720E_BASE_ADDRESS, data,
                                    V1720E_MAX_EVENT_WORDS, 1028, 0xFF,
                                    BLT32, &info);
    ok &= check(status == MVME_ACCESS_ERROR && single_reads == 4 &&
                    blt_calls == 1, "non-word-aligned count accepted");

    reset(1030, 0, 0);
    status = v1720e_read_event_mode(&vme, V1720E_BASE_ADDRESS, data,
                                    V1720E_MAX_EVENT_WORDS, 1030, 0xFF,
                                    BLT32, &info);
    ok &= check(status == MVME_SUCCESS && blt_calls == 0 &&
                    single_reads == 1030 && info.words == 1030,
                "oversize BLT did not select D32 before block transfer");

    reset(6, 0, 0);
    status = v1720e_read_event(&vme, V1720E_BASE_ADDRESS, data,
                               V1720E_MAX_EVENT_WORDS, 6, 0xFF, &info);
    ok &= check(status == MVME_SUCCESS && blt_calls == 0 &&
                    single_reads == 6 && info.words == 6,
                "default single D32 path changed");
    ok &= check(current_am == MVME_AM_A24_ND &&
                    current_mode == MVME_DMODE_D16,
                "VME access mode was not restored");
    if (!ok) return 1;
    puts("test_v1720e_blt32: passed");
    return 0;
}
