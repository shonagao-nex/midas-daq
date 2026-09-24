#include "alarm_policy.h"
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

using daq_monitor::AlarmDecision;
using daq_monitor::AlarmLevel;
using daq_monitor::AlarmObservation;
using daq_monitor::AlarmRuntimeState;
using daq_monitor::RawStatus;
using daq_monitor::RunState;

struct FakeAlarmApi {
  int trigger_count = 0;
  int reset_count = 0;
  bool active = false;
  AlarmLevel level = AlarmLevel::kInactive;

  void apply(const AlarmDecision& decision) {
    if (decision.reset) {
      ++reset_count;
      active = false;
    }
    if (decision.trigger) {
      ++trigger_count;
      active = true;
      level = decision.trigger_level;
    }
  }
};

AlarmObservation observation(const char* component, const char* severity,
                             const char* reason) {
  return {component, severity, reason, {}, 1700000000};
}

void apply_observation(AlarmRuntimeState* state, FakeAlarmApi* api,
                       const AlarmObservation& value,
                       bool alarm_system_active = true) {
  const AlarmDecision decision =
      daq_monitor::decide_alarm_transition(*state, value,
                                           alarm_system_active);
  api->apply(decision);
  *state = decision.next_state;
}

RawStatus normal_raw() {
  RawStatus raw;
  raw.run_state = RunState::kRunning;
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
  return raw;
}

void test_vme_lifecycle() {
  AlarmRuntimeState state;
  FakeAlarmApi api;

  apply_observation(&state, &api, observation("VME", "OK", "VME OK"));
  EXPECT(!api.active);
  EXPECT(api.trigger_count == 0);

  apply_observation(
      &state, &api,
      observation("VME", "ERROR", "VME event slip detected"));
  EXPECT(api.active);
  EXPECT(api.level == AlarmLevel::kError);
  EXPECT(api.trigger_count == 1);

  apply_observation(
      &state, &api,
      observation("VME", "ERROR", "VME event slip detected"));
  EXPECT(api.trigger_count == 1);

  apply_observation(&state, &api, observation("VME", "OK", "VME OK"));
  EXPECT(!api.active);
  EXPECT(api.reset_count == 2);  // initial stale clear plus ERROR -> OK
}

void test_warning_and_escalation() {
  AlarmRuntimeState state;
  FakeAlarmApi api;
  apply_observation(
      &state, &api,
      observation("Logger", "WARNING", "Logger disconnected"));
  EXPECT(api.active);
  EXPECT(api.level == AlarmLevel::kWarning);

  apply_observation(
      &state, &api,
      observation("Logger", "ERROR", "Logger unavailable"));
  EXPECT(api.reset_count == 2);
  EXPECT(api.trigger_count == 2);
  EXPECT(api.level == AlarmLevel::kError);
}

void test_alarm_system_off_then_on() {
  AlarmRuntimeState state;
  FakeAlarmApi api;
  const auto error = observation("VME", "ERROR", "VME event slip detected");

  apply_observation(&state, &api, error, false);
  EXPECT(!api.active);
  EXPECT(api.trigger_count == 0);
  EXPECT(!state.initialized);

  apply_observation(&state, &api, error, true);
  EXPECT(api.active);
  EXPECT(api.trigger_count == 1);

  apply_observation(&state, &api, error, false);
  EXPECT(!api.active);
  EXPECT(!state.initialized);
  apply_observation(&state, &api, observation("VME", "OK", "VME OK"),
                    false);
  apply_observation(&state, &api, observation("VME", "OK", "VME OK"),
                    true);
  EXPECT(!api.active);
  EXPECT(api.trigger_count == 1);
}

void test_policy_inputs() {
  using daq_monitor::evaluate_status;
  using daq_monitor::severity_name;

  RawStatus raw = normal_raw();
  raw.logger_connected = false;
  auto evaluated = evaluate_status(raw);
  EXPECT(evaluated.logger.severity == daq_monitor::Severity::kWarning);
  EXPECT(daq_monitor::alarm_level_from_severity(
             severity_name(evaluated.logger.severity)) ==
         AlarmLevel::kWarning);

  raw = normal_raw();
  raw.disk_free_gb = 49.9;
  evaluated = evaluate_status(raw);
  EXPECT(daq_monitor::alarm_level_from_severity(
             severity_name(evaluated.disk.severity)) ==
         AlarmLevel::kWarning);

  raw.disk_free_gb = 9.9;
  evaluated = evaluate_status(raw);
  EXPECT(daq_monitor::alarm_level_from_severity(
             severity_name(evaluated.disk.severity)) ==
         AlarmLevel::kError);

  raw = normal_raw();
  raw.vme.event_slip_count = 1;
  evaluated = evaluate_status(raw);
  AlarmRuntimeState state;
  FakeAlarmApi api;
  apply_observation(
      &state, &api,
      observation("VME", severity_name(evaluated.vme.severity),
                  evaluated.vme.reason.c_str()));
  EXPECT(api.active);

  // A BOR-equivalent counter reset makes the existing policy return OK,
  // which is sufficient for the alarm mapper to clear the component alarm.
  raw.vme.event_slip_count = 0;
  evaluated = evaluate_status(raw);
  apply_observation(
      &state, &api,
      observation("VME", severity_name(evaluated.vme.severity),
                  evaluated.vme.reason.c_str()));
  EXPECT(!api.active);

  raw = normal_raw();
  raw.run_state = RunState::kStopped;
  raw.vme.connected = false;
  evaluated = evaluate_status(raw);
  EXPECT(daq_monitor::alarm_level_from_severity(
             severity_name(evaluated.vme.severity)) ==
         AlarmLevel::kWarning);

  // Status remains WARNING, but the STOPPED frontend-disconnect condition is
  // deliberately mapped to an inactive component alarm.
  EXPECT(daq_monitor::suppress_frontend_disconnect_alarm(
      raw.run_state, raw.vme.connected, severity_name(evaluated.vme.severity),
      evaluated.vme.reason));
  AlarmRuntimeState stopped_vme_state;
  FakeAlarmApi stopped_vme_api;
  AlarmObservation stopped_vme =
      observation("VME", severity_name(evaluated.vme.severity),
                  evaluated.vme.reason.c_str());
  stopped_vme.alarm_suppressed = true;
  apply_observation(&stopped_vme_state, &stopped_vme_api, stopped_vme);
  EXPECT(!stopped_vme_api.active);

  raw = normal_raw();
  raw.run_state = RunState::kStopped;
  raw.easiroc.connected = false;
  evaluated = evaluate_status(raw);
  EXPECT(daq_monitor::suppress_frontend_disconnect_alarm(
      raw.run_state, raw.easiroc.connected,
      severity_name(evaluated.easiroc.severity), evaluated.easiroc.reason));
  AlarmRuntimeState stopped_easiroc_state;
  FakeAlarmApi stopped_easiroc_api;
  AlarmObservation stopped_easiroc =
      observation("EASIROC", severity_name(evaluated.easiroc.severity),
                  evaluated.easiroc.reason.c_str());
  stopped_easiroc.alarm_suppressed = true;
  apply_observation(&stopped_easiroc_state, &stopped_easiroc_api,
                    stopped_easiroc);
  EXPECT(!stopped_easiroc_api.active);

  raw = normal_raw();
  raw.vme.connected = false;
  evaluated = evaluate_status(raw);
  EXPECT(!daq_monitor::suppress_frontend_disconnect_alarm(
      raw.run_state, raw.vme.connected, severity_name(evaluated.vme.severity),
      evaluated.vme.reason));
  AlarmRuntimeState running_state;
  FakeAlarmApi running_api;
  apply_observation(&running_state, &running_api,
                    observation("VME", severity_name(evaluated.vme.severity),
                                evaluated.vme.reason.c_str()));
  EXPECT(running_api.active);

  raw = normal_raw();
  raw.easiroc.connected = false;
  evaluated = evaluate_status(raw);
  AlarmRuntimeState running_easiroc_state;
  FakeAlarmApi running_easiroc_api;
  apply_observation(
      &running_easiroc_state, &running_easiroc_api,
      observation("EASIROC", severity_name(evaluated.easiroc.severity),
                  evaluated.easiroc.reason.c_str()));
  EXPECT(running_easiroc_api.active);

  raw = normal_raw();
  raw.run_state = RunState::kPausedOrTransition;
  raw.vme.connected = false;
  evaluated = evaluate_status(raw);
  EXPECT(!daq_monitor::suppress_frontend_disconnect_alarm(
      raw.run_state, raw.vme.connected, severity_name(evaluated.vme.severity),
      evaluated.vme.reason));
  AlarmRuntimeState paused_state;
  FakeAlarmApi paused_api;
  apply_observation(&paused_state, &paused_api,
                    observation("VME", severity_name(evaluated.vme.severity),
                                evaluated.vme.reason.c_str()));
  EXPECT(paused_api.active);

  raw = normal_raw();
  raw.run_state = RunState::kPausedOrTransition;
  raw.easiroc.connected = false;
  evaluated = evaluate_status(raw);
  AlarmRuntimeState paused_easiroc_state;
  FakeAlarmApi paused_easiroc_api;
  apply_observation(
      &paused_easiroc_state, &paused_easiroc_api,
      observation("EASIROC", severity_name(evaluated.easiroc.severity),
                  evaluated.easiroc.reason.c_str()));
  EXPECT(paused_easiroc_api.active);

  // An active frontend alarm is reset when it transitions to the narrowly
  // suppressed STOPPED-disconnect warning.
  AlarmRuntimeState transition_state;
  FakeAlarmApi transition_api;
  apply_observation(&transition_state, &transition_api,
                    observation("VME", "ERROR", "VME disconnected while running"));
  AlarmObservation suppressed = observation(
      "VME", "WARNING", "VME disconnected while stopped");
  suppressed.alarm_suppressed = true;
  apply_observation(&transition_state, &transition_api, suppressed);
  EXPECT(!transition_api.active);
  EXPECT(transition_api.reset_count == 2);

  EXPECT(!daq_monitor::suppress_frontend_disconnect_alarm(
      RunState::kStopped, false, "WARNING", "Logger disconnected"));
  EXPECT(!daq_monitor::suppress_frontend_disconnect_alarm(
      RunState::kStopped, false, "WARNING", "Disk free below 50 GB"));

  raw = normal_raw();
  raw.easiroc.acquisition_fault = true;
  evaluated = evaluate_status(raw);
  EXPECT(daq_monitor::alarm_level_from_severity(
             severity_name(evaluated.easiroc.severity)) ==
         AlarmLevel::kError);

  // A disconnected non-participant maps to OK and therefore cannot trigger a
  // component alarm. A participant disconnect still maps to ERROR/alarm.
  raw = normal_raw();
  raw.easiroc.participating = false;
  raw.easiroc.connected = false;
  raw.easiroc.status_fresh = false;
  raw.easiroc.acquisition_running = false;
  raw.easiroc.acquisition_fault = true;
  evaluated = evaluate_status(raw);
  EXPECT(evaluated.easiroc.severity == daq_monitor::Severity::kOk);
  AlarmRuntimeState nonparticipant_state;
  FakeAlarmApi nonparticipant_api;
  apply_observation(
      &nonparticipant_state, &nonparticipant_api,
      observation("EASIROC", severity_name(evaluated.easiroc.severity),
                  evaluated.easiroc.reason.c_str()));
  EXPECT(!nonparticipant_api.active);
  EXPECT(nonparticipant_api.trigger_count == 0);

  AlarmRuntimeState prior_error_state;
  FakeAlarmApi prior_error_api;
  apply_observation(
      &prior_error_state, &prior_error_api,
      observation("EASIROC", "ERROR", "EASIROC decode error detected"));
  apply_observation(
      &prior_error_state, &prior_error_api,
      observation("EASIROC", severity_name(evaluated.easiroc.severity),
                  evaluated.easiroc.reason.c_str()));
  EXPECT(!prior_error_api.active);
  EXPECT(prior_error_api.reset_count == 2);

  raw = normal_raw();
  raw.vme.participating = false;
  raw.vme.connected = false;
  raw.vme.status_fresh = false;
  raw.vme.event_slip_count = 9;
  evaluated = evaluate_status(raw);
  EXPECT(evaluated.vme.severity == daq_monitor::Severity::kOk);
  AlarmRuntimeState vme_nonparticipant_state;
  FakeAlarmApi vme_nonparticipant_api;
  apply_observation(
      &vme_nonparticipant_state, &vme_nonparticipant_api,
      observation("VME", severity_name(evaluated.vme.severity),
                  evaluated.vme.reason.c_str()));
  EXPECT(!vme_nonparticipant_api.active);

  raw = normal_raw();
  raw.easiroc.connected = false;
  raw.easiroc.status_fresh = false;
  evaluated = evaluate_status(raw);
  EXPECT(evaluated.easiroc.severity == daq_monitor::Severity::kError);
  AlarmRuntimeState participant_state;
  FakeAlarmApi participant_api;
  apply_observation(
      &participant_state, &participant_api,
      observation("EASIROC", severity_name(evaluated.easiroc.severity),
                  evaluated.easiroc.reason.c_str()));
  EXPECT(participant_api.active);
  EXPECT(participant_api.level == AlarmLevel::kError);

  raw = normal_raw();
  raw.vme.acquisition_running = false;
  raw.easiroc.acquisition_running = false;
  evaluated = evaluate_status(raw);
  AlarmRuntimeState running_vme_acquisition_state;
  FakeAlarmApi running_vme_acquisition_api;
  apply_observation(
      &running_vme_acquisition_state, &running_vme_acquisition_api,
      observation("VME", severity_name(evaluated.vme.severity),
                  evaluated.vme.reason.c_str()));
  EXPECT(running_vme_acquisition_api.active);
  EXPECT(running_vme_acquisition_api.level == AlarmLevel::kError);
  AlarmRuntimeState running_easiroc_acquisition_state;
  FakeAlarmApi running_easiroc_acquisition_api;
  apply_observation(
      &running_easiroc_acquisition_state,
      &running_easiroc_acquisition_api,
      observation("EASIROC", severity_name(evaluated.easiroc.severity),
                  evaluated.easiroc.reason.c_str()));
  EXPECT(running_easiroc_acquisition_api.active);
  EXPECT(running_easiroc_acquisition_api.level == AlarmLevel::kError);

  // Acquisition stopping as part of EOR must not produce a component alarm,
  // but disconnects during the same STOP transition must remain alarmable.
  raw = normal_raw();
  raw.stop_transition_in_progress = true;
  raw.vme.acquisition_running = false;
  raw.easiroc.acquisition_running = false;
  evaluated = evaluate_status(raw);
  EXPECT(evaluated.vme.severity == daq_monitor::Severity::kOk);
  EXPECT(evaluated.easiroc.severity == daq_monitor::Severity::kOk);
  AlarmRuntimeState stop_vme_state;
  FakeAlarmApi stop_vme_api;
  apply_observation(
      &stop_vme_state, &stop_vme_api,
      observation("VME", severity_name(evaluated.vme.severity),
                  evaluated.vme.reason.c_str()));
  EXPECT(!stop_vme_api.active);
  AlarmRuntimeState stop_easiroc_state;
  FakeAlarmApi stop_easiroc_api;
  apply_observation(
      &stop_easiroc_state, &stop_easiroc_api,
      observation("EASIROC", severity_name(evaluated.easiroc.severity),
                  evaluated.easiroc.reason.c_str()));
  EXPECT(!stop_easiroc_api.active);

  raw.vme.connected = false;
  raw.vme.status_fresh = false;
  raw.easiroc.connected = false;
  raw.easiroc.status_fresh = false;
  evaluated = evaluate_status(raw);
  EXPECT(evaluated.vme.severity == daq_monitor::Severity::kError);
  EXPECT(evaluated.easiroc.severity == daq_monitor::Severity::kError);
  apply_observation(
      &stop_vme_state, &stop_vme_api,
      observation("VME", severity_name(evaluated.vme.severity),
                  evaluated.vme.reason.c_str()));
  apply_observation(
      &stop_easiroc_state, &stop_easiroc_api,
      observation("EASIROC", severity_name(evaluated.easiroc.severity),
                  evaluated.easiroc.reason.c_str()));
  EXPECT(stop_vme_api.active);
  EXPECT(stop_easiroc_api.active);
}

void test_unknown_and_message() {
  AlarmRuntimeState state{true, AlarmLevel::kError};
  const AlarmDecision unknown = daq_monitor::decide_alarm_transition(
      state, observation("VME", "", "status unavailable"));
  EXPECT(!unknown.reset);
  EXPECT(!unknown.trigger);
  EXPECT(unknown.next_state.level == AlarmLevel::kError);

  AlarmObservation value =
      observation("Disk", "WARNING", "free space below 50 GB");
  value.detail = "free=42.3 GB";
  const std::string message = daq_monitor::format_alarm_message(value);
  EXPECT(message.find("Disk WARNING") != std::string::npos);
  EXPECT(message.find("free=42.3 GB") != std::string::npos);
  EXPECT(message.find("2023-11-14T22:13:20Z") != std::string::npos);
  EXPECT(message.size() < 80);

  value = observation("EASIROC", "ERROR", "EASIROC ADC overflow detected");
  value.detail = "count=3092";
  const std::string overflow_message = daq_monitor::format_alarm_message(value);
  EXPECT(overflow_message.find("EASIROC ADC overflow detected") !=
         std::string::npos);
  EXPECT(overflow_message.find("(count=3092)") != std::string::npos);
  EXPECT(overflow_message.back() == ')');
  EXPECT(std::string(daq_monitor::alarm_class_for_level(
             AlarmLevel::kWarning)) == "DAQ Warning");
  EXPECT(std::string(daq_monitor::alarm_class_for_level(
             AlarmLevel::kError)) == "DAQ Error");
}

}  // namespace

int main() {
  test_vme_lifecycle();
  test_warning_and_escalation();
  test_alarm_system_off_then_on();
  test_policy_inputs();
  test_unknown_and_message();

  if (gFailures != 0) {
    std::fprintf(stderr, "alarm_policy_test: %d of %d checks failed\n",
                 gFailures, gChecks);
    return 1;
  }
  std::printf("alarm_policy_test: %d checks passed\n", gChecks);
  return 0;
}
