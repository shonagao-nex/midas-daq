#ifndef V7XX_CHANNEL_MASK_H
#define V7XX_CHANNEL_MASK_H

#include "midas.h"

namespace v7xx_channel_mask {

constexpr WORD kKillBit = 0x0100;
constexpr WORD kThresholdValueMask = 0x00FF;

template <typename Access>
bool apply(const Access &access, DWORD offset, DWORD enabled_mask, const char *description)
{
  for (unsigned ch = 0; ch < 32; ++ch) {
    const DWORD address = access.base + offset + 2 * ch;
    WORD current = 0;
    if (!access.read16(access.vme, address, current, description))
      return false;

    const WORD kill = (enabled_mask & (DWORD{1} << ch)) ? 0 : kKillBit;
    const WORD desired = (current & kThresholdValueMask) | kill;
    if ((current & kKillBit) != kill &&
        !access.write16(access.vme, address, desired, description))
      return false;
  }
  return true;
}

template <typename Access>
bool readback(const Access &access, DWORD offset, WORD (&thresholds)[32],
              const char *description)
{
  for (unsigned ch = 0; ch < 32; ++ch) {
    if (!access.read16(access.vme, access.base + offset + 2 * ch,
                       thresholds[ch], description))
      return false;
    thresholds[ch] &= kThresholdValueMask | kKillBit;
  }
  return true;
}

inline DWORD enabled_mask(const WORD (&thresholds)[32])
{
  DWORD mask = 0;
  for (unsigned ch = 0; ch < 32; ++ch) {
    if (!(thresholds[ch] & kKillBit))
      mask |= DWORD{1} << ch;
  }
  return mask;
}

}

#endif
