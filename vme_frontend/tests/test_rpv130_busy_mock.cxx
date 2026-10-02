#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "rpv130.h"

namespace {
uint8_t csr1 = 0;
int reads = 0;
std::vector<uint8_t> writes;

void reset(uint8_t value) {
    csr1 = value;
    reads = 0;
    writes.clear();
}
}

extern "C" int mvme_get_am(MVME_INTERFACE *vme, int *am) {
    *am = vme->am;
    return MVME_SUCCESS;
}
extern "C" int mvme_get_dmode(MVME_INTERFACE *vme, int *mode) {
    *mode = vme->dmode;
    return MVME_SUCCESS;
}
extern "C" int mvme_set_am(MVME_INTERFACE *vme, int am) {
    vme->am = am;
    return MVME_SUCCESS;
}
extern "C" int mvme_set_dmode(MVME_INTERFACE *vme, int mode) {
    vme->dmode = mode;
    return MVME_SUCCESS;
}
extern "C" int mvme_read(MVME_INTERFACE *, void *dst,
                          mvme_addr_t address, mvme_size_t count) {
    assert(address == RPV130_BASE_ADDRESS + 0x0C && count == 2);
    ++reads;
    const uint16_t value = csr1;
    std::memcpy(dst, &value, sizeof(value));
    return MVME_SUCCESS;
}
extern "C" int mvme_write(MVME_INTERFACE *, mvme_addr_t address,
                           void *src, mvme_size_t count) {
    assert(address == RPV130_BASE_ADDRESS + 0x0C && count == 2);
    uint16_t value = 0;
    std::memcpy(&value, src, sizeof(value));
    writes.push_back(static_cast<uint8_t>(value));
    if (value & RPV130_CSR1_CLR1) csr1 &= ~RPV130_CSR1_BUSY1;
    csr1 = (csr1 & RPV130_CSR1_BUSY1) |
           (value & (RPV130_CSR1_ENABLE3 | RPV130_CSR1_CHANNEL1_ARMED));
    return MVME_SUCCESS;
}

int main() {
    static_assert(RPV130_CSR1_CLR1 == 0x02, "CLR1 bit");
    static_assert(RPV130_CSR1_MASK1 == 0x08, "MASK1 bit");
    static_assert(RPV130_CSR1_ENABLE1 == 0x10, "ENABLE1 bit");
    static_assert(RPV130_CSR1_BUSY1 == 0x20, "BUSY1 bit");
    static_assert(RPV130_CSR1_CHANNEL1_ARMED == 0x18, "arm bits");

    MVME_INTERFACE vme = {};
    vme.am = MVME_AM_A24_ND;
    vme.dmode = MVME_DMODE_D32;
    uint8_t raw = 0;
    bool busy = false;
    reset(RPV130_CSR1_BUSY1 | RPV130_CSR1_CHANNEL1_ARMED);
    assert(rpv130_read_busy1(&vme, RPV130_BASE_ADDRESS, &busy, &raw)
           == MVME_SUCCESS && busy && raw == 0x38);
    assert(writes.empty() && reads == 1);
    assert(rpv130_clear_busy1_and_rearm(&vme, RPV130_BASE_ADDRESS, &raw)
           == MVME_SUCCESS);
    assert(writes == std::vector<uint8_t>({0x02, 0x18}));
    assert(raw == 0x18 && vme.am == MVME_AM_A24_ND &&
           vme.dmode == MVME_DMODE_D32);
    assert(rpv130_read_busy1(&vme, RPV130_BASE_ADDRESS, &busy, &raw)
           == MVME_SUCCESS && !busy);

    // Normal event clear keeps MASK1/ENABLE1 in its only write. Read-only
    // BUSY1/BUSY3 status must not be echoed into the write value.
    reset(0x80 | RPV130_CSR1_BUSY1 | RPV130_CSR1_ENABLE3 |
          RPV130_CSR1_CHANNEL1_ARMED);
    assert(rpv130_clear_busy1_preserving_arm(
               &vme, RPV130_BASE_ADDRESS, &raw) == MVME_SUCCESS);
    assert(writes == std::vector<uint8_t>({0x5a}));
    assert(raw == 0x58 && reads == 2);
    assert(vme.am == MVME_AM_A24_ND && vme.dmode == MVME_DMODE_D32);

    reset(RPV130_CSR1_BUSY1 | RPV130_CSR1_CHANNEL1_ARMED);
    RPV130_BUSY_TIMING event_timing = {};
    assert(rpv130_clear_busy1_preserving_arm_timed(
               &vme, RPV130_BASE_ADDRESS, &raw, &event_timing)
           == MVME_SUCCESS);
    assert(writes == std::vector<uint8_t>({0x1a}));
    assert(raw == 0x18 && event_timing.clr1_before_ns > 0 &&
           event_timing.clr1_before_ns <= event_timing.clr1_after_ns &&
           event_timing.rearm_after_ns == 0);

    reset(RPV130_CSR1_BUSY1 | RPV130_CSR1_ENABLE1);
    assert(rpv130_clear_busy1_preserving_arm(
               &vme, RPV130_BASE_ADDRESS, &raw) == MVME_ACCESS_ERROR);
    assert(writes.empty());

    reset(RPV130_CSR1_BUSY1 | RPV130_CSR1_CHANNEL1_ARMED);
    RPV130_BUSY_TIMING timing = {};
    assert(rpv130_clear_busy1_and_rearm_timed(
               &vme, RPV130_BASE_ADDRESS, &raw, &timing) == MVME_SUCCESS);
    assert(writes == std::vector<uint8_t>({0x02, 0x18}));
    assert(timing.clr1_before_ns > 0 &&
           timing.clr1_before_ns <= timing.clr1_after_ns &&
           timing.clr1_after_ns <= timing.rearm_after_ns);

    reset(RPV130_CSR1_BUSY1 | RPV130_CSR1_ENABLE3 |
          RPV130_CSR1_CHANNEL1_ARMED);
    assert(rpv130_clear_busy1_and_disable(&vme, RPV130_BASE_ADDRESS, &raw)
           == MVME_SUCCESS);
    assert(writes == std::vector<uint8_t>({0x42, 0x40}));
    assert(raw == 0x40);

    // Frontend contract: the false setting exits before any CSR1 write path.
    std::ifstream input("fevme.cxx");
    const std::string source{std::istreambuf_iterator<char>(input), {}};
    const auto arm = source.find("static bool arm_rpv130_single_event_busy()");
    const auto gate = source.find("if (!gVmeState.single_event_busy_enabled_for_run) return true;", arm);
    const auto write = source.find("rpv130_clear_busy1_and_rearm(", arm);
    assert(arm != std::string::npos && gate != std::string::npos &&
           gate < write);
    const auto event = source.find("INT read_vme_event(");
    const auto event_gate = source.find("if (gVmeState.single_event_busy_enabled_for_run)", event);
    const auto event_write = source.find("rpv130_clear_busy1_preserving_arm(", event);
    const auto event_timed_write = source.find(
        "rpv130_clear_busy1_preserving_arm_timed(", event);
    assert(event != std::string::npos && event_gate < event_write &&
           event_gate < event_timed_write);
    std::puts("test_rpv130_busy_mock: passed");
}
