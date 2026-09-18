#include "easiroc_readout.h"

#include <array>
#include <stdexcept>

namespace easiroc {

DecodedEvent organizeEvent(const Event& event) {
  if (event.data.size() != event.data_size)
    throw std::runtime_error("decoded event size does not match header");

  DecodedEvent result;
  std::array<bool, kAdcChannelCount> have_high_gain{};
  std::array<bool, kAdcChannelCount> have_low_gain{};
  std::array<std::uint16_t, kAdcChannelCount> leading_counts{};
  std::array<std::uint16_t, kAdcChannelCount> trailing_counts{};

  for (const auto& word : event.data) {
    switch (word.type) {
      case DataType::kAdcHighGain:
        if (have_high_gain[word.channel])
          throw std::runtime_error("duplicate HG ADC channel in event");
        have_high_gain[word.channel] = true;
        result.high_gain[word.channel] = word.value;
        break;
      case DataType::kAdcLowGain:
        if (have_low_gain[word.channel])
          throw std::runtime_error("duplicate LG ADC channel in event");
        have_low_gain[word.channel] = true;
        result.low_gain[word.channel] = word.value;
        break;
      case DataType::kTdcLeading: {
        const std::uint16_t hit = ++leading_counts[word.channel];
        result.leading.push_back({word.channel, word.value, hit});
        break;
      }
      case DataType::kTdcTrailing: {
        const std::uint16_t hit = ++trailing_counts[word.channel];
        result.trailing.push_back({word.channel, word.value, hit});
        break;
      }
      case DataType::kScaler:
        throw std::runtime_error("scaler word received while scaler is off");
      case DataType::kUnknown:
        throw std::runtime_error("unknown data word in event");
    }
  }

  for (std::size_t channel = 0; channel < kAdcChannelCount; ++channel) {
    if (!have_high_gain[channel])
      throw std::runtime_error("missing HG ADC channel in event");
    if (!have_low_gain[channel])
      throw std::runtime_error("missing LG ADC channel in event");
  }
  return result;
}

BankPayloads makeBankPayloads(const DecodedEvent& event) {
  BankPayloads result;
  result.high_gain = event.high_gain;
  result.low_gain = event.low_gain;
  result.leading.reserve(event.leading.size() * 3);
  result.trailing.reserve(event.trailing.size() * 3);
  for (const auto& hit : event.leading) {
    result.leading.push_back(hit.channel);
    result.leading.push_back(hit.hit);
    result.leading.push_back(hit.value);
  }
  for (const auto& hit : event.trailing) {
    result.trailing.push_back(hit.channel);
    result.trailing.push_back(hit.hit);
    result.trailing.push_back(hit.value);
  }
  return result;
}

}  // namespace easiroc
