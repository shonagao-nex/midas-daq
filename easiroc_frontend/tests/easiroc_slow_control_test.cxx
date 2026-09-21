#include "easiroc_slow_control.h"
#include "easiroc_run_settings.h"

#include <algorithm>
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

std::uint16_t encodedDacCode(const easiroc::SlowControlEncoder::Image& image) {
  // After the legacy byte/bit reversals, DAC code bits 0..5 occupy image[3]
  // bits 5..0 and bits 6..9 occupy image[2] bits 7..4.
  std::uint16_t result = 0;
  for (unsigned bit = 0; bit < 10; ++bit) {
    const unsigned image_byte = bit < 6 ? 3 : 2;
    const unsigned image_bit = bit < 6 ? 5 - bit : 13 - bit;
    if ((image[image_byte] & (1u << image_bit)) != 0) result |= 1u << bit;
  }
  return result;
}

std::uint16_t encodedDacSlope(
    const easiroc::SlowControlEncoder::Image& image) {
  // DAC slope immediately follows DAC code and becomes image[2] bit 3.
  return static_cast<std::uint16_t>((image[2] >> 3) & 0x01);
}

void checkOnlyThresholdBitsChanged(
    const easiroc::SlowControlEncoder::Image& baseline,
    const easiroc::SlowControlEncoder::Image& changed,
    const std::string& message) {
  for (std::size_t byte = 0; byte < baseline.size(); ++byte) {
    const std::uint8_t allowed = byte == 2 ? 0xf8 : (byte == 3 ? 0x3f : 0x00);
    check(((baseline[byte] ^ changed[byte]) & ~allowed) == 0,
          message + " at byte " + std::to_string(byte));
  }
}

std::array<std::uint16_t, 32> legacyInputDac() {
  std::array<std::uint16_t, 32> values{};
  values.fill(350);
  return values;
}

std::array<bool, 32> allChannelsEnabled() {
  std::array<bool, 32> values{};
  values.fill(true);
  return values;
}

void checkSingleActiveLowBit(
    const easiroc::SlowControlEncoder::Image& enabled,
    const easiroc::SlowControlEncoder::Image& disabled,
    const std::string& message) {
  unsigned changed_bits = 0;
  unsigned one_to_zero_bits = 0;
  for (std::size_t byte = 0; byte < enabled.size(); ++byte) {
    const std::uint8_t changed = enabled[byte] ^ disabled[byte];
    for (unsigned bit = 0; bit < 8; ++bit) {
      const std::uint8_t bit_mask = static_cast<std::uint8_t>(1u << bit);
      if ((changed & bit_mask) == 0) continue;
      ++changed_bits;
      if ((enabled[byte] & bit_mask) != 0 &&
          (disabled[byte] & bit_mask) == 0)
        ++one_to_zero_bits;
    }
  }
  check(changed_bits == 1 && one_to_zero_bits == 1, message);
}

easiroc::SlowControlEncoder::Image expectedAsicOverlay(
    std::uint16_t dac_code, std::uint16_t dac_slope,
    const std::array<std::uint16_t, 32>& input_dac,
    int hg_feedback_femtofarads = 100,
    int lg_feedback_femtofarads = 100,
    int hg_shaping_nanoseconds = 100,
    int lg_shaping_nanoseconds = 50,
    const std::array<bool, 32>* channel_enabled = nullptr) {
  auto config = easiroc::EasirocSlowControlConfig::legacySiteDefaults();
  config.setScalar(easiroc::SlowField::kDacCode, dac_code);
  config.setScalar(easiroc::SlowField::kDacSlope, dac_slope);
  config.setChannels(easiroc::SlowField::kInputDac, input_dac);
  config.setScalar(
      easiroc::SlowField::kCapacitorHighGainPaFeedback,
      easiroc::feedbackCapacitanceEncoderCode(hg_feedback_femtofarads));
  config.setScalar(
      easiroc::SlowField::kCapacitorLowGainPaFeedback,
      easiroc::feedbackCapacitanceEncoderCode(lg_feedback_femtofarads));
  config.setScalar(easiroc::SlowField::kTimeConstantHighGainShaper,
                   easiroc::shapingTimeEncoderCode(hg_shaping_nanoseconds));
  config.setScalar(easiroc::SlowField::kTimeConstantLowGainShaper,
                   easiroc::shapingTimeEncoderCode(lg_shaping_nanoseconds));
  if (channel_enabled != nullptr)
    config.setChannels(easiroc::SlowField::kDiscriminatorMask,
                       easiroc::logicalDiscriminatorMask(*channel_enabled));
  return easiroc::SlowControlEncoder::encode(config);
}
}  // namespace

int main() {
  try {
    const auto first = easiroc::EasirocSlowControlConfig::legacySiteDefaults();
    const auto second =
        easiroc::EasirocSlowControlConfig::legacySiteDefaults();
    const auto first_image = easiroc::SlowControlEncoder::encode(first);
    const auto second_image = easiroc::SlowControlEncoder::encode(second);
    check(first_image.size() == 57, "production ASIC image is not 57 bytes");
    check(first_image == kLegacyYamlImage,
          "production baseline differs from legacy ConfigLoader image");
    check(second_image == kLegacyYamlImage,
          "EASIROC2 image differs from legacy same-value configuration");
    check(first_image == second_image,
          "identical ASIC configurations produced different images");

    check(first.values(easiroc::SlowField::kDacCode) ==
              std::vector<std::uint16_t>{600},
          "raw 10-bit threshold code was not retained");
    check(first.values(easiroc::SlowField::kDacSlope) ==
              std::vector<std::uint16_t>{1},
          "legacy fine DAC slope was not retained");
    check(first.values(easiroc::SlowField::kInputDac).size() == 32 &&
              first.values(easiroc::SlowField::kInputDac).front() == 350 &&
              first.values(easiroc::SlowField::kInputDac).back() == 350,
          "raw 32-channel 9-bit Input DAC array was not retained");
    check(first.values(easiroc::SlowField::kCapacitorHighGainPaFeedback) ==
                  std::vector<std::uint16_t>{8} &&
              first.values(easiroc::SlowField::kCapacitorLowGainPaFeedback) ==
                  std::vector<std::uint16_t>{8},
          "legacy 100 fF feedback settings were not retained");
    check(first.values(easiroc::SlowField::kTimeConstantHighGainShaper) ==
                  std::vector<std::uint16_t>{4} &&
              first.values(easiroc::SlowField::kTimeConstantLowGainShaper) ==
                  std::vector<std::uint16_t>{2},
          "legacy HG/LG shaping settings were not retained");
    check(first.values(easiroc::SlowField::kDiscriminatorMask).size() == 32 &&
              std::all_of(
                  first.values(easiroc::SlowField::kDiscriminatorMask).begin(),
                  first.values(easiroc::SlowField::kDiscriminatorMask).end(),
                  [](std::uint16_t value) { return value == 0; }),
          "legacy discriminator mask is not logical 0 (unmasked) for every channel");

    for (const std::size_t channel : {std::size_t{0}, std::size_t{16},
                                      std::size_t{31}}) {
      auto masked = easiroc::EasirocSlowControlConfig::legacySiteDefaults();
      std::array<std::uint16_t, 32> values{};
      values[channel] = 1;
      masked.setChannels(easiroc::SlowField::kDiscriminatorMask, values);
      check(masked.values(easiroc::SlowField::kDiscriminatorMask)[channel] ==
                1 &&
                easiroc::SlowControlEncoder::encode(masked) != first_image,
            "logical discriminator mask=1 did not mask channel " +
                std::to_string(channel));
    }
    check(first.values(easiroc::SlowField::kEnableDiscriminator) ==
                  std::vector<std::uint16_t>{1} &&
              first.values(easiroc::SlowField::kEnableDac) ==
                  std::vector<std::uint16_t>{1} &&
              first.values(easiroc::SlowField::kEnable32Triggers) ==
                  std::vector<std::uint16_t>{1},
          "legacy enable/control baseline was not retained");

    const auto baseline_input_dac = legacyInputDac();
    const auto enabled_channels = allChannelsEnabled();
    const auto full_baseline_overlay =
        easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
            600, 1, baseline_input_dac, 100, 100, 100, 50,
            enabled_channels);
    check(full_baseline_overlay.size() == 57,
          "full ASIC overlay image is not 57 bytes");
    check(full_baseline_overlay == kLegacyYamlImage,
          "full default overlay differs from legacy ConfigLoader image");

    check(easiroc::logicalDiscriminatorMask(enabled_channels) ==
              std::array<std::uint16_t, 32>{},
          "all-enabled ChannelEnabled did not map to logical mask zero");
    for (const std::size_t channel : {std::size_t{0}, std::size_t{16},
                                      std::size_t{31}}) {
      auto channel_enabled = enabled_channels;
      channel_enabled[channel] = false;
      const auto logical_mask =
          easiroc::logicalDiscriminatorMask(channel_enabled);
      check(logical_mask[channel] == 1 &&
                std::count(logical_mask.begin(), logical_mask.end(), 1u) == 1,
            "disabled channel did not map exclusively to logical mask one at " +
                std::to_string(channel));
      const auto overlay =
          easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
              600, 1, baseline_input_dac, 100, 100, 100, 50,
              channel_enabled);
      check(overlay == expectedAsicOverlay(600, 1, baseline_input_dac, 100,
                                           100, 100, 50, &channel_enabled),
            "ChannelEnabled overlay differs from existing encoder at channel " +
                std::to_string(channel));
      checkSingleActiveLowBit(
          full_baseline_overlay, overlay,
          "existing encoder did not ActiveLow-invert exactly one mask bit at " +
              std::to_string(channel));
    }
    auto multiple_channels_enabled = enabled_channels;
    multiple_channels_enabled[0] = false;
    multiple_channels_enabled[7] = false;
    multiple_channels_enabled[16] = false;
    multiple_channels_enabled[31] = false;
    const auto multiple_mask =
        easiroc::logicalDiscriminatorMask(multiple_channels_enabled);
    check(std::count(multiple_mask.begin(), multiple_mask.end(), 1u) == 4,
          "multiple disabled channels did not map to four logical mask bits");
    const auto multiple_mask_overlay =
        easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
            600, 1, baseline_input_dac, 100, 100, 100, 50,
            multiple_channels_enabled);
    check(multiple_mask_overlay ==
              expectedAsicOverlay(600, 1, baseline_input_dac, 100, 100, 100,
                                  50, &multiple_channels_enabled),
          "multiple-channel mask overlay differs from existing encoder");

    const auto hg_feedback_overlay =
        easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
            600, 1, baseline_input_dac, 200, 100);
    check(hg_feedback_overlay ==
              expectedAsicOverlay(600, 1, baseline_input_dac, 200, 100) &&
              hg_feedback_overlay != full_baseline_overlay,
          "HG-only feedback overlay differs from existing encoder mapping");
    const auto lg_feedback_overlay =
        easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
            600, 1, baseline_input_dac, 100, 300);
    check(lg_feedback_overlay ==
              expectedAsicOverlay(600, 1, baseline_input_dac, 100, 300) &&
              lg_feedback_overlay != full_baseline_overlay,
          "LG-only feedback overlay differs from existing encoder mapping");
    const auto both_feedback_overlay =
        easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
            600, 1, baseline_input_dac, 400, 1500);
    check(both_feedback_overlay ==
              expectedAsicOverlay(600, 1, baseline_input_dac, 400, 1500) &&
              both_feedback_overlay != hg_feedback_overlay &&
              both_feedback_overlay != lg_feedback_overlay,
          "combined HG/LG feedback overlay was not independent");

    const auto hg_shaping_overlay =
        easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
            600, 1, baseline_input_dac, 100, 100, 75, 50);
    check(hg_shaping_overlay ==
              expectedAsicOverlay(600, 1, baseline_input_dac, 100, 100, 75,
                                  50) &&
              hg_shaping_overlay != full_baseline_overlay,
          "HG-only shaping overlay differs from existing encoder mapping");
    const auto lg_shaping_overlay =
        easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
            600, 1, baseline_input_dac, 100, 100, 100, 125);
    check(lg_shaping_overlay ==
              expectedAsicOverlay(600, 1, baseline_input_dac, 100, 100, 100,
                                  125) &&
              lg_shaping_overlay != full_baseline_overlay,
          "LG-only shaping overlay differs from existing encoder mapping");
    const auto both_shaping_overlay =
        easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
            600, 1, baseline_input_dac, 100, 100, 25, 175);
    check(both_shaping_overlay ==
              expectedAsicOverlay(600, 1, baseline_input_dac, 100, 100, 25,
                                  175) &&
              both_shaping_overlay != hg_shaping_overlay &&
              both_shaping_overlay != lg_shaping_overlay,
          "combined HG/LG shaping overlay was not independent");

    for (const auto channel : {std::size_t{0}, std::size_t{15},
                               std::size_t{31}}) {
      auto input_dac = baseline_input_dac;
      input_dac[channel] = static_cast<std::uint16_t>(351 + channel);
      const auto overlay =
          easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
              600, 1, input_dac, 100, 100);
      check(overlay == expectedAsicOverlay(600, 1, input_dac),
            "single-channel InputDAC overlay changed another slow-control "
            "field at channel " +
                std::to_string(channel));
      check(overlay != full_baseline_overlay,
            "single-channel InputDAC overlay did not change the image at "
            "channel " +
                std::to_string(channel));
      check(encodedDacCode(overlay) == 600 &&
                encodedDacSlope(overlay) == 1,
            "single-channel InputDAC overlay changed the threshold");
    }

    for (const auto boundary : {std::uint16_t{0}, std::uint16_t{511}}) {
      auto input_dac = baseline_input_dac;
      input_dac[16] = boundary;
      const auto overlay =
          easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
              600, 1, input_dac, 100, 100);
      check(overlay == expectedAsicOverlay(600, 1, input_dac),
            "InputDAC boundary overlay differs from existing encoder output");
    }

    auto combined_input_dac = baseline_input_dac;
    combined_input_dac[0] = 351;
    combined_input_dac[31] = 349;
    auto combined_channel_enabled = enabled_channels;
    combined_channel_enabled[5] = false;
    combined_channel_enabled[27] = false;
    const auto combined_overlay =
        easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
            601, 1, combined_input_dac, 700, 1200, 150, 25,
            combined_channel_enabled);
    check(combined_overlay ==
              expectedAsicOverlay(601, 1, combined_input_dac, 700, 1200, 150,
                                  25, &combined_channel_enabled),
          "combined threshold/InputDAC/feedback/shaping/mask overlay differs");
    check(encodedDacCode(combined_overlay) == 601 &&
              encodedDacSlope(combined_overlay) == 1,
          "combined overlay encoded an incorrect threshold");

    auto asic1_input_dac = baseline_input_dac;
    auto asic2_input_dac = baseline_input_dac;
    asic1_input_dac[0] = 111;
    asic2_input_dac[0] = 222;
    asic2_input_dac[31] = 333;
    auto asic1_channel_enabled = enabled_channels;
    auto asic2_channel_enabled = enabled_channels;
    asic1_channel_enabled[0] = false;
    asic2_channel_enabled[31] = false;
    const auto asic1_overlay =
        easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
            600, 1, asic1_input_dac, 200, 300, 75, 125,
            asic1_channel_enabled);
    const auto asic2_overlay =
        easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
            600, 1, asic2_input_dac, 1400, 1500, 25, 175,
            asic2_channel_enabled);
    check(asic1_overlay ==
              expectedAsicOverlay(600, 1, asic1_input_dac, 200, 300, 75,
                                  125, &asic1_channel_enabled) &&
              asic2_overlay ==
              expectedAsicOverlay(600, 1, asic2_input_dac, 1400, 1500, 25,
                                  175, &asic2_channel_enabled) &&
              asic1_overlay != asic2_overlay,
          "ASIC1/ASIC2 full overlays were not independent");
    check(easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
              600, 1, asic1_input_dac, 200, 300, 75, 125,
              asic1_channel_enabled) == asic1_overlay,
          "ASIC2 overlay modified ASIC1 helper state");
    expectFailure(
        [baseline_input_dac] {
          auto invalid = baseline_input_dac;
          invalid[31] = 512;
          easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
              600, 1, invalid, 100, 100);
        },
        "full ASIC overlay accepted an out-of-range InputDAC value");
    expectFailure(
        [baseline_input_dac] {
          easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
              600, 1, baseline_input_dac, 99, 100);
        },
        "full ASIC overlay accepted an invalid feedback capacitance");
    expectFailure(
        [baseline_input_dac] {
          easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
              600, 1, baseline_input_dac, 100, 100, 60, 50);
        },
        "full ASIC overlay accepted an invalid shaping time");

    const auto default_overlay =
        easiroc::SlowControlPolicy::encodeLegacySiteThresholdOverlay(
            600, 1, 600, 1);
    check(default_overlay[0] == kLegacyYamlImage &&
              default_overlay[1] == kLegacyYamlImage,
          "default threshold overlay differs from production baseline");

    for (const auto dac_code : {std::uint16_t{0}, std::uint16_t{600},
                                std::uint16_t{1023}}) {
      for (const auto dac_slope : {std::uint16_t{0}, std::uint16_t{1}}) {
        const auto images =
            easiroc::SlowControlPolicy::encodeLegacySiteThresholdOverlay(
                dac_code, dac_slope, dac_code, dac_slope);
        check(images[0].size() == 57 && images[1].size() == 57,
              "threshold boundary overlay did not produce 57-byte images");
        check(encodedDacCode(images[0]) == dac_code &&
                  encodedDacCode(images[1]) == dac_code,
              "DAC code boundary was encoded at unexpected bits");
        check(encodedDacSlope(images[0]) == dac_slope &&
                  encodedDacSlope(images[1]) == dac_slope,
              "DAC slope boundary was encoded at unexpected bit");
        checkOnlyThresholdBitsChanged(
            first_image, images[0],
            "ASIC1 threshold overlay changed a non-threshold bit");
        checkOnlyThresholdBitsChanged(
            second_image, images[1],
            "ASIC2 threshold overlay changed a non-threshold bit");
      }
    }

    const auto independent_overlays =
        easiroc::SlowControlPolicy::encodeLegacySiteThresholdOverlay(0, 0,
                                                                    1023, 1);
    check(encodedDacCode(independent_overlays[0]) == 0 &&
              encodedDacSlope(independent_overlays[0]) == 0 &&
              encodedDacCode(independent_overlays[1]) == 1023 &&
              encodedDacSlope(independent_overlays[1]) == 1,
          "ASIC1/ASIC2 threshold overlays were not independent");
    check(easiroc::SlowControlEncoder::encode(first) == first_image,
          "threshold overlay modified the source production baseline");
    expectFailure(
        [] {
          easiroc::SlowControlPolicy::encodeLegacySiteThresholdOverlay(
              1024, 1, 600, 1);
        },
        "out-of-range ASIC1 threshold code was accepted");
    expectFailure(
        [] {
          easiroc::SlowControlPolicy::encodeLegacySiteThresholdOverlay(
              600, 1, 600, 2);
        },
        "out-of-range ASIC2 DAC slope was accepted");

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

    const easiroc::AsicSlowControlImages baseline_images{{first_image,
                                                          second_image}};
    const auto asic_plan =
        easiroc::SlowControlPolicy::buildAsicApplyPlan(baseline_images);
    check(asic_plan.size() == 7,
          "ASIC-only apply plan does not contain exactly seven operations");
    check(isWrite(asic_plan[0], 0x00000000, {0xde, 0xde, 0x00}) &&
              isWrite(asic_plan[1], 0x00000003,
                      std::vector<std::uint8_t>(first_image.begin(),
                                                first_image.end())) &&
              isWrite(asic_plan[2], 0x0000003d,
                      std::vector<std::uint8_t>(second_image.begin(),
                                                second_image.end())) &&
              isWrite(asic_plan[3], 0x00000000, {0xde, 0xde, 0x03}),
          "ASIC-only image/start-cycle sequence differs");
    check(asic_plan[4].type == easiroc::TransactionType::kDelay &&
              asic_plan[4].delay_ms == 100 &&
              isWrite(asic_plan[5], 0x00000000, {0xfe, 0xfe, 0x00}) &&
              isWrite(asic_plan[6], 0x00000000, {0xde, 0xde, 0x00}),
          "ASIC-only delay/load/release sequence differs");

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
