#include "V1190Decoder.h"

#include "DecodedEvent.h"
#include "midasio.h"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace {
std::uint32_t ReadWord(const char* data, std::size_t index) {
  std::uint32_t value = 0;
  std::memcpy(&value, data + index * sizeof(value), sizeof(value));
  return value;
}
}  // namespace

namespace ana {

bool V1190Decoder::Decode(TMEvent& event, DecodedEvent& output) const {
  assert(output.v1190.tle0.size() == 128);
  assert(output.v1190.ttr0.size() == 128);
  TMBank* bank = event.FindBank("TDC0");
  if (!bank || bank->type != TID_DWORD || bank->data_size < 2 * sizeof(std::uint32_t) ||
      bank->data_size % sizeof(std::uint32_t) != 0)
    return false;

  const char* data = event.GetBankData(bank);
  if (!data) return false;
  const std::size_t words = bank->data_size / sizeof(std::uint32_t);
  const std::uint32_t global_header = ReadWord(data, 0);
  if (((global_header >> 27) & 0x1fu) != 0x08u) return false;

  bool found_trailer = false;
  for (std::size_t i = 1; i < words; ++i) {
    const std::uint32_t word = ReadWord(data, i);
    const std::uint32_t type = (word >> 27) & 0x1fu;
    if (type == 0x00u) {
      const std::size_t channel = (word >> 19) & 0x7fu;
      const auto value = static_cast<std::int32_t>(word & 0x7ffffu);
      if ((word & 0x04000000u) == 0)
        output.v1190.tle0[channel].push_back(value);
      else
        output.v1190.ttr0[channel].push_back(value);
    } else if (type == 0x10u) {
      if (((word >> 5) & 0xffffu) != words) return false;
      found_trailer = true;
    } else if (type != 0x01u && type != 0x03u && type != 0x04u &&
               type != 0x11u) {
      return false;
    }
  }
  if (!found_trailer) return false;
  output.counters.v1190 = (global_header >> 5) & 0x003fffffu;
  return true;
}

}  // namespace ana
