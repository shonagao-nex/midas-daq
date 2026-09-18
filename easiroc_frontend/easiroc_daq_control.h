#ifndef EASIROC_FRONTEND_EASIROC_DAQ_CONTROL_H
#define EASIROC_FRONTEND_EASIROC_DAQ_CONTROL_H

#include <cstdint>

namespace easiroc {

struct DaqEnables {
  bool adc = true;
  bool tdc = true;
  bool scaler = false;
};

struct RegisterValue {
  std::uint32_t address;
  std::uint8_t value;
};

// Pure DAQ-control policy. This class owns no transport and performs no I/O.
// startValue()/stopValue() only describe a future explicit register write.
class DaqControl {
 public:
  static constexpr std::uint32_t kStatusRegisterAddress = 0x00000077;
  static constexpr std::uint8_t kDaqModeBit = 0x01;
  static constexpr std::uint8_t kAdcEnableBit = 0x02;
  static constexpr std::uint8_t kTdcEnableBit = 0x04;
  static constexpr std::uint8_t kScalerEnableBit = 0x08;

  explicit DaqControl(DaqEnables enables = {}) noexcept;

  DaqEnables enables() const noexcept { return enables_; }
  void setEnables(DaqEnables enables) noexcept { enables_ = enables; }
  void setAdcEnabled(bool enabled) noexcept { enables_.adc = enabled; }
  void setTdcEnabled(bool enabled) noexcept { enables_.tdc = enabled; }
  void setScalerEnabled(bool enabled) noexcept { enables_.scaler = enabled; }

  std::uint8_t statusByte(bool daq_mode) const noexcept;
  RegisterValue startValue() const noexcept;
  RegisterValue stopValue() const noexcept;

 private:
  DaqEnables enables_;
};

}  // namespace easiroc

#endif
