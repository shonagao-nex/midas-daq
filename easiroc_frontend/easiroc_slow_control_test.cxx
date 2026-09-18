#include "easiroc_slow_control.h"

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
  } catch (const std::exception&) {
    return;
  }
  throw std::runtime_error(message);
}

easiroc::EasirocSlowControlConfig makeLegacySiteConfig() {
  auto config = easiroc::EasirocSlowControlConfig::referenceDefaults();
  std::array<std::uint16_t, 32> input_dac{};
  input_dac.fill(350);
  config.setChannels(easiroc::SlowField::kInputDac, input_dac);
  config.setScalar(easiroc::SlowField::kCapacitorHighGainPaFeedback, 8);
  config.setScalar(easiroc::SlowField::kCapacitorLowGainPaFeedback, 8);
  config.setScalar(easiroc::SlowField::kTimeConstantHighGainShaper, 4);
  config.setScalar(easiroc::SlowField::kTimeConstantLowGainShaper, 2);
  config.setScalar(easiroc::SlowField::kDacCode, 600);
  return config;
}

constexpr std::array<std::uint8_t, 57> kLegacyYamlImage{{
    0xc0, 0xff, 0x9f, 0xc6, 0xff, 0xff, 0xff, 0xbf, 0x7d, 0xdf,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0xe3,
    0x5e, 0xbd, 0x7a, 0xf5, 0xea, 0xd5, 0xab, 0x57, 0xaf, 0x5e,
    0xbd, 0x7a, 0xf5, 0xea, 0xd5, 0xab, 0x57, 0xaf, 0x5e, 0xbd,
    0x7a, 0xf5, 0xea, 0xd5, 0xab, 0x57, 0xaf, 0x5e, 0xbd, 0x7a,
    0xf5, 0xea, 0xd5, 0xab, 0x57, 0xaf, 0xde,
}};

bool isWrite(const easiroc::Transaction& transaction, std::uint32_t address,
             const std::vector<std::uint8_t>& data) {
  return transaction.type == easiroc::TransactionType::kWrite &&
         transaction.address == address && transaction.data == data &&
         transaction.delay_ms == 0;
}
}  // namespace

int main() {
  try {
    const auto first = makeLegacySiteConfig();
    const auto second = makeLegacySiteConfig();  // EASIROC2 uses "same".
    const auto first_image = easiroc::SlowControlEncoder::encode(first);
    const auto second_image = easiroc::SlowControlEncoder::encode(second);
    check(first_image == kLegacyYamlImage,
          "EASIROC1 image differs from legacy ConfigLoader algorithm");
    check(second_image == kLegacyYamlImage,
          "EASIROC2 image differs from legacy same-value configuration");

    check(first.values(easiroc::SlowField::kDacCode) ==
              std::vector<std::uint16_t>{600},
          "raw 10-bit threshold code was not retained");
    check(first.values(easiroc::SlowField::kInputDac).size() == 32 &&
              first.values(easiroc::SlowField::kInputDac).front() == 350 &&
              first.values(easiroc::SlowField::kInputDac).back() == 350,
          "raw 32-channel 9-bit Input DAC array was not retained");

    auto bit_order_config = first;
    bit_order_config.setScalar(
        easiroc::SlowField::kCapacitorLowGainPaFeedback, 1);
    check(easiroc::SlowControlEncoder::encode(bit_order_config) != first_image,
          "MSB-to-LSB field change did not affect encoded image");
    auto active_low_config = first;
    std::array<std::uint16_t, 32> masked{};
    masked.fill(1);
    active_low_config.setChannels(easiroc::SlowField::kDiscriminatorMask,
                                  masked);
    check(easiroc::SlowControlEncoder::encode(active_low_config) != first_image,
          "active-low channel array change did not affect encoded image");
    expectFailure(
        [] {
          auto config = easiroc::EasirocSlowControlConfig::referenceDefaults();
          config.setScalar(easiroc::SlowField::kDacCode, 1024);
        },
        "out-of-range raw threshold code was accepted");
    expectFailure(
        [] {
          auto config = easiroc::EasirocSlowControlConfig::referenceDefaults();
          std::array<std::uint16_t, 32> values{};
          values[7] = 512;
          config.setChannels(easiroc::SlowField::kInputDac, values);
        },
        "out-of-range raw Input DAC code was accepted");

    easiroc::SlowControlConfig config;
    config.easiroc = {first, second};
    config.probe[0] = {easiroc::ProbeOutput::kPaHighGain, 5};
    config.probe[1] = {easiroc::ProbeOutput::kFastShaper, 40};
    config.read_register[0].high_gain_channel = 5;
    config.read_register[1].high_gain_channel = 40;
    config.pedestal_high_gain[0] = 0x0123;
    config.pedestal_low_gain[0] = 0x0abc;
    config.selectable_logic.pattern =
        easiroc::SelectableLogicPattern::kOneChannel;
    config.selectable_logic.one_channel = 12;
    config.selectable_logic.hit_number_threshold = 4;
    config.selectable_logic.and_channels = {0, 9, 63};
    config.trigger_width_ns = 100;
    config.time_window_ns = 4095;
    config.trigger_delay = easiroc::TriggerDelayConfig{};

    const auto plan = easiroc::SlowControlPolicy::buildApplyPlan(config);
    check(plan.size() == 24, "apply plan transaction count differs");
    check(isWrite(plan[0], 0x00000000, {0xde, 0xde, 0x00}) &&
              plan[1].address == 0x00000003 && plan[1].data.size() == 57 &&
              plan[2].address == 0x0000003d && plan[2].data.size() == 57 &&
              isWrite(plan[3], 0x00000000, {0xde, 0xde, 0x03}),
          "Slow Control image/start-cycle write order differs");
    check(plan[4].type == easiroc::TransactionType::kDelay &&
              plan[4].delay_ms == 100 &&
              isWrite(plan[5], 0x00000000, {0xfe, 0xfe, 0x00}) &&
              isWrite(plan[6], 0x00000000, {0xde, 0xde, 0x00}),
          "Slow Control 100 ms/loadSc latch sequence differs");
    check(isWrite(plan[7], 0x00000000, {0xce, 0xce, 0x00}) &&
              plan[8].address == 0x00000003 && plan[8].data.size() == 20 &&
              plan[9].address == 0x0000003d && plan[9].data.size() == 20 &&
              isWrite(plan[10], 0x00000000, {0xce, 0xce, 0x03}) &&
              plan[11].type == easiroc::TransactionType::kDelay &&
              plan[11].delay_ms == 100 &&
              isWrite(plan[12], 0x00000000, {0xee, 0xee, 0x00}) &&
              isWrite(plan[13], 0x00000000, {0xce, 0xce, 0x00}),
          "probe image/apply sequence differs");
    check(isWrite(plan[14], 0x00000000, {0x4e, 0x4e, 0x00}) &&
              isWrite(plan[15], 0x00000000, {0xce, 0xce, 0x00}) &&
              isWrite(plan[16], 0x0000003c, {5}) &&
              isWrite(plan[17], 0x00000076, {8}),
          "read-register reset/selection sequence differs");
    check(plan[18].address == 0x00001000 && plan[18].data.size() == 256 &&
              plan[18].data[0] == 0x01 && plan[18].data[1] == 0x23 &&
              plan[18].data[128] == 0x0a && plan[18].data[129] == 0xbc,
          "pedestal suppression format/order differs");
    check(plan[19].address == 0x00000078 && plan[19].data.size() == 11 &&
              plan[19].data[0] == 0 && plan[19].data[1] == 12 &&
              plan[19].data[2] == 4 && plan[19].data[10] == 0x01 &&
              plan[19].data[9] == 0x02 && plan[19].data[3] == 0x80,
          "selectable trigger logic encoding differs");
    check(isWrite(plan[20], 0x00000088, {7}) &&
              isWrite(plan[21], 0x00000100, {0x0f, 0xff}),
          "trigger width/time-window encoding differs");
    check(isWrite(plan[22], 0x00010100, {0}) &&
              isWrite(plan[23], 0x00010101, {18, 8, 13}),
          "trigger mode/default-delay encoding differs");

    std::cout << "easiroc_slow_control_test: " << tests_run
              << " checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << "easiroc_slow_control_test: FAILED: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
