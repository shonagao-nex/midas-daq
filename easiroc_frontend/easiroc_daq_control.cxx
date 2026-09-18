#include "easiroc_daq_control.h"

namespace easiroc {

DaqControl::DaqControl(DaqEnables enables) noexcept : enables_(enables) {}

std::uint8_t DaqControl::statusByte(bool daq_mode) const noexcept {
  std::uint8_t value = daq_mode ? kDaqModeBit : 0;
  if (enables_.adc) value |= kAdcEnableBit;
  if (enables_.tdc) value |= kTdcEnableBit;
  if (enables_.scaler) value |= kScalerEnableBit;
  return value;
}

RegisterValue DaqControl::startValue() const noexcept {
  return {kStatusRegisterAddress, statusByte(true)};
}

RegisterValue DaqControl::stopValue() const noexcept {
  return {kStatusRegisterAddress, statusByte(false)};
}

}  // namespace easiroc
