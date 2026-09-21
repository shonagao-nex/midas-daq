#include "easiroc_asic_apply.h"

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

struct ObservedOperation {
  easiroc::TransactionType type;
  std::uint32_t address;
  std::size_t data_size;
  unsigned delay_ms;
};
}  // namespace

int main() {
  try {
    easiroc::AsicSlowControlSettings disabled;
    int writes = 0;
    int delays = 0;
    const auto skipped = easiroc::applyAsicSlowControlAtBor(
        disabled,
        [&](std::uint32_t, const std::vector<std::uint8_t>&) { ++writes; },
        [&](unsigned) { ++delays; });
    check(!skipped.attempted && !skipped.sequence_succeeded &&
              skipped.completed_transactions == 0 && skipped.error.empty(),
          "ApplyAtBOR=false did not return a clean skipped result");
    check(writes == 0 && delays == 0,
          "ApplyAtBOR=false invoked the executor");
    check(easiroc::asicApplyPermitsDaqOn(skipped),
          "skipped apply did not permit the unchanged DAQ ON path");

    auto enabled = disabled;
    enabled.apply_at_bor = true;
    std::vector<ObservedOperation> operations;
    const auto succeeded = easiroc::applyAsicSlowControlAtBor(
        enabled,
        [&](std::uint32_t address, const std::vector<std::uint8_t>& data) {
          operations.push_back({easiroc::TransactionType::kWrite, address,
                                data.size(), 0});
        },
        [&](unsigned milliseconds) {
          operations.push_back(
              {easiroc::TransactionType::kDelay, 0, 0, milliseconds});
        });
    check(succeeded.attempted && succeeded.sequence_succeeded &&
              succeeded.completed_transactions == 7 &&
              succeeded.error.empty(),
          "successful mock apply result differs");
    check(easiroc::asicApplyPermitsDaqOn(succeeded),
          "successful apply did not permit DAQ ON");
    check(operations.size() == 7,
          "executor did not execute exactly seven transactions");
    check(operations[0].type == easiroc::TransactionType::kWrite &&
              operations[0].address == 0x00000000 &&
              operations[0].data_size == 3 &&
              operations[1].address == 0x00000003 &&
              operations[1].data_size == 57 &&
              operations[2].address == 0x0000003d &&
              operations[2].data_size == 57 &&
              operations[3].address == 0x00000000 &&
              operations[3].data_size == 3,
          "executor write order/address/image sizes differ");
    check(operations[4].type == easiroc::TransactionType::kDelay &&
              operations[4].delay_ms == 100 &&
              operations[5].address == 0x00000000 &&
              operations[6].address == 0x00000000,
          "executor delay/load/release order differs");
    for (const auto& operation : operations) {
      check(operation.type == easiroc::TransactionType::kDelay ||
                operation.address == 0x00000000 ||
                operation.address == 0x00000003 ||
                operation.address == 0x0000003d,
            "non-ASIC aggregate-plan transaction reached executor");
    }

    writes = 0;
    delays = 0;
    const auto failed = easiroc::applyAsicSlowControlAtBor(
        enabled,
        [&](std::uint32_t, const std::vector<std::uint8_t>&) {
          if (++writes == 3) throw std::runtime_error("mock RBCP failure");
        },
        [&](unsigned) { ++delays; });
    check(failed.attempted && !failed.sequence_succeeded &&
              failed.completed_transactions == 2,
          "failed executor did not stop at the failing write");
    check(writes == 3 && delays == 0,
          "executor continued after the failing write");
    check(failed.error.find("transaction 3/7 failed: mock RBCP failure") !=
                  std::string::npos &&
              failed.error.find("state may be unknown") != std::string::npos,
          "LastApplyError text lacks transaction cause or safety warning");
    bool daq_on_called = false;
    if (easiroc::asicApplyPermitsDaqOn(failed)) daq_on_called = true;
    check(!daq_on_called,
          "failed apply permitted the DAQ ON-equivalent continuation");

    std::cout << "easiroc_asic_apply_test: " << tests_run
              << " checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << "easiroc_asic_apply_test: FAILED: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
