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
int read_calls = 0;
int fail_read_call = 0;
int fail_write_call = 0;
bool keep_busy = false;
bool drop_enable3 = false;
std::vector<uint8_t> writes;
std::vector<char> csr1_accesses;

void reset(uint8_t value) {
    csr1 = value;
    reads = 0;
    read_calls = 0;
    fail_read_call = 0;
    fail_write_call = 0;
    keep_busy = false;
    drop_enable3 = false;
    writes.clear();
    csr1_accesses.clear();
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
    assert(address >= RPV130_BASE_ADDRESS &&
           address <= RPV130_BASE_ADDRESS + 0x0E && count == 2);
    if (++read_calls == fail_read_call) return MVME_ACCESS_ERROR;
    const bool is_csr1 = address == RPV130_BASE_ADDRESS + 0x0C;
    if (is_csr1) {
        ++reads;
        csr1_accesses.push_back('R');
    }
    const uint16_t value = is_csr1 ? csr1 : 0;
    std::memcpy(dst, &value, sizeof(value));
    return MVME_SUCCESS;
}
extern "C" int mvme_write(MVME_INTERFACE *, mvme_addr_t address,
                           void *src, mvme_size_t count) {
    assert(address == RPV130_BASE_ADDRESS + 0x0C && count == 2);
    uint16_t value = 0;
    std::memcpy(&value, src, sizeof(value));
    writes.push_back(static_cast<uint8_t>(value));
    csr1_accesses.push_back('W');
    if (static_cast<int>(writes.size()) == fail_write_call)
        return MVME_ACCESS_ERROR;
    if ((value & RPV130_CSR1_CLR1) && !keep_busy)
        csr1 &= ~RPV130_CSR1_BUSY1;
    csr1 = (csr1 & RPV130_CSR1_BUSY1) |
           (value & (RPV130_CSR1_ENABLE3 | RPV130_CSR1_CHANNEL1_ARMED));
    if (drop_enable3) csr1 &= ~RPV130_CSR1_ENABLE3;
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
    assert(rpv130_clear_busy1_preserving_enable_state(
               &vme, RPV130_BASE_ADDRESS, &raw) == MVME_SUCCESS);
    assert(writes == std::vector<uint8_t>({0x5a}));
    assert(raw == 0x58 && reads == 2);
    assert(vme.am == MVME_AM_A24_ND && vme.dmode == MVME_DMODE_D32);
    const auto normal_accesses = csr1_accesses;
    assert(normal_accesses == std::vector<char>({'R', 'W', 'R'}));

    reset(0x80 | RPV130_CSR1_BUSY1 | RPV130_CSR1_ENABLE3 |
          RPV130_CSR1_CHANNEL1_ARMED);
    RPV130_BUSY_TIMING event_timing = {};
    assert(rpv130_clear_busy1_preserving_enable_state_timed(
               &vme, RPV130_BASE_ADDRESS, &raw, &event_timing)
           == MVME_SUCCESS);
    assert(writes == std::vector<uint8_t>({0x5a}) &&
           csr1_accesses == normal_accesses && reads == 2);
    assert(raw == 0x58 && event_timing.clr1_before_ns > 0 &&
           event_timing.clr1_before_ns <= event_timing.clr1_after_ns);
    assert(vme.am == MVME_AM_A24_ND && vme.dmode == MVME_DMODE_D32);

    reset(RPV130_CSR1_BUSY1 | RPV130_CSR1_ENABLE1);
    assert(rpv130_clear_busy1_preserving_enable_state(
               &vme, RPV130_BASE_ADDRESS, &raw) == MVME_ACCESS_ERROR);
    assert(writes.empty());

    reset(RPV130_CSR1_BUSY1 | RPV130_CSR1_ENABLE3 |
          RPV130_CSR1_CHANNEL1_ARMED);
    assert(rpv130_clear_busy1_and_disable(&vme, RPV130_BASE_ADDRESS, &raw)
           == MVME_SUCCESS);
    assert(writes == std::vector<uint8_t>({0x42, 0x40}));
    assert(raw == 0x40);

    // Cleanup must also work when a new process has no record of the old arm.
    reset(RPV130_CSR1_BUSY1 | RPV130_CSR1_CHANNEL1_ARMED);
    assert(rpv130_clear_busy1_and_disable(&vme, RPV130_BASE_ADDRESS, &raw)
           == MVME_SUCCESS);
    assert(writes == std::vector<uint8_t>({0x02, 0x00}));
    assert(raw == 0);

    RPV130_STATUS before = {};
    reset(RPV130_CSR1_BUSY1 | RPV130_CSR1_ENABLE3 |
          RPV130_CSR1_CHANNEL1_ARMED);
    assert(rpv130_recover_stopped(&vme, RPV130_BASE_ADDRESS, &before, &raw)
           == MVME_SUCCESS);
    assert(before.csr1 == 0x78 && raw == 0x40 &&
           writes == std::vector<uint8_t>({0x42, 0x40}));

    reset(0x38);
    fail_read_call = 1;
    assert(rpv130_recover_stopped(&vme, RPV130_BASE_ADDRESS, &before, &raw)
           == MVME_ACCESS_ERROR && writes.empty());

    reset(0x38);
    fail_read_call = 7; // CSR1 read inside the two-write helper.
    assert(rpv130_recover_stopped(&vme, RPV130_BASE_ADDRESS, &before, &raw)
           == MVME_ACCESS_ERROR && writes.empty());

    for (int failed_write = 1; failed_write <= 2; ++failed_write) {
        reset(0x38);
        fail_write_call = failed_write;
        assert(rpv130_recover_stopped(&vme, RPV130_BASE_ADDRESS,
                                       &before, &raw) == MVME_ACCESS_ERROR);
        assert(static_cast<int>(writes.size()) == failed_write);
    }

    reset(0x38);
    fail_read_call = 8; // CSR1 readback after both writes.
    assert(rpv130_recover_stopped(&vme, RPV130_BASE_ADDRESS, &before, &raw)
           == MVME_ACCESS_ERROR && writes.size() == 2);

    reset(0x38);
    keep_busy = true;
    assert(rpv130_recover_stopped(&vme, RPV130_BASE_ADDRESS, &before, &raw)
           == MVME_ACCESS_ERROR && (csr1 & RPV130_CSR1_BUSY1));

    reset(0x78);
    drop_enable3 = true;
    assert(rpv130_recover_stopped(&vme, RPV130_BASE_ADDRESS, &before, &raw)
           == MVME_ACCESS_ERROR && !(csr1 & RPV130_CSR1_ENABLE3));

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
    const auto event_write = source.find("rpv130_clear_busy1_preserving_enable_state(", event);
    assert(event != std::string::npos && event_gate < event_write &&
           source.find("rpv130_clear_busy1_preserving_enable_state_timed(", event)
               == std::string::npos);
    const auto recovery = source.find("static bool recover_rpv130_stopped_state(");
    const auto recovery_end = source.find("\n}", recovery);
    const auto recovery_body = source.substr(recovery, recovery_end - recovery);
    assert(recovery != std::string::npos && recovery_end != std::string::npos);
    assert(recovery_body.find("rpv130_recover_stopped(") != std::string::npos);
    assert(recovery_body.find("single_event_busy_enabled_for_run") == std::string::npos);
    const auto startup_recovery = source.find(
        "recover_rpv130_stopped_state(\"STOPPED frontend startup\")");
    const auto bor_recovery = source.find(
        "recover_rpv130_stopped_state(\"BOR preparation\")");
    assert(startup_recovery != std::string::npos &&
           bor_recovery != std::string::npos);
    assert(source.substr(startup_recovery - 90, 90).find(
               "gVmeState.rpv130_enabled_for_run") != std::string::npos);
    assert(source.substr(bor_recovery - 90, 90).find(
               "gVmeState.rpv130_enabled_for_run") != std::string::npos);
    std::puts("test_rpv130_busy_mock: passed");
}
