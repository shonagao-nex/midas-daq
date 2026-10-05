#include "v1190_clear.h"

#include <cassert>
#include <string>
#include <vector>

namespace {
struct Call { bool write; DWORD address; WORD value; std::string description; };
std::vector<Call> calls;
bool write_ok;
bool read_ok;
WORD status_value;

bool write16(MVME_INTERFACE *, DWORD address, WORD value, const char *description)
{
    calls.push_back({true, address, value, description});
    return write_ok;
}
bool read16(MVME_INTERFACE *, DWORD address, WORD &value, const char *description)
{
    calls.push_back({false, address, 0, description});
    if (!read_ok) return false;
    value = status_value;
    return true;
}
void reset(bool write_success, bool read_success, WORD status)
{
    calls.clear();
    write_ok = write_success;
    read_ok = read_success;
    status_value = status;
}
}

int main()
{
    const DWORD base = 0x12340000;
    const v1190_clear::Access access = {nullptr, base, read16, write16};
    reset(true, true, 0x0002);
    assert(v1190_clear::software_clear(access, "V1190 Software Clear"));
    assert(v1190_clear::verify_clear(access, "V1190 Status after clear") == v1190_clear::CheckResult::Empty);
    assert(calls.size() == 2 && calls[0].write && calls[0].address == base + 0x1016 && calls[0].value == 0);
    assert(!calls[1].write && calls[1].address == base + 0x1002);
    assert(calls[0].description == "V1190 Software Clear" && calls[1].description == "V1190 Status after clear");

    reset(false, true, 0);
    assert(!v1190_clear::software_clear(access, "V1190 manual Software Clear"));
    assert(calls.size() == 1);

    reset(true, false, 0);
    assert(v1190_clear::software_clear(access, "V1190 manual Software Clear"));
    assert(v1190_clear::verify_clear(access, "V1190 Status after manual Software Clear") == v1190_clear::CheckResult::ReadFailed);
    assert(calls.size() == 2);

    reset(true, true, 0x0001);
    assert(v1190_clear::software_clear(access, "V1190 manual Software Clear"));
    assert(v1190_clear::verify_clear(access, "V1190 Status after manual Software Clear") == v1190_clear::CheckResult::DataReady);
    assert(calls.size() == 2 && calls[0].description == "V1190 manual Software Clear" &&
           calls[1].description == "V1190 Status after manual Software Clear");
}
