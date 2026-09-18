#include "easiroc_event.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int tests_run = 0;

void check(bool condition, const std::string& message) {
  ++tests_run;
  if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void expectFailure(Function function, const std::string& message) {
  ++tests_run;
  try {
    function();
  } catch (const std::runtime_error&) {
    return;
  }
  throw std::runtime_error(message);
}

std::array<std::uint8_t, 4> encodeWireWord(std::uint32_t word) {
  return {static_cast<std::uint8_t>(0x80 | ((word >> 21) & 0x7f)),
          static_cast<std::uint8_t>((word >> 14) & 0x7f),
          static_cast<std::uint8_t>((word >> 7) & 0x7f),
          static_cast<std::uint8_t>(word & 0x7f)};
}

void append(std::vector<std::uint8_t>& destination,
            const std::array<std::uint8_t, 4>& word) {
  destination.insert(destination.end(), word.begin(), word.end());
}
}  // namespace

int main() {
  try {
    constexpr std::uint32_t header = 0x08000005;
    constexpr std::uint32_t adc = (5u << 13) | (1u << 12) | 0x0abcu;
    constexpr std::uint32_t tdc = 0x00201000 | (12u << 13) | 0x0345u;
    constexpr std::uint32_t tdc_trailing =
        0x00200000 | (31u << 13) | 0x0678u;
    constexpr std::uint32_t scaler = 0x00400000 | (68u << 14) | 0x2345u;
    constexpr std::uint32_t adc_low = 0x00080000 | (63u << 13) | 0x0123u;

    const auto header_wire = encodeWireWord(header);
    check(header_wire == std::array<std::uint8_t, 4>{0xc0, 0x00, 0x00, 0x05},
          "known header wire bytes differ");
    check(easiroc::decodeWireWord(header_wire) == header,
          "raw-to-logical conversion failed");
    check(easiroc::decodeWireWord({0x92, 0x34, 0x56, 0x78}) ==
              ((0x12u << 21) | (0x34u << 14) | (0x56u << 7) | 0x78u),
          "explicit four-group raw-to-logical conversion failed");

    std::vector<std::uint8_t> event_bytes;
    append(event_bytes, header_wire);
    append(event_bytes, encodeWireWord(adc));
    append(event_bytes, encodeWireWord(tdc));
    append(event_bytes, encodeWireWord(tdc_trailing));
    append(event_bytes, encodeWireWord(scaler));
    append(event_bytes, encodeWireWord(adc_low));
    const auto event = easiroc::decodeEvent(event_bytes);
    check(event.header == header && event.data_size == 5,
          "valid event header failed");
    check(event.data[0].type == easiroc::DataType::kAdcHighGain &&
              event.data[0].channel == 5 && event.data[0].overflow &&
              event.data[0].value == 0x0abc,
          "high-gain ADC decode failed");
    check(event.data[1].type == easiroc::DataType::kTdcLeading &&
              event.data[1].channel == 12 && event.data[1].value == 0x0345,
          "leading TDC decode failed");
    check(event.data[2].type == easiroc::DataType::kTdcTrailing &&
              event.data[2].channel == 31 && event.data[2].value == 0x0678,
          "trailing TDC decode failed");
    check(event.data[3].type == easiroc::DataType::kScaler &&
              event.data[3].channel == 68 && event.data[3].value == 0x2345,
          "scaler decode failed");
    check(event.data[4].type == easiroc::DataType::kAdcLowGain &&
              event.data[4].channel == 63 && !event.data[4].overflow &&
              event.data[4].value == 0x0123,
          "low-gain ADC decode failed");

    expectFailure(
        [] { easiroc::decodeEvent({0x80, 0x00, 0x00, 0x00}); },
        "non-header first word was accepted");
    expectFailure(
        [&] {
          auto short_event = event_bytes;
          short_event.resize(short_event.size() - 4);
          easiroc::decodeEvent(short_event);
        },
        "size mismatch was accepted");
    expectFailure(
        [] { easiroc::decodeWireWord({0xc0, 0x80, 0x00, 0x00}); },
        "malformed per-byte framing was accepted");

    std::cout << "easiroc_event_test: " << tests_run << " checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << "easiroc_event_test: FAILED: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
