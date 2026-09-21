#include "easiroc_slow_control.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace easiroc {
namespace {

enum class BitOrder { kLsbToMsb, kMsbToLsb };
struct FieldDescriptor {
  unsigned bits;
  unsigned count;
  BitOrder order;
  bool active_low;
};

constexpr std::array<FieldDescriptor,
                     static_cast<std::size_t>(SlowField::kCount)>
    kFields{{
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {9, 32, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {4, 1, BitOrder::kLsbToMsb, false},
        {4, 1, BitOrder::kLsbToMsb, false},
        {4, 1, BitOrder::kMsbToLsb, false},
        {4, 1, BitOrder::kMsbToLsb, false},
        {2, 32, BitOrder::kMsbToLsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {3, 1, BitOrder::kLsbToMsb, true},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {3, 1, BitOrder::kLsbToMsb, true},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 32, BitOrder::kLsbToMsb, true},
        {10, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, false},
        {1, 1, BitOrder::kLsbToMsb, true},
        {1, 1, BitOrder::kLsbToMsb, true},
        {4, 1, BitOrder::kMsbToLsb, false},
    }};

std::size_t index(SlowField field) { return static_cast<std::size_t>(field); }

std::uint16_t reverseBits(std::uint16_t value, unsigned bits) {
  std::uint16_t result = 0;
  for (unsigned bit = 0; bit < bits; ++bit)
    if ((value & (1u << bit)) != 0) result |= 1u << (bits - bit - 1);
  return result;
}

void fillBits(std::vector<std::uint8_t>& bytes, std::uint16_t value,
              unsigned bits, std::size_t position) {
  for (unsigned bit = 0; bit < bits; ++bit) {
    if ((value & (1u << bit)) != 0)
      bytes[(position + bit) / 8] |= 1u << ((position + bit) % 8);
  }
}

std::uint8_t reverseByte(std::uint8_t value) {
  return static_cast<std::uint8_t>(reverseBits(value, 8));
}

std::vector<std::uint8_t> directControl(bool select_sc, bool load_sc,
                                        bool reset_read, bool start_cycles) {
  std::uint8_t chip = 0;
  if (reset_read) chip |= 0x80;
  chip |= 0x40;  // rstbSr
  if (load_sc) chip |= 0x20;
  if (select_sc) chip |= 0x10;
  chip |= 0x08;  // pwrOn
  chip |= 0x04;  // resetPA
  chip |= 0x02;  // valEvt
  return {chip, chip, static_cast<std::uint8_t>(start_cycles ? 0x03 : 0x00)};
}

template <std::size_t Size>
std::vector<std::uint8_t> asVector(
    const std::array<std::uint8_t, Size>& value) {
  return {value.begin(), value.end()};
}

void appendSerialApply(std::vector<Transaction>& plan, bool select_sc,
                       const std::vector<std::uint8_t>& first,
                       const std::vector<std::uint8_t>& second) {
  plan.push_back(Transaction::write(
      0x00000000, directControl(select_sc, false, true, false)));
  plan.push_back(Transaction::write(0x00000003, first));
  plan.push_back(Transaction::write(0x0000003d, second));
  plan.push_back(Transaction::write(
      0x00000000, directControl(select_sc, false, true, true)));
  plan.push_back(Transaction::delay(100));
  plan.push_back(Transaction::write(
      0x00000000, directControl(select_sc, true, true, false)));
  plan.push_back(Transaction::write(
      0x00000000, directControl(select_sc, false, true, false)));
}

void validateDelay(int value) {
  if (value != -1 && (value < 1 || value > 253))
    throw std::invalid_argument("trigger delay must be -1 or 1..253");
}

}  // namespace

Transaction Transaction::write(std::uint32_t address,
                               std::vector<std::uint8_t> data) {
  return {TransactionType::kWrite, address, std::move(data), 0};
}

Transaction Transaction::delay(unsigned milliseconds) {
  return {TransactionType::kDelay, 0, {}, milliseconds};
}

EasirocSlowControlConfig::EasirocSlowControlConfig() {
  for (std::size_t i = 0; i < kFields.size(); ++i)
    values_[i].assign(kFields[i].count, 0);
}

EasirocSlowControlConfig EasirocSlowControlConfig::referenceDefaults() {
  EasirocSlowControlConfig config;
  std::array<std::uint16_t, 32> input_dac{};
  input_dac.fill(256);
  config.setChannels(SlowField::kInputDac, input_dac);
  std::array<std::uint16_t, 32> zeros{};
  config.setChannels(SlowField::kDisablePaAndCalibrationEnable, zeros);
  config.setChannels(SlowField::kDiscriminatorMask, zeros);

  config.setScalar(SlowField::kEnableInputDac, 1);
  config.setScalar(SlowField::kDacReference, 1);
  config.setScalar(SlowField::kLowGainPaBias, 0);
  config.setScalar(SlowField::kCapacitorHighGainPaComp, 14);
  config.setScalar(SlowField::kCapacitorHighGainPaFeedback, 4);
  config.setScalar(SlowField::kCapacitorLowGainPaFeedback, 4);
  config.setScalar(SlowField::kCapacitorLowGainPaComp, 9);
  config.setScalar(SlowField::kTimeConstantLowGainShaper, 2);
  config.setScalar(SlowField::kTimeConstantHighGainShaper, 2);
  config.setScalar(SlowField::kTrackHoldBias, 0);
  config.setScalar(SlowField::kRsOrDiscriminator, 0);
  config.setScalar(SlowField::kDacCode, 840);
  config.setScalar(SlowField::kDacSlope, 1);

  const std::array<SlowField, 24> enabled{{
      SlowField::kHighGainPreampPp,
      SlowField::kEnableHighGainPa,
      SlowField::kLowGainPreampPp,
      SlowField::kEnableLowGainPa,
      SlowField::kLowGainSlowShaperPp,
      SlowField::kEnableLowGainSlowShaper,
      SlowField::kHighGainSlowShaperPp,
      SlowField::kEnableHighGainSlowShaper,
      SlowField::kFastShapersFollowerPp,
      SlowField::kEnableFastShaper,
      SlowField::kFastShaperPp,
      SlowField::kTrackHoldPp,
      SlowField::kEnableTrackHold,
      SlowField::kEnableDiscriminator,
      SlowField::kDiscriminatorPp,
      SlowField::kDacPp,
      SlowField::kEnableDac,
      SlowField::kBandGapPp,
      SlowField::kEnableBandGap,
      SlowField::kHighGainOtaqPp,
      SlowField::kLowGainOtaqPp,
      SlowField::kProbeOtaqPp,
      SlowField::kLvdsReceiversPp,
      SlowField::kEnableHighGainOtaq,
  }};
  for (const auto field : enabled) config.setScalar(field, 1);
  config.setScalar(SlowField::kEnableLowGainOtaq, 1);
  config.setScalar(SlowField::kEnableProbeOtaq, 1);
  config.setScalar(SlowField::kEnableLvdsReceivers, 1);
  config.setScalar(SlowField::kEnableDigitalOutput, 1);
  config.setScalar(SlowField::kEnableOr32, 1);
  config.setScalar(SlowField::kEnable32Triggers, 1);
  return config;
}

EasirocSlowControlConfig EasirocSlowControlConfig::legacySiteDefaults() {
  // Start from the complete legacy DefaultRegisterValue.yml configuration,
  // then apply RegisterValue.yml and InputDAC.yml site overrides. Keep this
  // baseline complete: future ODB fields must overlay it rather than a
  // zero-initialized EasirocSlowControlConfig.
  auto config = referenceDefaults();
  std::array<std::uint16_t, 32> input_dac{};
  input_dac.fill(350);
  config.setChannels(SlowField::kInputDac, input_dac);
  config.setScalar(SlowField::kCapacitorHighGainPaFeedback, 8);  // 100 fF
  config.setScalar(SlowField::kCapacitorLowGainPaFeedback, 8);   // 100 fF
  config.setScalar(SlowField::kTimeConstantHighGainShaper, 4);   // 100 ns
  config.setScalar(SlowField::kTimeConstantLowGainShaper, 2);    // 50 ns
  config.setScalar(SlowField::kDacCode, 600);
  config.setScalar(SlowField::kDacSlope, 1);  // fine
  return config;
}

void EasirocSlowControlConfig::setScalar(SlowField field,
                                         std::uint16_t value) {
  const auto& descriptor = kFields.at(index(field));
  if (descriptor.count != 1)
    throw std::invalid_argument("Slow Control field is channel-valued");
  if (value >= (1u << descriptor.bits))
    throw std::out_of_range("Slow Control scalar exceeds field width");
  values_[index(field)] = {value};
}

void EasirocSlowControlConfig::setChannels(
    SlowField field, const std::array<std::uint16_t, 32>& values) {
  const auto& descriptor = kFields.at(index(field));
  if (descriptor.count != values.size())
    throw std::invalid_argument("Slow Control field is not 32-channel");
  for (const auto value : values)
    if (value >= (1u << descriptor.bits))
      throw std::out_of_range("Slow Control channel value exceeds field width");
  values_[index(field)] = {values.begin(), values.end()};
}

const std::vector<std::uint16_t>& EasirocSlowControlConfig::values(
    SlowField field) const {
  return values_.at(index(field));
}

SlowControlEncoder::Image SlowControlEncoder::encode(
    const EasirocSlowControlConfig& config) {
  std::vector<std::uint8_t> bytes(57, 0);
  std::size_t bit_position = 0;
  for (std::size_t i = 0; i < kFields.size(); ++i) {
    const auto& descriptor = kFields[i];
    const auto field = static_cast<SlowField>(i);
    const auto& values = config.values(field);
    if (values.size() != descriptor.count)
      throw std::invalid_argument("Slow Control field element count differs");
    for (auto value : values) {
      if (value >= (1u << descriptor.bits))
        throw std::out_of_range("Slow Control value exceeds field width");
      if (descriptor.order == BitOrder::kMsbToLsb)
        value = reverseBits(value, descriptor.bits);
      if (descriptor.active_low)
        value ^= static_cast<std::uint16_t>((1u << descriptor.bits) - 1);
      fillBits(bytes, value, descriptor.bits, bit_position);
      bit_position += descriptor.bits;
    }
  }
  if (bit_position != 456)
    throw std::logic_error("Slow Control schema is not 456 bits");
  for (auto& byte : bytes) byte = reverseByte(byte);
  std::reverse(bytes.begin(), bytes.end());
  std::array<std::uint8_t, 57> result{};
  std::copy(bytes.begin(), bytes.end(), result.begin());
  return result;
}

std::vector<Transaction> SlowControlPolicy::buildAsicApplyPlan(
    const AsicSlowControlImages& images) {
  std::vector<Transaction> plan;
  appendSerialApply(plan, true, asVector(images[0]), asVector(images[1]));
  return plan;
}

SlowControlEncoder::Image SlowControlPolicy::encodeLegacySiteAsicOverlay(
    std::uint16_t dac_code, std::uint16_t dac_slope,
    const std::array<std::uint16_t, 32>& input_dac) {
  auto config = EasirocSlowControlConfig::legacySiteDefaults();
  config.setScalar(SlowField::kDacCode, dac_code);
  config.setScalar(SlowField::kDacSlope, dac_slope);
  config.setChannels(SlowField::kInputDac, input_dac);
  return SlowControlEncoder::encode(config);
}

AsicSlowControlImages SlowControlPolicy::encodeLegacySiteThresholdOverlay(
    std::uint16_t asic1_dac_code, std::uint16_t asic1_dac_slope,
    std::uint16_t asic2_dac_code, std::uint16_t asic2_dac_slope) {
  std::array<std::uint16_t, 32> legacy_input_dac{};
  legacy_input_dac.fill(350);
  return {encodeLegacySiteAsicOverlay(asic1_dac_code, asic1_dac_slope,
                                      legacy_input_dac),
          encodeLegacySiteAsicOverlay(asic2_dac_code, asic2_dac_slope,
                                      legacy_input_dac)};
}

std::array<std::uint8_t, 20> SlowControlPolicy::encodeProbe(
    const ProbeSelection& selection, unsigned chip_index) {
  if (chip_index > 1) throw std::invalid_argument("chip index must be 0 or 1");
  std::array<std::uint8_t, 20> result{};
  if (selection.channel == -1) return result;
  const int first_channel = chip_index == 0 ? 0 : 32;
  if (selection.channel < first_channel || selection.channel > first_channel + 31)
    throw std::out_of_range("probe channel does not belong to selected chip");
  const unsigned local = static_cast<unsigned>(selection.channel - first_channel);
  unsigned bit = 0;
  switch (selection.output) {
    case ProbeOutput::kPaHighGain: bit = local * 2; break;
    case ProbeOutput::kPaLowGain: bit = local * 2 + 1; break;
    case ProbeOutput::kSlowShaperHighGain: bit = 64 + local * 2; break;
    case ProbeOutput::kSlowShaperLowGain: bit = 64 + local * 2 + 1; break;
    case ProbeOutput::kFastShaper: bit = 128 + local; break;
  }
  std::vector<std::uint8_t> working(20, 0);
  fillBits(working, 1, 1, bit);
  for (auto& byte : working) byte = reverseByte(byte);
  std::reverse(working.begin(), working.end());
  std::copy(working.begin(), working.end(), result.begin());
  return result;
}

std::array<std::uint8_t, 11> SlowControlPolicy::encodeSelectableLogic(
    const SelectableLogicConfig& config) {
  const auto pattern = static_cast<std::uint8_t>(config.pattern);
  if (pattern > 9) throw std::out_of_range("selectable logic pattern exceeds 9");
  if (config.hit_number_threshold > 64)
    throw std::out_of_range("hit-number threshold exceeds 64");
  std::array<std::uint8_t, 11> result{};
  result[0] = pattern;
  if (config.pattern == SelectableLogicPattern::kOneChannel) {
    if (config.one_channel > 63)
      throw std::out_of_range("one-channel trigger exceeds channel 63");
    result[1] = config.one_channel;
  }
  result[2] = config.hit_number_threshold;
  for (const auto channel : config.and_channels) {
    if (channel > 63)
      throw std::out_of_range("AND channel exceeds channel 63");
    result[10 - channel / 8] |= 1u << (channel % 8);
  }
  return result;
}

std::uint8_t SlowControlPolicy::encodeTriggerWidth(
    const std::optional<std::uint16_t>& width_ns) {
  if (!width_ns) return 0;
  if (*width_ns < 40 || *width_ns > 800)
    throw std::out_of_range("trigger width must be raw or 40..800 ns");
  return static_cast<std::uint8_t>((*width_ns - 38) / 8);
}

std::array<std::uint8_t, 3> SlowControlPolicy::encodeTriggerDelay(
    const TriggerDelayConfig& config) {
  if (config.mode > 7) throw std::out_of_range("trigger mode exceeds 7");
  validateDelay(config.trigger);
  validateDelay(config.hold);
  validateDelay(config.l1);
  return {static_cast<std::uint8_t>(config.trigger == -1 ? 18 : config.trigger),
          static_cast<std::uint8_t>(config.hold == -1 ? 8 : config.hold),
          static_cast<std::uint8_t>(config.l1 == -1 ? 13 : config.l1)};
}

std::vector<Transaction> SlowControlPolicy::buildApplyPlan(
    const SlowControlConfig& config) {
  const AsicSlowControlImages images{{
      SlowControlEncoder::encode(config.easiroc[0]),
      SlowControlEncoder::encode(config.easiroc[1]),
  }};
  std::vector<Transaction> plan = buildAsicApplyPlan(images);

  const auto first_probe = encodeProbe(config.probe[0], 0);
  const auto second_probe = encodeProbe(config.probe[1], 1);
  appendSerialApply(plan, false, asVector(first_probe), asVector(second_probe));

  plan.push_back(Transaction::write(
      0x00000000, directControl(false, false, false, false)));
  plan.push_back(Transaction::write(
      0x00000000, directControl(false, false, true, false)));
  for (unsigned chip = 0; chip < 2; ++chip) {
    const int channel = config.read_register[chip].high_gain_channel;
    if (channel == -1) continue;
    const int first_channel = chip == 0 ? 0 : 32;
    if (channel < first_channel || channel > first_channel + 31)
      throw std::out_of_range("read-register channel belongs to other chip");
    plan.push_back(Transaction::write(
        chip == 0 ? 0x0000003c : 0x00000076,
        {static_cast<std::uint8_t>(channel - first_channel)}));
  }

  std::vector<std::uint8_t> pedestal;
  pedestal.reserve(256);
  for (const auto* values : {&config.pedestal_high_gain,
                             &config.pedestal_low_gain}) {
    for (const auto value : *values) {
      if (value > 4095) throw std::out_of_range("pedestal exceeds 4095");
      pedestal.push_back(static_cast<std::uint8_t>(value >> 8));
      pedestal.push_back(static_cast<std::uint8_t>(value));
    }
  }
  plan.push_back(Transaction::write(0x00001000, std::move(pedestal)));
  plan.push_back(Transaction::write(
      0x00000078, asVector(encodeSelectableLogic(config.selectable_logic))));
  plan.push_back(Transaction::write(
      0x00000088, {encodeTriggerWidth(config.trigger_width_ns)}));
  if (config.time_window_ns > 4095)
    throw std::out_of_range("time window exceeds 4095 ns");
  plan.push_back(Transaction::write(
      0x00000100,
      {static_cast<std::uint8_t>(config.time_window_ns >> 8),
       static_cast<std::uint8_t>(config.time_window_ns)}));

  if (config.trigger_delay) {
    const auto delays = encodeTriggerDelay(*config.trigger_delay);
    plan.push_back(Transaction::write(0x00010100,
                                      {config.trigger_delay->mode}));
    plan.push_back(Transaction::write(0x00010101, asVector(delays)));
  }
  return plan;
}

}  // namespace easiroc
