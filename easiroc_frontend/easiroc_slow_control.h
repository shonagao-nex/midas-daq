#ifndef EASIROC_FRONTEND_EASIROC_SLOW_CONTROL_H
#define EASIROC_FRONTEND_EASIROC_SLOW_CONTROL_H

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace easiroc {

enum class TransactionType { kWrite, kDelay };

struct Transaction {
  TransactionType type = TransactionType::kWrite;
  std::uint32_t address = 0;
  std::vector<std::uint8_t> data;
  unsigned delay_ms = 0;

  static Transaction write(std::uint32_t address,
                           std::vector<std::uint8_t> data);
  static Transaction delay(unsigned milliseconds);
};

enum class SlowField : std::size_t {
  kEnableInputDac,
  kDacReference,
  kInputDac,
  kLowGainPaBias,
  kHighGainPreampPp,
  kEnableHighGainPa,
  kLowGainPreampPp,
  kEnableLowGainPa,
  kCapacitorHighGainPaComp,
  kCapacitorHighGainPaFeedback,
  kCapacitorLowGainPaFeedback,
  kCapacitorLowGainPaComp,
  kDisablePaAndCalibrationEnable,
  kLowGainSlowShaperPp,
  kEnableLowGainSlowShaper,
  kTimeConstantLowGainShaper,
  kHighGainSlowShaperPp,
  kEnableHighGainSlowShaper,
  kTimeConstantHighGainShaper,
  kFastShapersFollowerPp,
  kEnableFastShaper,
  kFastShaperPp,
  kTrackHoldPp,
  kEnableTrackHold,
  kTrackHoldBias,
  kEnableDiscriminator,
  kDiscriminatorPp,
  kRsOrDiscriminator,
  kDiscriminatorMask,
  kDacCode,
  kDacSlope,
  kDacPp,
  kEnableDac,
  kBandGapPp,
  kEnableBandGap,
  kHighGainOtaqPp,
  kLowGainOtaqPp,
  kProbeOtaqPp,
  kLvdsReceiversPp,
  kEnableHighGainOtaq,
  kEnableLowGainOtaq,
  kEnableProbeOtaq,
  kEnableLvdsReceivers,
  kEnableDigitalOutput,
  kEnableOr32,
  kEnable32Triggers,
  kNotConnected,
  kCount,
};

class EasirocSlowControlConfig {
 public:
  EasirocSlowControlConfig();

  static EasirocSlowControlConfig referenceDefaults();

  void setScalar(SlowField field, std::uint16_t value);
  void setChannels(SlowField field,
                   const std::array<std::uint16_t, 32>& values);
  const std::vector<std::uint16_t>& values(SlowField field) const;

 private:
  std::array<std::vector<std::uint16_t>,
             static_cast<std::size_t>(SlowField::kCount)>
      values_;
};

class SlowControlEncoder {
 public:
  static std::array<std::uint8_t, 57> encode(
      const EasirocSlowControlConfig& config);
};

enum class ProbeOutput {
  kPaHighGain,
  kPaLowGain,
  kSlowShaperHighGain,
  kSlowShaperLowGain,
  kFastShaper,
};

struct ProbeSelection {
  ProbeOutput output = ProbeOutput::kFastShaper;
  int channel = -1;  // Global 0..63; -1 disables this chip's probe image.
};

struct ReadRegisterSelection {
  int high_gain_channel = -1;  // Global 0..63; -1 disables selection.
};

enum class SelectableLogicPattern : std::uint8_t {
  kOneChannel = 0,
  kOr32Upper = 1,
  kOr32Lower = 2,
  kOr64 = 3,
  kOr32And = 4,
  kOr16And = 5,
  kAnd32Upper = 6,
  kAnd32Lower = 7,
  kAnd64 = 8,
  kAnd32Or = 9,
};

struct SelectableLogicConfig {
  SelectableLogicPattern pattern = SelectableLogicPattern::kOr64;
  std::uint8_t one_channel = 0;
  std::uint8_t hit_number_threshold = 0;
  std::vector<std::uint8_t> and_channels;
};

struct TriggerDelayConfig {
  std::uint8_t mode = 0;
  int trigger = -1;
  int hold = -1;
  int l1 = -1;
};

struct SlowControlConfig {
  std::array<EasirocSlowControlConfig, 2> easiroc;
  std::array<ProbeSelection, 2> probe;
  std::array<ReadRegisterSelection, 2> read_register;
  std::array<std::uint16_t, 64> pedestal_high_gain{};
  std::array<std::uint16_t, 64> pedestal_low_gain{};
  SelectableLogicConfig selectable_logic;
  std::optional<std::uint16_t> trigger_width_ns;  // nullopt means raw.
  std::uint16_t time_window_ns = 4095;
  std::optional<TriggerDelayConfig> trigger_delay;
};

class SlowControlPolicy {
 public:
  static std::vector<Transaction> buildApplyPlan(
      const SlowControlConfig& config);

  static std::array<std::uint8_t, 20> encodeProbe(
      const ProbeSelection& selection, unsigned chip_index);
  static std::array<std::uint8_t, 11> encodeSelectableLogic(
      const SelectableLogicConfig& config);
  static std::uint8_t encodeTriggerWidth(
      const std::optional<std::uint16_t>& width_ns);
  static std::array<std::uint8_t, 3> encodeTriggerDelay(
      const TriggerDelayConfig& config);
};

}  // namespace easiroc

#endif
