#include "v1720e.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE V1720E_BASE_ADDRESS
#define MASK (BASE + 0x8110u)
#define IO (BASE + 0x811Cu)
static DWORD mask_reg, io_reg;
static int am = MVME_AM_A24_ND, mode = MVME_DMODE_D16;
static int fail_read, fail_write, corrupt_readback;
static int reads, writes;

int mvme_get_am(MVME_INTERFACE *v, int *out) { (void)v; *out=am; return MVME_SUCCESS; }
int mvme_set_am(MVME_INTERFACE *v, int x) { (void)v; am=x; return MVME_SUCCESS; }
int mvme_get_dmode(MVME_INTERFACE *v, int *out) { (void)v; *out=mode; return MVME_SUCCESS; }
int mvme_set_dmode(MVME_INTERFACE *v, int x) { (void)v; mode=x; return MVME_SUCCESS; }
int mvme_read(MVME_INTERFACE *v, void *out, mvme_addr_t addr, mvme_size_t size) {
    DWORD value;
    (void)v;
    ++reads;
    if (size != 4 || reads == fail_read || (addr != MASK && addr != IO)) return MVME_ACCESS_ERROR;
    value = addr == MASK ? mask_reg : io_reg;
    if (corrupt_readback && writes &&
        ((corrupt_readback == 1 && addr == MASK) ||
         (corrupt_readback == 2 && addr == IO))) value ^= 1u;
    memcpy(out, &value, 4);
    return MVME_SUCCESS;
}
int mvme_write(MVME_INTERFACE *v, mvme_addr_t addr, void *src, mvme_size_t size) {
    (void)v;
    ++writes;
    if (size != 4 || writes == fail_write || (addr != MASK && addr != IO)) return MVME_ACCESS_ERROR;
    memcpy(addr == MASK ? &mask_reg : &io_reg, src, 4);
    return MVME_SUCCESS;
}
static void reset(void) {
    mask_reg = 0xA5A5A5FFu;
    io_reg = 0x1234FFFFu;
    reads=writes=fail_read=fail_write=corrupt_readback=0;
}
static int check(int yes, const char *message) {
    if (!yes) fprintf(stderr, "FAIL: %s\n", message);
    return yes;
}
int main(void) {
    MVME_INTERFACE *v = (MVME_INTERFACE *)(uintptr_t)1;
    DWORD mask=0, io=0;
    int ok=1;
    reset();
    ok &= check(v1720e_configure_trg_out(v, BASE, &mask, &io) == MVME_SUCCESS,
                "normal configuration");
    ok &= check(mask == 0x65A5A500u && mask_reg == mask,
                "external-only mask and reserved bits");
    ok &= check(io == (0x1234FFFFu & ~0x8003u) && io_reg == io,
                "NIM, enabled, normal and unrelated bits");
    ok &= check(writes == 2 && reads == 4, "two writes and readbacks");
    reset(); fail_write=1;
    ok &= check(v1720e_configure_trg_out(v, BASE, &mask, &io) != MVME_SUCCESS,
                "write failure");
    reset(); fail_read=3;
    ok &= check(v1720e_configure_trg_out(v, BASE, &mask, &io) != MVME_SUCCESS,
                "mask readback failure");
    reset(); corrupt_readback=1;
    ok &= check(v1720e_configure_trg_out(v, BASE, &mask, &io) != MVME_SUCCESS,
                "mask readback mismatch");
    reset(); corrupt_readback=2;
    ok &= check(v1720e_configure_trg_out(v, BASE, &mask, &io) != MVME_SUCCESS,
                "I/O readback mismatch");
    reset(); fail_read=4;
    ok &= check(v1720e_configure_trg_out(v, BASE, &mask, &io) != MVME_SUCCESS,
                "I/O readback communication failure");
    ok &= check(am == MVME_AM_A24_ND && mode == MVME_DMODE_D16,
                "VME modes restored");
    if (!ok) return 1;
    puts("test_v1720e_trg_out: passed");
    return 0;
}
