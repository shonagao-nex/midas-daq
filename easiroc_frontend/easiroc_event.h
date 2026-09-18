#ifndef EASIROC_FRONTEND_EASIROC_EVENT_H
#define EASIROC_FRONTEND_EASIROC_EVENT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace easiroc {

enum class DataType {
  kAdcHighGain,
  kAdcLowGain,
  kTdcLeading,
  kTdcTrailing,
  kScaler,
  kUnknown,
};

struct DataWord {
  std::uint32_t raw = 0;
  DataType type = DataType::kUnknown;
  std::uint8_t channel = 0;
  std::uint16_t value = 0;
  bool overflow = false;  // ADC over-threshold flag (logical bit 12).
};

struct Event {
  std::uint32_t header = 0;
  std::uint16_t data_size = 0;
  std::vector<DataWord> data;
};

// Converts one four-byte TCP wire word to the controller's 28-bit logical word.
// Throws std::runtime_error if the per-byte framing bits are malformed.
std::uint32_t decodeWireWord(const std::array<std::uint8_t, 4>& bytes);

DataWord decodeDataWord(std::uint32_t logical_word);

// Decodes exactly one complete event. Extra or missing bytes are rejected.
Event decodeEvent(const std::vector<std::uint8_t>& wire_bytes);

}  // namespace easiroc

#endif
