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
using daq_monitor::ConfigurationRawStatus;
using daq_monitor::EasirocRawStatus;
using daq_monitor::RawStatus;
using daq_monitor::RunState;
using daq_monitor::Severity;
using daq_monitor::StartEvaluation;
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
  expect_severity(evaluate_easiroc(RunState::kRunning, raw), Severity::kError);
  expect_severity(evaluate_easiroc(RunState::kPausedOrTransition, raw),
                  Severity::kError);
  expect_severity(evaluate_easiroc(RunState::kStopped, raw), Severity::kOk);
  raw = EasirocRawStatus{};
  raw.connected = true;
  raw.status_fresh = true;
  raw.acquisition_running = true;
  raw.event_content_error_count = 1;
  expect_severity(evaluate_easiroc(RunState::kRunning, raw), Severity::kError);
}

void test_can_start() {
  using daq_monitor::evaluate_can_start;
  RawStatus raw = normal_raw(RunState::kStopped);
  EXPECT(evaluate_can_start(raw).allowed);
  EXPECT(evaluate_can_start(raw).reason.empty());

  raw.vme.connected = false;
  EXPECT(!evaluate_can_start(raw).allowed);
  EXPECT(evaluate_can_start(raw).reason == "VME frontend disconnected");
  raw = normal_raw(RunState::kStopped);
  raw.easiroc.connected = false;
  EXPECT(!evaluate_can_start(raw).allowed);
  EXPECT(evaluate_can_start(raw).reason ==
         "EASIROC frontend disconnected");

  raw = normal_raw(RunState::kStopped);
  raw.disk_free_gb = 10.0;
  EXPECT(evaluate_can_start(raw).allowed);
  raw.disk_free_gb = 9.9;
  EXPECT(!evaluate_can_start(raw).allowed);
  EXPECT(evaluate_can_start(raw).reason == "Disk free below 10 GB");

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
  raw.vme.status_fresh = false;
  EXPECT(!evaluate_can_start(raw).allowed);
  EXPECT(evaluate_can_start(raw).reason == "VME frontend status stale");
  raw = normal_raw(RunState::kStopped);
  raw.easiroc.status_fresh = false;
  EXPECT(!evaluate_can_start(raw).allowed);
  EXPECT(evaluate_can_start(raw).reason ==
         "EASIROC frontend status stale");

  // Configuration is established inside BOR, so an unconfirmed previous
  // configuration must not make the pre-start gate reject every new run.
  raw = normal_raw(RunState::kStopped);
  raw.vme_configuration = {};
  raw.easiroc_configuration = {};
  EXPECT(evaluate_can_start(raw).allowed);
}

void test_bor_configuration() {
  using daq_monitor::evaluate_bor_configuration;
  ConfigurationRawStatus vme{true, 43, 1001};
  ConfigurationRawStatus easiroc{true, 43, 1002};
  EXPECT(evaluate_bor_configuration(43, vme, easiroc).ready);

  vme.ok = false;
  EXPECT(!evaluate_bor_configuration(43, vme, easiroc).ready);
  EXPECT(evaluate_bor_configuration(43, vme, easiroc).reason ==
         "VME configuration not valid for run 43");
  vme = {true, 42, 1001};
  EXPECT(!evaluate_bor_configuration(43, vme, easiroc).ready);
  vme = {true, 43, 0};
  easiroc.ok = false;
  EXPECT(!evaluate_bor_configuration(43, vme, easiroc).ready);
  EXPECT(evaluate_bor_configuration(43, vme, easiroc).reason ==
         "EASIROC configuration not valid for run 43");
  easiroc = {true, 42, 1002};
  EXPECT(!evaluate_bor_configuration(43, vme, easiroc).ready);

  // CheckedUnix is diagnostic metadata. The run-number match prevents a
  // previous run's successful configuration from passing this gate.
  easiroc = {true, 43, 0};
  EXPECT(evaluate_bor_configuration(43, vme, easiroc).ready);
}

void test_start_evaluation() {
  using daq_monitor::evaluate_can_start;
  using daq_monitor::evaluate_start;

  RawStatus raw = normal_raw(RunState::kStopped);
  const ConfigurationRawStatus valid{true, 42, 1000};
  StartEvaluation result =
      evaluate_start(42, evaluate_can_start(raw), valid, valid);
  EXPECT(result.allowed);
  EXPECT(result.reason.empty());

  raw.monitor_status_fresh = false;
  result = evaluate_start(
      42, evaluate_can_start(raw),
      ConfigurationRawStatus{false, 41, 0},
      ConfigurationRawStatus{false, 41, 0});
  EXPECT(!result.allowed);
  EXPECT(result.reason == "Monitor status stale");

  raw = normal_raw(RunState::kStopped);
  result = evaluate_start(42, evaluate_can_start(raw),
                          ConfigurationRawStatus{false, 42, 1000}, valid);
  EXPECT(!result.allowed);
  EXPECT(result.reason == "VME configuration not valid for run 42");
  result = evaluate_start(42, evaluate_can_start(raw), valid,
                          ConfigurationRawStatus{false, 42, 1000});
  EXPECT(!result.allowed);
  EXPECT(result.reason == "EASIROC configuration not valid for run 42");
  result = evaluate_start(42, evaluate_can_start(raw),
                          ConfigurationRawStatus{true, 41, 1000}, valid);
  EXPECT(!result.allowed);
  EXPECT(result.reason == "VME configuration not valid for run 42");
  result = evaluate_start(42, evaluate_can_start(raw), valid,
                          ConfigurationRawStatus{true, 41, 1000});
  EXPECT(!result.allowed);
  EXPECT(result.reason == "EASIROC configuration not valid for run 42");

  result = evaluate_start(
      42, evaluate_can_start(raw),
      ConfigurationRawStatus{false, 42, 1000},
      ConfigurationRawStatus{false, 42, 1000});
  EXPECT(result.reason == "VME configuration not valid for run 42");

  raw.logger_connected = false;
  raw.vme.event_slip_count = 1;
  raw.easiroc.decode_error_count = 1;
  result = evaluate_start(42, evaluate_can_start(raw), valid, valid);
  EXPECT(result.allowed);

  raw.disk_free_gb = 10.0;
  EXPECT(evaluate_start(42, evaluate_can_start(raw), valid, valid).allowed);
  raw.disk_free_gb = 9.9;
  result = evaluate_start(42, evaluate_can_start(raw), valid, valid);
  EXPECT(!result.allowed);
  EXPECT(result.reason == "Disk free below 10 GB");
}

void test_global() {
  using daq_monitor::evaluate_status;
  RawStatus raw = normal_raw();
  EXPECT(evaluate_status(raw).global_severity == Severity::kOk);
  EXPECT(evaluate_status(raw).global_summary == "DAQ OK");

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
  test_global();
  test_can_start();
  test_bor_configuration();
  test_start_evaluation();

  if (gFailures != 0) {
    std::fprintf(stderr, "status_policy_test: %d of %d checks failed\n",
                 gFailures, gChecks);
    return 1;
  }
  std::printf("status_policy_test: %d checks passed\n", gChecks);
  return 0;
}
