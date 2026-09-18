#ifndef EASIROC_FRONTEND_EASIROC_READOUT_H
#define EASIROC_FRONTEND_EASIROC_READOUT_H

#include "easiroc_event.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace easiroc {

constexpr std::size_t kAdcChannelCount = 64;

struct TdcHit {
  std::uint8_t channel = 0;
  std::uint16_t value = 0;
  std::uint16_t hit = 0;  // One-based occurrence number for this channel.
};

struct DecodedEvent {
  std::array<std::uint16_t, kAdcChannelCount> high_gain{};
  std::array<std::uint16_t, kAdcChannelCount> low_gain{};
  std::vector<TdcHit> leading;
  std::vector<TdcHit> trailing;
};

// Organizes one decoded wire event for readout. Exactly one HG and one LG word
// per channel are required. Scaler and unknown words are rejected because the
// acquisition mode enables ADC/TDC only.
DecodedEvent organizeEvent(const Event& event);

// MIDAS-independent payload representation. ETLE and ETTR contain repeating
// channel, hit, value triplets. Keeping this conversion independent of MIDAS
// permits complete offline testing of the bank contents.
struct BankPayloads {
  std::array<std::uint16_t, kAdcChannelCount> high_gain{};
  std::array<std::uint16_t, kAdcChannelCount> low_gain{};
  std::vector<std::uint16_t> leading;
  std::vector<std::uint16_t> trailing;
};

BankPayloads makeBankPayloads(const DecodedEvent& event);

}  // namespace easiroc

#endif
