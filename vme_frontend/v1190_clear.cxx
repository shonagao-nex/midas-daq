#include "v1190_clear.h"

namespace v1190_clear {

constexpr DWORD kSoftwareClear = 0x1016;
constexpr DWORD kStatus = 0x1002;
constexpr WORD kDataReady = 0x0001;

// Issue the V1190 Software Clear with the existing D16 access callback.
bool software_clear(const Access &access, const char *description)
{
  return access.write16(access.vme, access.base + kSoftwareClear, 0, description);
}

// Read Status after clear and report whether DataReady remains asserted.
CheckResult verify_clear(const Access &access, const char *description)
{
  WORD status = 0;
  if (!access.read16(access.vme, access.base + kStatus, status, description)) return CheckResult::ReadFailed;
  return status & kDataReady ? CheckResult::DataReady : CheckResult::Empty;
}

}
