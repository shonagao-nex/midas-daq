#include "V775Decoder.h"

#include "DecodedEvent.h"
#include "midasio.h"

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

bool V775Decoder::Decode(TMEvent& event, DecodedEvent& output) const {
  // The current VME frontend publishes V775 as TDC1; TDC0 is V1190.
  TMBank* bank = event.FindBank("TDC1");
  if (!bank || bank->type != TID_DWORD || bank->data_size < 2 * sizeof(std::uint32_t) ||
      bank->data_size % sizeof(std::uint32_t) != 0)
    return false;

  const char* data = event.GetBankData(bank);
  if (!data) return false;
  const std::size_t words = bank->data_size / sizeof(std::uint32_t);
  const std::uint32_t header = ReadWord(data, 0);
  const std::uint32_t trailer = ReadWord(data, words - 1);
  if (((header >> 24) & 0x7u) != 2u || ((trailer >> 24) & 0x7u) != 4u)
    return false;
  if (((header >> 8) & 0x3fu) != words - 2) return false;

  for (std::size_t i = 1; i + 1 < words; ++i) {
    const std::uint32_t word = ReadWord(data, i);
    if (((word >> 24) & 0x7u) != 0u) return false;
    const std::size_t channel = (word >> 16) & 0x1fu;
    output.v775.tdc0[channel] = static_cast<std::int32_t>(word & 0xfffu);
  }
  output.counters.v775 = trailer & 0x00ffffffu;
  return true;
}

}  // namespace ana

