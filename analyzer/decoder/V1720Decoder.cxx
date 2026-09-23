#include "V1720Decoder.h"

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

bool V1720Decoder::Decode(TMEvent& event, DecodedEvent& output) const {
  assert(output.v1720.fadc0.size() == 8);
  TMBank* bank = event.FindBank("FADC");
  if (!bank || bank->type != TID_DWORD || bank->data_size < 4 * sizeof(std::uint32_t) ||
      bank->data_size % sizeof(std::uint32_t) != 0)
    return false;

  const char* data = event.GetBankData(bank);
  if (!data) return false;
  const std::size_t words = bank->data_size / sizeof(std::uint32_t);
  const std::uint32_t header0 = ReadWord(data, 0);
  const std::uint32_t header1 = ReadWord(data, 1);
  if ((header0 >> 28) != 0xau || (header0 & 0x0fffffffu) != words) return false;

  output.counters.v1720 = ReadWord(data, 2) & 0x00ffffffu;
  const std::uint32_t channel_mask = header1 & 0xffu;
  const bool zle_compressed = ((header1 >> 24) & 1u) != 0;
  if (channel_mask == 0 || zle_compressed) return false;

  std::size_t active_channels = 0;
  for (std::size_t channel = 0; channel < 8; ++channel)
    if (channel_mask & (1u << channel)) ++active_channels;
  if ((words - 4) % active_channels != 0) return false;

  const std::size_t words_per_channel = (words - 4) / active_channels;
  std::size_t position = 4;
  for (std::size_t channel = 0; channel < 8; ++channel) {
    if ((channel_mask & (1u << channel)) == 0) continue;
    auto& waveform = output.v1720.fadc0[channel];
    waveform.reserve(words_per_channel * 2);
    for (std::size_t i = 0; i < words_per_channel; ++i) {
      const std::uint32_t word = ReadWord(data, position++);
      waveform.push_back(static_cast<std::int32_t>(word & 0xfffu));
      waveform.push_back(static_cast<std::int32_t>((word >> 16) & 0xfffu));
    }
  }
  return true;
}

}  // namespace ana
