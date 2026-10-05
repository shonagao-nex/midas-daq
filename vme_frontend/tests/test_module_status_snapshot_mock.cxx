#include "../module_status_snapshot.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
MVME_INTERFACE *const kVme = reinterpret_cast<MVME_INTERFACE *>(1);
constexpr DWORD kV792 = 0x600000, kV775 = 0, kV1190 = 0xc10000;
constexpr DWORD kV792S1 = 0x100e, kV792S2 = 0x1022;
constexpr DWORD kV775S1 = 0x100e, kV775S2 = 0x1022;
constexpr DWORD kV1190Status = 0x1002, kV1190Stored = 0x1020, kV1190Counter = 0x101c;
std::vector<std::string> calls;
std::vector<DWORD> addresses;
int fail_at = -1;
void reset() { calls.clear(); addresses.clear(); fail_at = -1; }
bool read16(MVME_INTERFACE *vme, DWORD address, WORD &value, const char *label)
{
  assert(vme == kVme);
  calls.emplace_back(label); addresses.push_back(address);
  if (static_cast<int>(calls.size()) == fail_at) return false;
  value = static_cast<WORD>(0x10 + calls.size());
  return true;
}
bool read32(MVME_INTERFACE *vme, DWORD address, DWORD &value, const char *label)
{
  assert(vme == kVme);
  calls.emplace_back(label); addresses.push_back(address);
  if (static_cast<int>(calls.size()) == fail_at) return false;
  value = 0x123456;
  return true;
}
void counter792(MVME_INTERFACE *vme, DWORD base, DWORD *value)
{
  assert(vme == kVme && base == kV792);
  calls.emplace_back("V792 counter"); addresses.push_back(base);
  *value = 792;
}
void counter775(MVME_INTERFACE *vme, DWORD base, DWORD *value)
{
  assert(vme == kVme && base == kV775);
  calls.emplace_back("V775 counter"); addresses.push_back(base);
  *value = 775;
}
void check_v7xx(DWORD base, DWORD s1, DWORD s2, module_status_snapshot::ReadCounter counter, const char *name, DWORD expected)
{
  module_status_snapshot::V7xxSnapshot snapshot = {0, 0, 99};
  const std::string first = std::string(name) + " status1", second = std::string(name) + " status2";
  reset();
  assert(module_status_snapshot::read_v7xx(kVme, base, s1, s2, first.c_str(), second.c_str(), read16, counter, snapshot));
  assert(snapshot.status1 == 0x11 && snapshot.status2 == 0x12 && snapshot.counter == expected);
  assert((addresses == std::vector<DWORD>{base + s1, base + s2, base}));
  assert((calls == std::vector<std::string>{first, second, std::string(name) + " counter"}));
  for (int failure : {1, 2}) {
    reset(); fail_at = failure; snapshot = {0, 0, 99};
    assert(!module_status_snapshot::read_v7xx(kVme, base, s1, s2, first.c_str(), second.c_str(), read16, counter, snapshot));
    assert(calls.size() == static_cast<size_t>(failure) && snapshot.counter == 99);
  }
}
void check_v1190()
{
  module_status_snapshot::V1190Snapshot snapshot = {0, 0, 99};
  reset();
  assert(module_status_snapshot::read_v1190(kVme, kV1190, kV1190Status, kV1190Stored, kV1190Counter,
                                           "V1190 status", "V1190 stored", "V1190 counter", read16, read32, snapshot));
  assert(snapshot.status == 0x11 && snapshot.stored == 0x12 && snapshot.counter == 0x123456);
  assert((addresses == std::vector<DWORD>{kV1190 + kV1190Status, kV1190 + kV1190Stored, kV1190 + kV1190Counter}));
  assert((calls == std::vector<std::string>{"V1190 status", "V1190 stored", "V1190 counter"}));
  for (int failure : {1, 2, 3}) {
    reset(); fail_at = failure; snapshot = {0, 0, 99};
    assert(!module_status_snapshot::read_v1190(kVme, kV1190, kV1190Status, kV1190Stored, kV1190Counter,
                                                "V1190 status", "V1190 stored", "V1190 counter", read16, read32, snapshot));
    assert(calls.size() == static_cast<size_t>(failure));
  }
}
void check_both_callers()
{
  std::ifstream file("fevme.cxx");
  assert(file);
  const std::string source(std::istreambuf_iterator<char>{file}, {});
  const auto eor_start = source.find("static void refresh_enabled_module_variables()");
  const auto loop_start = source.find("INT frontend_loop()");
  const auto loop_end = source.find("INT poll_event(", loop_start);
  assert(eor_start != std::string::npos && loop_start != std::string::npos && loop_end != std::string::npos);
  const std::string eor = source.substr(eor_start, loop_start - eor_start);
  const std::string loop = source.substr(loop_start, loop_end - loop_start);
  for (const auto &part : {eor, loop}) {
    assert(part.find("module_status_snapshot::read_v7xx(gVme, V792_BASE") != std::string::npos);
    assert(part.find("module_status_snapshot::read_v7xx(gVme, V775_BASE") != std::string::npos);
    assert(part.find("module_status_snapshot::read_v1190(gVme, V1190_BASE") != std::string::npos);
  }
}
}
int main()
{
  check_v7xx(kV792, kV792S1, kV792S2, counter792, "V792", 792);
  check_v7xx(kV775, kV775S1, kV775S2, counter775, "V775", 775);
  check_v1190();
  check_both_callers();
  std::puts("module status snapshot mock tests passed");
}
