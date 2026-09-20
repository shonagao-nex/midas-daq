#include "easiroc_daq_control.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {
int tests_run = 0;

void check(bool condition, const std::string& message) {
  ++tests_run;
  if (!condition) throw std::runtime_error(message);
}

void checkValues(easiroc::DaqEnables enables, std::uint8_t expected_start,
                 std::uint8_t expected_stop, const std::string& name) {
  const easiroc::DaqControl control(enables);
  const auto start = control.startValue();
  const auto stop = control.stopValue();
  check(start.address == easiroc::DaqControl::kStatusRegisterAddress &&
            stop.address == easiroc::DaqControl::kStatusRegisterAddress,
        name + ": register address differs");
  check(start.value == expected_start && stop.value == expected_stop,
        name + ": status byte differs");
}
}  // namespace

int main() {
  try {
    static_assert(std::is_nothrow_constructible_v<easiroc::DaqControl>,
                  "construction must be local and non-throwing");
    static_assert(std::is_trivially_destructible_v<easiroc::DaqControl>,
                  "destruction must not perform DAQ cleanup I/O");

    checkValues({true, true, false}, 0x07, 0x06,
                "ADC on, TDC on, scaler off");
    checkValues({true, false, false}, 0x03, 0x02,
                "ADC on, TDC off, scaler off");
    checkValues({false, true, false}, 0x05, 0x04,
                "ADC off, TDC on, scaler off");
    checkValues({true, true, true}, 0x0f, 0x0e,
                "ADC on, TDC on, scaler on");
    checkValues({false, false, false}, 0x01, 0x00,
                "all enables off");

    easiroc::DaqControl control({false, false, false});
    check((control.statusByte(true) ^ control.statusByte(false)) ==
              easiroc::DaqControl::kDaqModeBit,
          "DAQ mode must change only bit zero");

    control.setAdcEnabled(true);
    check(control.startValue().value == 0x03 &&
              control.stopValue().value == 0x02,
          "ADC enable update failed");
    control.setTdcEnabled(true);
    control.setScalerEnabled(true);
    check(control.startValue().value == 0x0f &&
              control.stopValue().value == 0x0e,
          "TDC/scaler enable update failed");
    control.setEnables({false, true, false});
    check(control.startValue().value == 0x05 &&
              control.stopValue().value == 0x04,
          "combined enable update failed");

    // Construction and all operations above have no transport object or I/O
    // callback to invoke; they only return plain address/value descriptions.
    check(control.startValue().address == 0x00000077,
          "pure policy returned an unexpected address");

    std::cout << "easiroc_daq_control_test: " << tests_run
              << " checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << "easiroc_daq_control_test: FAILED: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
