#include "status_policy.h"

#include <array>
#include <string>

namespace daq_monitor {
namespace {

ComponentStatus status(Severity severity, const char* reason) {
  return {severity, reason};
}

bool is_run_active(RunState run_state) {
  // PAUSED is part of the current run. Only STOPPED excludes run-scoped
  // integrity counters from the current status.
  return run_state != RunState::kStopped;
}

}  // namespace

const char* severity_name(Severity severity) {
  switch (severity) {
    case Severity::kOk:
      return "OK";
    case Severity::kWarning:
      return "WARNING";
    case Severity::kError:
      return "ERROR";
  }
  return "ERROR";
}

ComponentStatus evaluate_disk(double free_gb) {
  if (free_gb < 0.0)
    return status(Severity::kError, "Disk free unavailable");
  if (free_gb < kDiskErrorThresholdGB)
    return status(Severity::kError, "Disk free below 10 GB");
  if (free_gb < kDiskWarningThresholdGB)
    return status(Severity::kWarning, "Disk free below 50 GB");
  return status(Severity::kOk, "Disk OK");
}

ComponentStatus evaluate_logger(bool connected) {
  if (!connected)
    return status(Severity::kWarning, "Logger disconnected");
  return status(Severity::kOk, "Logger OK");
}

ComponentStatus evaluate_vme(RunState run_state, const VmeRawStatus& raw) {
  // Data-integrity failures take precedence over connectivity. Their ordering
  // is fixed here and is also the ordering used for the VME Reason key.
  if (is_run_active(run_state) && raw.event_slip_count > 0)
    return status(Severity::kError, "VME event slip detected");
  if (is_run_active(run_state) && raw.malformed_event_count > 0)
    return status(Severity::kError, "VME malformed event detected");
  if (is_run_active(run_state) && raw.timeout_count > 0)
    return status(Severity::kError, "VME read timeout detected");
  if (is_run_active(run_state) && raw.size_error_count > 0)
    return status(Severity::kError, "VME event size error detected");
  if (is_run_active(run_state) && raw.channel_mask_error_count > 0)
    return status(Severity::kError, "VME channel mask error detected");
  if (is_run_active(run_state) && raw.event_content_error_count > 0)
    return status(Severity::kError, "VME event content error detected");
  if (is_run_active(run_state) && raw.counter_discontinuity_count > 0)
    return status(Severity::kError, "VME counter discontinuity detected");

  if (raw.connected && raw.status_fresh)
    return status(Severity::kOk, "VME OK");
  if (raw.connected)
    return status(is_run_active(run_state) ? Severity::kError
                                        : Severity::kWarning,
                  "VME status stale");
  if (is_run_active(run_state))
    return status(Severity::kError, "VME disconnected while running");
  if (run_state == RunState::kStopped)
    return status(Severity::kWarning, "VME disconnected while stopped");
  return status(Severity::kWarning, "VME disconnected outside running");
}

ComponentStatus evaluate_easiroc(RunState run_state,
                                 const EasirocRawStatus& raw) {
  // Fault and acquisition-state failures precede data-integrity failures;
  // this makes the most immediate readout failure the published reason.
  if (raw.acquisition_fault)
    return status(Severity::kError, "EASIROC acquisition fault");
  if (is_run_active(run_state) && raw.connected && raw.status_fresh &&
      !raw.acquisition_running)
    return status(Severity::kError, "EASIROC acquisition not running");
  if (is_run_active(run_state) && raw.decode_error_count > 0)
    return status(Severity::kError, "EASIROC decode error detected");
  if (is_run_active(run_state) && raw.timeout_count > 0)
    return status(Severity::kError, "EASIROC receive timeout detected");
  if (is_run_active(run_state) && raw.overflow_count > 0)
    return status(Severity::kError, "EASIROC ADC overflow detected");
  if (is_run_active(run_state) && raw.event_content_error_count > 0)
    return status(Severity::kError, "EASIROC event content error detected");

  if (raw.connected && raw.status_fresh)
    return status(Severity::kOk, "EASIROC OK");
  if (raw.connected)
    return status(is_run_active(run_state) ? Severity::kError
                                        : Severity::kWarning,
                  "EASIROC status stale");
  if (is_run_active(run_state))
    return status(Severity::kError, "EASIROC disconnected while running");
  if (run_state == RunState::kStopped)
    return status(Severity::kWarning,
                  "EASIROC disconnected while stopped");
  return status(Severity::kWarning,
                "EASIROC disconnected outside running");
}

CanStartEvaluation evaluate_can_start(const RawStatus& raw) {
  if (!raw.monitor_status_fresh)
    return {false, "Monitor status stale"};
  if (!raw.vme.connected)
    return {false, "VME frontend disconnected"};
  if (!raw.easiroc.connected)
    return {false, "EASIROC frontend disconnected"};
  if (!raw.vme.status_fresh)
    return {false, "VME frontend status stale"};
  if (!raw.easiroc.status_fresh)
    return {false, "EASIROC frontend status stale"};
  if (raw.disk_free_gb < 0.0)
    return {false, "Disk free unavailable"};
  if (raw.disk_free_gb < kDiskErrorThresholdGB)
    return {false, "Disk free below 10 GB"};
  return {true, {}};
}

ConfigurationEvaluation evaluate_bor_configuration(
    std::int32_t target_run_number,
    const ConfigurationRawStatus& vme,
    const ConfigurationRawStatus& easiroc) {
  if (!vme.ok || vme.run_number != target_run_number)
    return {false, "VME configuration not valid for run " +
                       std::to_string(target_run_number)};
  if (!easiroc.ok || easiroc.run_number != target_run_number)
    return {false, "EASIROC configuration not valid for run " +
                       std::to_string(target_run_number)};
  return {true, {}};
}

StartEvaluation evaluate_start(
    std::int32_t target_run_number,
    const CanStartEvaluation& pre_start,
    const ConfigurationRawStatus& vme,
    const ConfigurationRawStatus& easiroc) {
  if (!pre_start.allowed)
    return {false, pre_start.reason};

  const ConfigurationEvaluation configuration =
      evaluate_bor_configuration(target_run_number, vme, easiroc);
  return {configuration.ready, configuration.reason};
}

StatusEvaluation evaluate_status(const RawStatus& raw) {
  StatusEvaluation evaluation;
  evaluation.disk = evaluate_disk(raw.disk_free_gb);
  evaluation.logger = evaluate_logger(raw.logger_connected);
  evaluation.vme = evaluate_vme(raw.run_state, raw.vme);
  evaluation.easiroc = evaluate_easiroc(raw.run_state, raw.easiroc);

  // For equal severities, the operational priority is VME, EASIROC, Disk,
  // then Logger. The selected component supplies the short GUI summary.
  const std::array<const ComponentStatus*, 4> components = {
      &evaluation.vme, &evaluation.easiroc, &evaluation.disk,
      &evaluation.logger};
  const ComponentStatus* selected = components.front();
  for (const ComponentStatus* component : components) {
    if (static_cast<int>(component->severity) >
        static_cast<int>(selected->severity))
      selected = component;
  }
  evaluation.global_severity = selected->severity;
  evaluation.global_summary = selected->severity == Severity::kOk
                                  ? "DAQ OK"
                                  : selected->reason;
  evaluation.can_start = evaluate_can_start(raw);
  return evaluation;
}

}  // namespace daq_monitor
