#include "easiroc_event.h"

#include <stdexcept>

namespace easiroc {
namespace {
constexpr std::uint32_t kHeaderFlag = 0x08000000;
constexpr std::uint32_t kAdcTypeMask = 0x00680000;
constexpr std::uint32_t kAdcHighGain = 0x00000000;
constexpr std::uint32_t kAdcLowGain = 0x00080000;
constexpr std::uint32_t kTdcTypeMask = 0x00601000;
constexpr std::uint32_t kTdcLeading = 0x00201000;
constexpr std::uint32_t kTdcTrailing = 0x00200000;
constexpr std::uint32_t kScalerTypeMask = 0x00600000;
constexpr std::uint32_t kScaler = 0x00400000;

std::array<std::uint8_t, 4> wordAt(const std::vector<std::uint8_t>& bytes,
                                   std::size_t offset) {
  return {bytes[offset], bytes[offset + 1], bytes[offset + 2],
          bytes[offset + 3]};
}
}  // namespace

std::uint32_t decodeWireWord(const std::array<std::uint8_t, 4>& bytes) {
  if ((bytes[0] & 0x80) == 0 || (bytes[1] & 0x80) != 0 ||
      (bytes[2] & 0x80) != 0 || (bytes[3] & 0x80) != 0) {
    throw std::runtime_error("invalid EASIROC wire-word framing bits");
  }
  return (static_cast<std::uint32_t>(bytes[0] & 0x7f) << 21) |
         (static_cast<std::uint32_t>(bytes[1] & 0x7f) << 14) |
         (static_cast<std::uint32_t>(bytes[2] & 0x7f) << 7) |
         static_cast<std::uint32_t>(bytes[3] & 0x7f);
}

DataWord decodeDataWord(std::uint32_t word) {
  if ((word & kHeaderFlag) != 0)
    throw std::runtime_error("header word found in event data");

  DataWord result;
  result.raw = word;
  if ((word & kAdcTypeMask) == kAdcHighGain) {
    result.type = DataType::kAdcHighGain;
    result.channel = static_cast<std::uint8_t>((word >> 13) & 0x3f);
    result.overflow = ((word >> 12) & 1) != 0;
    result.value = static_cast<std::uint16_t>(word & 0x0fff);
  } else if ((word & kAdcTypeMask) == kAdcLowGain) {
    result.type = DataType::kAdcLowGain;
    result.channel = static_cast<std::uint8_t>((word >> 13) & 0x3f);
    result.overflow = ((word >> 12) & 1) != 0;
    result.value = static_cast<std::uint16_t>(word & 0x0fff);
  } else if ((word & kTdcTypeMask) == kTdcLeading) {
    result.type = DataType::kTdcLeading;
    result.channel = static_cast<std::uint8_t>((word >> 13) & 0x3f);
    result.value = static_cast<std::uint16_t>(word & 0x0fff);
  } else if ((word & kTdcTypeMask) == kTdcTrailing) {
    result.type = DataType::kTdcTrailing;
    result.channel = static_cast<std::uint8_t>((word >> 13) & 0x3f);
    result.value = static_cast<std::uint16_t>(word & 0x0fff);
  } else if ((word & kScalerTypeMask) == kScaler) {
    result.type = DataType::kScaler;
    result.channel = static_cast<std::uint8_t>((word >> 14) & 0x7f);
    result.value = static_cast<std::uint16_t>(word & 0x3fff);
  }
  return result;
}

Event decodeEvent(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 4 || bytes.size() % 4 != 0)
    throw std::runtime_error("event byte count is not a non-empty word sequence");

  Event event;
  event.header = decodeWireWord(wordAt(bytes, 0));
  if ((event.header & kHeaderFlag) == 0)
    throw std::runtime_error("event does not begin with a header word");
  event.data_size = static_cast<std::uint16_t>(event.header & 0x0fff);

  const std::size_t expected_bytes =
      (static_cast<std::size_t>(event.data_size) + 1) * 4;
  if (bytes.size() != expected_bytes)
    throw std::runtime_error("event size does not match header data size");

  event.data.reserve(event.data_size);
  for (std::size_t i = 0; i < event.data_size; ++i)
    event.data.push_back(
        decodeDataWord(decodeWireWord(wordAt(bytes, (i + 1) * 4))));
  return event;
}

}  // namespace easiroc
