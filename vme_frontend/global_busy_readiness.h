#pragma once

namespace global_busy {

struct ParticipantReadiness {
  bool connected;
  bool daq_ready;
  int ready_run_number;
};

// Nonparticipants are outside this run's readiness contract.
inline bool ready_for_run(bool participating, ParticipantReadiness state,
                          int run_number) {
  return !participating ||
         (state.connected && state.daq_ready &&
          state.ready_run_number == run_number);
}

}  // namespace global_busy
