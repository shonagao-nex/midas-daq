#include "global_busy_readiness.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

std::string source() {
  std::ifstream input("global_busy.cxx");
  return {std::istreambuf_iterator<char>(input), {}};
}
}  // namespace

int main() {
  using global_busy::ParticipantReadiness;
  using global_busy::ready_for_run;
  constexpr int run = 24;
  const ParticipantReadiness ready{true, true, run};
  const ParticipantReadiness not_ready{true, false, run};
  const ParticipantReadiness disconnected{false, false, 0};
  const ParticipantReadiness wrong_run{true, true, run - 1};

  // VME is mandatory; EASIROC is checked only if recorded as a participant.
  require(ready_for_run(true, ready, run) &&
              ready_for_run(false, disconnected, run),
          "VME-only run must accept disconnected EASIROC");
  require(ready_for_run(true, ready, run) &&
              ready_for_run(true, ready, run),
          "run with both participants Ready must succeed");
  require(!ready_for_run(true, not_ready, run),
          "participating EASIROC with DAQReady=false must fail");
  require(ready_for_run(false, not_ready, run),
          "nonparticipant with DAQReady=false must be ignored");
  require(!ready_for_run(true, disconnected, run),
          "disconnected participant must fail");
  require(!ready_for_run(true, wrong_run, run),
          "participant with wrong ReadyRunNumber must fail");

  const auto code = source();
  require(!code.empty(), "cannot read global_busy.cxx");
  require(code.find("/DAQ/Status/Run/ParticipationValid") != std::string::npos &&
              code.find("/DAQ/Status/Run/ParticipationRunNumber") != std::string::npos &&
              code.find("/DAQ/Status/Run/VMEParticipating") != std::string::npos &&
              code.find("/DAQ/Status/Run/EASIROCParticipating") != std::string::npos,
          "START 600 must use the recorded run participants");
  require(code.find("const bool vme_ready = participants.vme &&") != std::string::npos &&
              code.find("participants.easiroc, run)") != std::string::npos,
          "VME must be mandatory and EASIROC must use its participation flag");
  require(code.find("if (!participating) return ready_for_run(false, {}, run);") !=
              std::string::npos,
          "nonparticipant must bypass client and DAQReady lookup");
  std::cout << "test_global_busy_readiness: passed\n";
}
