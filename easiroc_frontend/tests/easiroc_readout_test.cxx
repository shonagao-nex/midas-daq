#include "easiroc_readout.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int tests_run = 0;

void check(bool condition, const std::string& message) {
  ++tests_run;
  if (!condition) throw std::runtime_error(message);
}

easiroc::Event makeEvent() {
  easiroc::Event event;
  for (std::uint8_t channel = 0; channel < easiroc::kAdcChannelCount;
       ++channel) {
    event.data.push_back(
        {0, easiroc::DataType::kAdcHighGain, channel,
         static_cast<std::uint16_t>(100 + channel), false});
    event.data.push_back(
        {0, easiroc::DataType::kAdcLowGain, channel,
         static_cast<std::uint16_t>(200 + channel), false});
  }
  event.data_size = static_cast<std::uint16_t>(event.data.size());
  return event;
}

void addTdc(easiroc::Event* event, easiroc::DataType type,
            std::uint8_t channel, std::uint16_t value) {
  event->data.push_back({0, type, channel, value, false});
  event->data_size = static_cast<std::uint16_t>(event->data.size());
}
}  // namespace

int main() {
  try {
    {
      const auto decoded = easiroc::organizeEvent(makeEvent());
      check(decoded.leading.empty() && decoded.trailing.empty(),
            "zero-hit event did not produce empty TDC lists");
      check(decoded.high_gain[0] == 100 && decoded.high_gain[63] == 163 &&
                decoded.low_gain[0] == 200 && decoded.low_gain[63] == 263,
            "ADC data were not ordered by channel");
    }

    auto event = makeEvent();
    addTdc(&event, easiroc::DataType::kTdcLeading, 5, 1200);
    addTdc(&event, easiroc::DataType::kTdcLeading, 5, 1320);
    addTdc(&event, easiroc::DataType::kTdcLeading, 7, 1400);
    addTdc(&event, easiroc::DataType::kTdcLeading, 5, 1450);
    addTdc(&event, easiroc::DataType::kTdcTrailing, 5, 1500);
    addTdc(&event, easiroc::DataType::kTdcTrailing, 7, 1550);
    addTdc(&event, easiroc::DataType::kTdcTrailing, 5, 1600);
    const auto decoded = easiroc::organizeEvent(event);

    check(decoded.leading[0].channel == 5 && decoded.leading[0].hit == 1,
          "single/first leading hit number is not one");
    check(decoded.leading[1].channel == 5 && decoded.leading[1].hit == 2 &&
              decoded.leading[3].channel == 5 && decoded.leading[3].hit == 3,
          "three hits on one leading channel were not numbered 1,2,3");
    check(decoded.leading[2].channel == 7 && decoded.leading[2].hit == 1,
          "leading hit counts were not independent by channel");
    check(decoded.trailing[0].channel == 5 && decoded.trailing[0].hit == 1 &&
              decoded.trailing[2].channel == 5 &&
              decoded.trailing[2].hit == 2 &&
              decoded.trailing[1].channel == 7 &&
              decoded.trailing[1].hit == 1,
          "trailing hit counts were not independent by channel");
    check(decoded.leading[1].hit == 2 && decoded.trailing[0].hit == 1,
          "leading and trailing hit counts were not independent");

    const auto payloads = easiroc::makeBankPayloads(decoded);
    check(payloads.high_gain == decoded.high_gain &&
              payloads.low_gain == decoded.low_gain,
          "ADC bank payloads differ from channel-ordered decoded data");
    check(payloads.leading ==
              std::vector<std::uint16_t>({5, 1, 1200, 5, 2, 1320,
                                          7, 1, 1400, 5, 3, 1450}) &&
              payloads.trailing ==
                  std::vector<std::uint16_t>({5, 1, 1500, 7, 1, 1550,
                                              5, 2, 1600}),
          "TDC bank payload is not channel,hit,value triplets");

    auto next_event = makeEvent();
    addTdc(&next_event, easiroc::DataType::kTdcLeading, 5, 1700);
    const auto next_decoded = easiroc::organizeEvent(next_event);
    check(next_decoded.leading.size() == 1 &&
              next_decoded.leading[0].hit == 1,
          "hit count did not reset between events");

    std::cout << "easiroc_readout_test: " << tests_run
              << " checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << "easiroc_readout_test: FAILED: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
