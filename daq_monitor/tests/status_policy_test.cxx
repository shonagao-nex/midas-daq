#include "status_policy.h"

#include <cstdio>
#include <string>

namespace {

int gChecks = 0;
int gFailures = 0;

void expect(bool condition, const char* expression, int line) {
  ++gChecks;
  if (!condition) {
    ++gFailures;
    std::fprintf(stderr, "line %d: expectation failed: %s\n", line,
                 expression);
  }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

using daq_monitor::ComponentStatus;
using daq_monitor::EasirocRawStatus;
using daq_monitor::RawStatus;
using daq_monitor::RunParticipation;
using daq_monitor::RunState;
using daq_monitor::Severity;
using daq_monitor::VmeRawStatus;

void expect_severity(const ComponentStatus& result, Severity expected) {
  EXPECT(result.severity == expected);
}

RawStatus normal_raw(RunState run_state = RunState::kRunning) {
  RawStatus raw;
  raw.run_state = run_state;
  raw.monitor_status_fresh = true;
  raw.disk_free_gb = 100.0;
  raw.logger_connected = true;
  raw.vme.connected = true;
  raw.vme.status_fresh = true;
  raw.vme.acquisition_expected = true;
  raw.vme.acquisition_running = true;
  raw.easiroc.connected = true;
  raw.easiroc.status_fresh = true;
  raw.easiroc.acquisition_running = true;
  raw.vme_configuration = {true, 42, 1000};
  raw.easiroc_configuration = {true, 42, 1000};
  return raw;
}

void test_disk() {
  using daq_monitor::evaluate_disk;
  expect_severity(evaluate_disk(51.0), Severity::kOk);
  expect_severity(evaluate_disk(50.0), Severity::kOk);
  expect_severity(evaluate_disk(49.9), Severity::kWarning);
  expect_severity(evaluate_disk(10.0), Severity::kWarning);
  expect_severity(evaluate_disk(9.9), Severity::kError);
}

void test_logger() {
  using daq_monitor::evaluate_logger;
  expect_severity(evaluate_logger(true), Severity::kOk);
  expect_severity(evaluate_logger(false), Severity::kWarning);
}

void test_frontend_connectivity() {
  using daq_monitor::evaluate_easiroc;
  using daq_monitor::evaluate_vme;
  VmeRawStatus vme;
  EasirocRawStatus easiroc;

  vme.connected = true;
  vme.status_fresh = true;
  easiroc.connected = true;
  easiroc.status_fresh = true;
  easiroc.acquisition_running = true;
  expect_severity(evaluate_vme(RunState::kRunning, vme), Severity::kOk);
  expect_severity(evaluate_easiroc(RunState::kRunning, easiroc),
                  Severity::kOk);

  vme.connected = false;
  easiroc.connected = false;
  expect_severity(evaluate_vme(RunState::kRunning, vme), Severity::kError);
  expect_severity(evaluate_easiroc(RunState::kRunning, easiroc),
                  Severity::kError);
  expect_severity(evaluate_vme(RunState::kStopped, vme), Severity::kWarning);
  expect_severity(evaluate_easiroc(RunState::kStopped, easiroc),
                  Severity::kWarning);

  vme.connected = true;
  easiroc.connected = true;
  easiroc.acquisition_running = false;
  expect_severity(evaluate_vme(RunState::kStopped, vme), Severity::kOk);
  expect_severity(evaluate_easiroc(RunState::kStopped, easiroc),
                  Severity::kOk);

  vme.connected = false;
  easiroc.connected = false;
  expect_severity(evaluate_vme(RunState::kPausedOrTransition, vme),
                  Severity::kError);
  expect_severity(evaluate_easiroc(RunState::kPausedOrTransition, easiroc),
                  Severity::kError);
}

void test_vme_integrity() {
  using daq_monitor::evaluate_vme;
  VmeRawStatus raw;
  raw.connected = true;
  raw.status_fresh = true;
  expect_severity(evaluate_vme(RunState::kRunning, raw), Severity::kOk);

  raw.event_slip_count = 1;
  expect_severity(evaluate_vme(RunState::kRunning, raw), Severity::kError);
  expect_severity(evaluate_vme(RunState::kPausedOrTransition, raw),
                  Severity::kError);
  expect_severity(evaluate_vme(RunState::kStopped, raw), Severity::kOk);
  EXPECT(evaluate_vme(RunState::kRunning, raw).reason ==
         "VME event slip detected");
  raw = VmeRawStatus{};
  raw.connected = true;
  raw.status_fresh = true;
  raw.malformed_event_count = 1;
  expect_severity(evaluate_vme(RunState::kRunning, raw), Severity::kError);
  raw = VmeRawStatus{};
  raw.connected = true;
  raw.status_fresh = true;
  raw.timeout_count = 1;
  expect_severity(evaluate_vme(RunState::kRunning, raw), Severity::kError);
  raw = VmeRawStatus{};
  raw.connected = true;
  raw.status_fresh = true;
  raw.event_content_error_count = 1;
  expect_severity(evaluate_vme(RunState::kRunning, raw), Severity::kError);
  raw = VmeRawStatus{};
  raw.connected = true;
  raw.status_fresh = true;
  raw.counter_discontinuity_count = 1;
  expect_severity(evaluate_vme(RunState::kRunning, raw), Severity::kError);
}

void test_easiroc_integrity() {
  using daq_monitor::evaluate_easiroc;
  EasirocRawStatus raw;
  raw.connected = true;
  raw.status_fresh = true;
  raw.acquisition_running = true;
  expect_severity(evaluate_easiroc(RunState::kRunning, raw), Severity::kOk);

  raw.acquisition_fault = true;
  expect_severity(evaluate_easiroc(RunState::kRunning, raw), Severity::kError);
  EXPECT(evaluate_easiroc(RunState::kRunning, raw).reason ==
         "EASIROC acquisition fault");
  raw = EasirocRawStatus{};
  raw.connected = true;
  raw.status_fresh = true;
  raw.acquisition_running = false;
  expect_severity(evaluate_easiroc(RunState::kRunning, raw), Severity::kError);
  raw = EasirocRawStatus{};
  raw.connected = true;
  raw.status_fresh = true;
  raw.acquisition_running = true;
  raw.decode_error_count = 1;
  expect_severity(evaluate_easiroc(RunState::kRunning, raw), Severity::kError);
  raw = EasirocRawStatus{};
  raw.connected = true;
  raw.status_fresh = true;
  raw.acquisition_running = true;
  raw.timeout_count = 1;
  expect_severity(evaluate_easiroc(RunState::kRunning, raw), Severity::kError);
  raw = EasirocRawStatus{};
  raw.connected = true;
  raw.status_fresh = true;
  raw.acquisition_running = true;
  raw.overflow_count = 1;
  expect_severity(evaluate_easiroc(RunState::kRunning, raw),
                  Severity::kWarning);
  EXPECT(evaluate_easiroc(RunState::kRunning, raw).reason ==
         "EASIROC ADC over-threshold flag detected");
  expect_severity(evaluate_easiroc(RunState::kPausedOrTransition, raw),
                  Severity::kWarning);
  expect_severity(evaluate_easiroc(RunState::kStopped, raw), Severity::kOk);
  raw.event_content_error_count = 1;
  expect_severity(evaluate_easiroc(RunState::kRunning, raw), Severity::kError);
  raw.event_content_error_count = 0;
  raw.connected = false;
  raw.status_fresh = false;
  expect_severity(evaluate_easiroc(RunState::kRunning, raw), Severity::kError);
  raw = EasirocRawStatus{};
  raw.connected = true;
  raw.status_fresh = true;
  raw.acquisition_running = true;
  raw.event_content_error_count = 1;
  expect_severity(evaluate_easiroc(RunState::kRunning, raw), Severity::kError);
}

void test_stop_transition_acquisition_policy() {
  using daq_monitor::evaluate_status;

  // A participating frontend that unexpectedly stops acquisition during an
  // ordinary RUNNING interval remains an ERROR for both detector systems.
  RawStatus raw = normal_raw();
  raw.vme.acquisition_running = false;
  raw.easiroc.acquisition_running = false;
  auto evaluated = evaluate_status(raw);
  EXPECT(evaluated.vme.severity == Severity::kError);
  EXPECT(evaluated.vme.reason == "VME acquisition not running");
  EXPECT(evaluated.easiroc.severity == Severity::kError);
  EXPECT(evaluated.easiroc.reason ==
         "EASIROC acquisition not running");

  // EOR is allowed to turn acquisition off while Runinfo still says RUNNING.
  raw.stop_transition_in_progress = true;
  evaluated = evaluate_status(raw);
  EXPECT(evaluated.vme.severity == Severity::kOk);
  EXPECT(evaluated.easiroc.severity == Severity::kOk);
  EXPECT(evaluated.global_severity == Severity::kOk);

  // The STOP exception is deliberately narrow: disconnect, hardware fault,
  // and data-integrity failures are still visible.
  raw.vme.connected = false;
  raw.vme.status_fresh = false;
  evaluated = evaluate_status(raw);
  EXPECT(evaluated.vme.severity == Severity::kError);
  EXPECT(evaluated.vme.reason == "VME disconnected while running");
  raw = normal_raw();
  raw.stop_transition_in_progress = true;
  raw.easiroc.connected = false;
  raw.easiroc.status_fresh = false;
  evaluated = evaluate_status(raw);
  EXPECT(evaluated.easiroc.severity == Severity::kError);
  EXPECT(evaluated.easiroc.reason ==
         "EASIROC disconnected while running");

  raw = normal_raw();
  raw.stop_transition_in_progress = true;
  raw.vme.acquisition_running = false;
  raw.vme.event_slip_count = 1;
  raw.easiroc.acquisition_running = false;
  raw.easiroc.acquisition_fault = true;
  evaluated = evaluate_status(raw);
  EXPECT(evaluated.vme.severity == Severity::kError);
  EXPECT(evaluated.vme.reason == "VME event slip detected");
  EXPECT(evaluated.easiroc.severity == Severity::kError);
  EXPECT(evaluated.easiroc.reason == "EASIROC acquisition fault");

  // Once STOPPED, the normal STOPPED connectivity/acquisition policy applies.
  raw = normal_raw(RunState::kStopped);
  raw.stop_transition_in_progress = false;
  raw.vme.acquisition_running = false;
  raw.easiroc.acquisition_running = false;
  evaluated = evaluate_status(raw);
  EXPECT(evaluated.vme.severity == Severity::kOk);
  EXPECT(evaluated.easiroc.severity == Severity::kOk);
  raw.vme.connected = false;
  raw.easiroc.connected = false;
  evaluated = evaluate_status(raw);
  EXPECT(evaluated.vme.severity == Severity::kWarning);
  EXPECT(evaluated.easiroc.severity == Severity::kWarning);
}

void test_can_start() {
  using daq_monitor::evaluate_can_start;
  RawStatus raw = normal_raw(RunState::kStopped);
  EXPECT(evaluate_can_start(raw).allowed);
  EXPECT(evaluate_can_start(raw).reason.empty());

  // A single connected, fresh frontend is a valid DAQ configuration.
  raw.vme.connected = false;
  raw.vme.status_fresh = false;
  EXPECT(evaluate_can_start(raw).allowed);
  raw = normal_raw(RunState::kStopped);
  raw.easiroc.connected = false;
  raw.easiroc.status_fresh = false;
  EXPECT(evaluate_can_start(raw).allowed);

  raw = normal_raw(RunState::kStopped);
  raw.vme.connected = false;
  raw.vme.status_fresh = false;
  raw.easiroc.connected = false;
  raw.easiroc.status_fresh = false;
  EXPECT(!evaluate_can_start(raw).allowed);
  EXPECT(evaluate_can_start(raw).reason == "No DAQ frontend running");

  raw = normal_raw(RunState::kStopped);
  raw.disk_free_gb = 10.0;
  EXPECT(evaluate_can_start(raw).allowed);
  raw.disk_free_gb = 9.9;
  EXPECT(!evaluate_can_start(raw).allowed);
  EXPECT(evaluate_can_start(raw).reason == "Disk free below 10 GB");
  raw.disk_free_gb = -1.0;
  EXPECT(!evaluate_can_start(raw).allowed);
  EXPECT(evaluate_can_start(raw).reason == "Disk free unavailable");

  raw = normal_raw(RunState::kStopped);
  raw.logger_connected = false;
  EXPECT(evaluate_can_start(raw).allowed);
  raw = normal_raw(RunState::kStopped);
  raw.vme.event_slip_count = 1;
  EXPECT(evaluate_can_start(raw).allowed);
  raw = normal_raw(RunState::kStopped);
  raw.easiroc.decode_error_count = 1;
  EXPECT(evaluate_can_start(raw).allowed);

  raw = normal_raw(RunState::kStopped);
  raw.monitor_status_fresh = false;
  raw.vme.connected = false;
  EXPECT(!evaluate_can_start(raw).allowed);
  EXPECT(evaluate_can_start(raw).reason == "Monitor status stale");
  raw = normal_raw(RunState::kStopped);
  raw.easiroc.connected = false;
  raw.easiroc.status_fresh = false;
  raw.vme.status_fresh = false;
  EXPECT(!evaluate_can_start(raw).allowed);
  EXPECT(evaluate_can_start(raw).reason == "VME frontend status stale");
  raw = normal_raw(RunState::kStopped);
  raw.vme.connected = false;
  raw.vme.status_fresh = false;
  raw.easiroc.status_fresh = false;
  EXPECT(!evaluate_can_start(raw).allowed);
  EXPECT(evaluate_can_start(raw).reason ==
         "EASIROC frontend status stale");

  // Stale or failed configuration metadata is diagnostic only. Each connected
  // frontend establishes the current run's configuration in its own BOR.
  raw = normal_raw(RunState::kStopped);
  raw.vme.connected = false;
  raw.vme.status_fresh = false;
  raw.vme_configuration = {true, 41, 999};
  raw.easiroc_configuration = {false, 41, 999};
  EXPECT(evaluate_can_start(raw).allowed);

  raw = normal_raw(RunState::kStopped);
  raw.vme_configuration = {};
  raw.easiroc_configuration = {};
  EXPECT(evaluate_can_start(raw).allowed);
}

void test_run_participation() {
  using daq_monitor::evaluate_status;
  using daq_monitor::resolve_run_participation;

  // VME-only: stale state and old errors from the non-participant are ignored.
  RawStatus raw = normal_raw();
  raw.easiroc.participating = false;
  raw.easiroc.connected = false;
  raw.easiroc.status_fresh = false;
  raw.easiroc.acquisition_running = false;
  raw.easiroc.acquisition_fault = true;
  raw.easiroc.decode_error_count = 3;
  EXPECT(evaluate_status(raw).vme.severity == Severity::kOk);
  EXPECT(evaluate_status(raw).easiroc.severity == Severity::kOk);
  EXPECT(evaluate_status(raw).easiroc.reason ==
         "EASIROC not participating in current run");
  EXPECT(evaluate_status(raw).global_severity == Severity::kOk);

  // EASIROC-only: stale VME counters from a previous run are ignored.
  raw = normal_raw();
  raw.vme.participating = false;
  raw.vme.connected = false;
  raw.vme.status_fresh = false;
  raw.vme.event_slip_count = 4;
  raw.vme.timeout_count = 2;
  EXPECT(evaluate_status(raw).vme.severity == Severity::kOk);
  EXPECT(evaluate_status(raw).vme.reason ==
         "VME not participating in current run");
  EXPECT(evaluate_status(raw).easiroc.severity == Severity::kOk);
  EXPECT(evaluate_status(raw).global_severity == Severity::kOk);

  // A participant remains monitored after disconnecting.
  raw = normal_raw();
  raw.vme.connected = false;
  raw.vme.status_fresh = false;
  EXPECT(evaluate_status(raw).vme.severity == Severity::kError);
  EXPECT(evaluate_status(raw).vme.reason ==
         "VME disconnected while running");
  EXPECT(evaluate_status(raw).global_severity == Severity::kError);

  raw = normal_raw();
  raw.easiroc.connected = false;
  raw.easiroc.status_fresh = false;
  EXPECT(evaluate_status(raw).easiroc.severity == Severity::kError);
  EXPECT(evaluate_status(raw).easiroc.reason ==
         "EASIROC disconnected while running");
  EXPECT(evaluate_status(raw).global_severity == Severity::kError);

  // Exact run-number matching is mandatory. A previous run's participant
  // record cannot suppress monitoring in the current run.
  RunParticipation recorded{true, 52, true, false};
  auto active = resolve_run_participation(RunState::kRunning, 52, recorded);
  EXPECT(active.known);
  EXPECT(active.vme);
  EXPECT(!active.easiroc);

  active = resolve_run_participation(RunState::kRunning, 53, recorded);
  EXPECT(!active.known);
  EXPECT(active.vme);
  EXPECT(active.easiroc);

  recorded.valid = false;
  active = resolve_run_participation(RunState::kRunning, 52, recorded);
  EXPECT(!active.known);
  EXPECT(active.vme);
  EXPECT(active.easiroc);

  active = resolve_run_participation(RunState::kStopped, 52, recorded);
  EXPECT(active.known);
  EXPECT(!active.vme);
  EXPECT(!active.easiroc);
}

void test_transition_sequence() {
  EXPECT(daq_monitor::kStartTransitionSequence == 400);
}

void test_global() {
  using daq_monitor::evaluate_status;
  RawStatus raw = normal_raw();
  EXPECT(evaluate_status(raw).global_severity == Severity::kOk);
  EXPECT(evaluate_status(raw).global_summary == "DAQ OK");

  raw.easiroc.overflow_count = 1;
  EXPECT(evaluate_status(raw).easiroc.severity == Severity::kWarning);
  EXPECT(evaluate_status(raw).global_severity == Severity::kWarning);
  EXPECT(evaluate_status(raw).global_summary ==
         "EASIROC ADC over-threshold flag detected");
  raw.easiroc.overflow_count = 0;

  raw.logger_connected = false;
  EXPECT(evaluate_status(raw).global_severity == Severity::kWarning);
  EXPECT(evaluate_status(raw).global_summary == "Logger disconnected");

  raw.vme.event_slip_count = 1;
  EXPECT(evaluate_status(raw).global_severity == Severity::kError);
  EXPECT(evaluate_status(raw).global_summary == "VME event slip detected");

  raw.easiroc.acquisition_fault = true;
  EXPECT(evaluate_status(raw).global_severity == Severity::kError);
  EXPECT(evaluate_status(raw).global_summary == "VME event slip detected");
}

}  // namespace

int main() {
  test_disk();
  test_logger();
  test_frontend_connectivity();
  test_vme_integrity();
  test_easiroc_integrity();
  test_stop_transition_acquisition_policy();
  test_global();
  test_run_participation();
  test_can_start();
  test_transition_sequence();

  if (gFailures != 0) {
    std::fprintf(stderr, "status_policy_test: %d of %d checks failed\n",
                 gFailures, gChecks);
    return 1;
  }
  std::printf("status_policy_test: %d checks passed\n", gChecks);
  return 0;
}
