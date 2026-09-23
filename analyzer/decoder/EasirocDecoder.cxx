#include "EasirocDecoder.h"

#include "DecodedEvent.h"
#include "midasio.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
std::uint16_t ReadWord(const char* data, std::size_t index) {
  std::uint16_t value = 0;
  std::memcpy(&value, data + index * sizeof(value), sizeof(value));
  return value;
}

bool DecodeHits(TMEvent& event, const char* bank_name,
                std::vector<std::vector<std::int32_t>>& destination) {
  assert(destination.size() == 64);
  TMBank* bank = event.FindBank(bank_name);
  if (!bank || bank->type != TID_WORD || bank->data_size % sizeof(std::uint16_t) != 0)
    return false;
  const std::size_t words = bank->data_size / sizeof(std::uint16_t);
  if (words == 0) return true;
  const char* data = event.GetBankData(bank);
  if (!data) return false;
  if (words % 3 != 0) return false;
  for (std::size_t i = 0; i < words; i += 3) {
    const std::size_t channel = ReadWord(data, i);
    if (channel >= destination.size()) return false;
    // Word i+1 is the one-based hit ordinal produced by the frontend.
    destination[channel].push_back(static_cast<std::int32_t>(ReadWord(data, i + 2)));
  }
  return true;
}
}  // namespace

namespace ana {

bool EasirocDecoder::Decode(TMEvent& event, DecodedEvent& output) const {
  TMBank* adc = event.FindBank("EAHG");
  if (!adc || adc->type != TID_WORD ||
      adc->data_size != output.easiroc.eadc0.size() * sizeof(std::uint16_t))
    return false;
  const char* data = event.GetBankData(adc);
  if (!data) return false;
  for (std::size_t channel = 0; channel < output.easiroc.eadc0.size(); ++channel)
    output.easiroc.eadc0[channel] = ReadWord(data, channel);

  if (!DecodeHits(event, "ETLE", output.easiroc.etle0) ||
      !DecodeHits(event, "ETTR", output.easiroc.ettr0))
    return false;

  // The current bank payloads do not contain the NIM-EASIROC raw event counter.
  output.counters.nim_easiroc = kInvalidCounter;
  return true;
}

}  // namespace ana
