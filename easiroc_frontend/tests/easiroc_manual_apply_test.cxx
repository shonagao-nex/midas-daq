#include "easiroc_manual_apply.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <variant>
#include <map>
#include <type_traits>

namespace {
int tests_run = 0;

void check(bool condition, const std::string& message) {
  ++tests_run;
  if (!condition) throw std::runtime_error(message);
}

bool sameAsic(const easiroc::AsicDiscriminatorSettings& lhs,
              const easiroc::AsicDiscriminatorSettings& rhs) {
  return lhs.dac_code == rhs.dac_code && lhs.dac_slope == rhs.dac_slope &&
         lhs.hg_feedback_capacitance == rhs.hg_feedback_capacitance &&
         lhs.lg_feedback_capacitance == rhs.lg_feedback_capacitance &&
         lhs.hg_shaping_time == rhs.hg_shaping_time &&
         lhs.lg_shaping_time == rhs.lg_shaping_time &&
         lhs.input_dac == rhs.input_dac &&
         lhs.channel_enabled == rhs.channel_enabled;
}

bool sameApplied(const easiroc::AppliedAsicSlowControlSettings& lhs,
                 const easiroc::AppliedAsicSlowControlSettings& rhs) {
  return lhs.valid == rhs.valid && lhs.request_id == rhs.request_id &&
         lhs.apply_unix_time == rhs.apply_unix_time &&
         sameAsic(lhs.asic[0], rhs.asic[0]) &&
         sameAsic(lhs.asic[1], rhs.asic[1]);
}

std::array<std::uint16_t, easiroc::kInputDacChannelCount> inputValues(
    const easiroc::AsicDiscriminatorSettings& settings) {
  std::array<std::uint16_t, easiroc::kInputDacChannelCount> result{};
  for (std::size_t channel = 0; channel < result.size(); ++channel)
    result[channel] = static_cast<std::uint16_t>(settings.input_dac[channel]);
  return result;
}

easiroc::ManualApplyBackendResult execute(
    const easiroc::ManualApplyStatus& pending,
    const easiroc::ManualApplyRequestContext& context,
    const easiroc::AsicSlowControlSettings& settings,
    const easiroc::AppliedAsicSlowControlSettings& previous,
    bool previous_indeterminate, std::uint64_t unix_time, int* writes,
    int* delays) {
  return easiroc::executeManualApplyBackend(
      pending, context, settings, previous, previous_indeterminate, unix_time,
      {},
      [&](std::uint32_t, const std::vector<std::uint8_t>&) { ++*writes; },
      [&](unsigned) { ++*delays; });
}

}  // namespace

int main() {
  try {
    easiroc::ManualApplyStatus initial;
    check(initial.active_request_id == 0 &&
              initial.last_handled_request_id == 0 &&
              initial.last_successful_request_id == 0 &&
              initial.state == easiroc::ManualApplyState::kIdle &&
              !initial.apply_in_progress &&
              !initial.last_attempt_succeeded,
          "manual apply initial state is not safe/idle");

    const auto request1 = easiroc::beginManualApplyRequest(initial, 1);
    check(request1.handled && request1.pending.active_request_id == 1 &&
              request1.pending.apply_in_progress &&
              request1.pending.state == easiroc::ManualApplyState::kPending,
          "request 1 did not enter Pending");

    auto settings = easiroc::AsicSlowControlSettings{};
    settings.asic[0].dac_code = 601;
    settings.asic[0].input_dac[0] = 351;
    settings.asic[0].input_dac[17] = 349;
    settings.asic[0].hg_feedback_capacitance = 200;
    settings.asic[0].hg_shaping_time = 75;
    settings.asic[0].lg_shaping_time = 125;
    settings.asic[0].channel_enabled[0] = false;
    settings.asic[0].channel_enabled[16] = false;
    settings.asic[1].dac_code = 602;
    settings.asic[1].input_dac[16] = 352;
    settings.asic[1].input_dac[31] = 348;
    settings.asic[1].lg_feedback_capacitance = 1500;
    settings.asic[1].hg_shaping_time = 25;
    settings.asic[1].lg_shaping_time = 175;
    settings.asic[1].channel_enabled[31] = false;
    const auto frozen_settings = settings;
    const auto expected_asic1 =
        easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
            frozen_settings.asic[0].dac_code,
            frozen_settings.asic[0].dac_slope,
            inputValues(frozen_settings.asic[0]),
            frozen_settings.asic[0].hg_feedback_capacitance,
            frozen_settings.asic[0].lg_feedback_capacitance,
            frozen_settings.asic[0].hg_shaping_time,
            frozen_settings.asic[0].lg_shaping_time,
            frozen_settings.asic[0].channel_enabled);
    const auto expected_asic2 =
        easiroc::SlowControlPolicy::encodeLegacySiteAsicOverlay(
            frozen_settings.asic[1].dac_code,
            frozen_settings.asic[1].dac_slope,
            inputValues(frozen_settings.asic[1]),
            frozen_settings.asic[1].hg_feedback_capacitance,
            frozen_settings.asic[1].lg_feedback_capacitance,
            frozen_settings.asic[1].hg_shaping_time,
            frozen_settings.asic[1].lg_shaping_time,
            frozen_settings.asic[1].channel_enabled);

    std::vector<std::uint32_t> addresses;
    std::vector<std::vector<std::uint8_t>> payloads;
    std::vector<easiroc::ManualApplyState> published_states;
    const easiroc::ManualApplyRequestContext stopped_enabled{
        easiroc::ManualApplyRunState::kStopped, true, false, true};
    const auto succeeded = easiroc::executeManualApplyBackend(
        request1.pending, stopped_enabled, settings, {}, false, 1001,
        [&](const easiroc::ManualApplyStatus& status) {
          published_states.push_back(status.state);
          settings.asic[0].input_dac[0] = 400;
          settings.asic[0].hg_shaping_time = 150;
          settings.asic[0].channel_enabled[0] = true;
          settings.asic[1].dac_code = 700;
          settings.asic[1].lg_shaping_time = 25;
          settings.asic[1].channel_enabled[31] = true;
        },
        [&](std::uint32_t address, const std::vector<std::uint8_t>& data) {
          addresses.push_back(address);
          payloads.push_back(data);
        },
        [&](unsigned milliseconds) {
          check(milliseconds == 100, "manual apply delay is not 100 ms");
        });
    check(published_states ==
              std::vector<easiroc::ManualApplyState>{
                  easiroc::ManualApplyState::kApplying},
          "Applying was not published exactly once before execution");
    check(succeeded.apply_result.sequence_succeeded &&
              succeeded.apply_result.completed_transactions == 7 &&
              succeeded.terminal.state ==
                  easiroc::ManualApplyState::kSucceeded &&
              succeeded.terminal.last_handled_request_id == 1 &&
              succeeded.terminal.last_successful_request_id == 1 &&
              succeeded.terminal.last_attempt_succeeded &&
              succeeded.terminal.last_apply_error.empty() &&
              !succeeded.terminal.apply_in_progress &&
              succeeded.terminal.active_request_id == 0 &&
              succeeded.last_applied_changed &&
              !succeeded.hardware_state_indeterminate,
          "successful request terminal state differs");
    check(succeeded.last_applied.valid &&
              succeeded.last_applied.request_id == 1 &&
              succeeded.last_applied.apply_unix_time == 1001 &&
              sameAsic(succeeded.last_applied.asic[0],
                       frozen_settings.asic[0]) &&
              sameAsic(succeeded.last_applied.asic[1],
                       frozen_settings.asic[1]),
          "LastApplied was not populated from the request snapshot");
    check(easiroc::compareAsicSlowControlSettings(
              frozen_settings, succeeded.last_applied,
              succeeded.hardware_state_indeterminate)
              .status == easiroc::HardwareConfigurationStatus::kMatch,
          "successful apply does not produce ConfigurationStatus Match");
    check(addresses.size() == 6 && payloads.size() == 6 &&
              addresses[1] == 0x00000003 && payloads[1].size() == 57 &&
              addresses[2] == 0x0000003d && payloads[2].size() == 57,
          "manual backend did not execute the ASIC-only plan");
    check(std::equal(expected_asic1.begin(), expected_asic1.end(),
                     payloads[1].begin()) &&
              std::equal(expected_asic2.begin(), expected_asic2.end(),
                         payloads[2].begin()),
          "threshold/InputDAC/feedback/shaping/mask values did not propagate "
          "to both ASIC images");

    const auto duplicate =
        easiroc::beginManualApplyRequest(succeeded.terminal, 1);
    check(!duplicate.handled,
          "the same request ID was accepted more than once");

    const easiroc::AppliedAsicSlowControlSettings previous =
        succeeded.last_applied;
    const auto request2 =
        easiroc::beginManualApplyRequest(succeeded.terminal, 2);
    int failed_writes = 0;
    int failed_delays = 0;
    const auto failed = easiroc::executeManualApplyBackend(
        request2.pending, stopped_enabled, frozen_settings, previous, false,
        1002, {},
        [&](std::uint32_t, const std::vector<std::uint8_t>&) {
          if (++failed_writes >= 3)
            throw std::runtime_error("mock RBCP failure");
        },
        [&](unsigned) { ++failed_delays; });
    check(failed_writes == 4 && failed_delays == 0 &&
              failed.apply_result.completed_transactions == 2,
          "executor did not stop at the failed write");
    check(failed.terminal.state == easiroc::ManualApplyState::kFailed &&
              failed.terminal.last_handled_request_id == 2 &&
              failed.terminal.last_successful_request_id == 1 &&
              !failed.terminal.last_attempt_succeeded &&
              failed.terminal.last_apply_error.find("mock RBCP failure") !=
                  std::string::npos &&
              failed.terminal.last_apply_error.find(
                  "hardware state may be indeterminate") !=
                  std::string::npos &&
              failed.hardware_state_indeterminate &&
              !failed.last_applied_changed &&
              sameApplied(failed.last_applied, previous),
          "partial failure did not preserve LastApplied or mark Indeterminate");
    check(easiroc::compareAsicSlowControlSettings(
              frozen_settings, failed.last_applied,
              failed.hardware_state_indeterminate)
              .status ==
              easiroc::HardwareConfigurationStatus::kIndeterminate,
          "partial failure does not produce ConfigurationStatus Indeterminate");

    struct RejectionCase {
      const char* name;
      easiroc::ManualApplyRequestContext context;
      easiroc::AsicSlowControlSettings settings;
      const char* expected_error;
    };
    auto invalid_dac = frozen_settings;
    invalid_dac.asic[0].dac_code = 1024;
    auto invalid_slope = frozen_settings;
    invalid_slope.asic[0].dac_slope = 2;
    auto invalid_input = frozen_settings;
    invalid_input.asic[1].input_dac[31] = 512;
    auto invalid_shaping = frozen_settings;
    invalid_shaping.asic[0].hg_shaping_time = 60;
    const std::array rejection_cases{
        RejectionCase{"RUNNING",
                      {easiroc::ManualApplyRunState::kRunning, true, false},
                      frozen_settings, "RUNNING"},
        RejectionCase{"PAUSED",
                      {easiroc::ManualApplyRunState::kPaused, true, false},
                      frozen_settings, "PAUSED"},
        RejectionCase{"disabled",
                      {easiroc::ManualApplyRunState::kStopped, false, false},
                      frozen_settings, "Enabled=TRUE"},
        RejectionCase{"invalid DAC", stopped_enabled, invalid_dac,
                      "range 0..1023"},
        RejectionCase{"invalid slope", stopped_enabled, invalid_slope,
                      "coarse"},
        RejectionCase{"invalid InputDAC", stopped_enabled, invalid_input,
                      "InputDAC[31]"},
        RejectionCase{"invalid shaping", stopped_enabled, invalid_shaping,
                      "HGShapingTime=60"},
        RejectionCase{"busy",
                      {easiroc::ManualApplyRunState::kStopped, true, true},
                      frozen_settings, "already in progress"},
    };
    std::uint32_t rejection_id = 10;
    for (const auto& rejection : rejection_cases) {
      const auto begun = easiroc::beginManualApplyRequest(
          succeeded.terminal, rejection_id++);
      int writes = 0;
      int delays = 0;
      const auto rejected = execute(
          begun.pending, rejection.context, rejection.settings, previous,
          false, 2000 + rejection_id, &writes, &delays);
      check(rejected.terminal.state ==
                    easiroc::ManualApplyState::kRejected &&
                rejected.terminal.last_handled_request_id == rejection_id - 1 &&
                rejected.terminal.last_successful_request_id == 1 &&
                rejected.terminal.last_apply_error.find(
                    rejection.expected_error) != std::string::npos &&
                writes == 0 && delays == 0 &&
                !rejected.hardware_state_indeterminate &&
                !rejected.last_applied_changed &&
                sameApplied(rejected.last_applied, previous),
            std::string(rejection.name) +
                " rejection touched hardware or persistent state");
    }

    int stale_hardware_calls = 0;
    const auto stale = easiroc::acknowledgeStaleManualApplyRequest(
        easiroc::ManualApplyStatus{}, 50, 3001);
    check(stale.state == easiroc::ManualApplyState::kIndeterminate &&
              stale.last_handled_request_id == 50 &&
              stale.last_successful_request_id == 0 &&
              stale.active_request_id == 0 && !stale.apply_in_progress &&
              stale_hardware_calls == 0,
          "startup stale request was not acknowledged without hardware");

    // Full slow-control path: pure mocks, no socket/ODB dependencies.
    auto extended = frozen_settings;
    extended.extras = easiroc::SlowControlExtras{};
    extended.extras->probe_enabled = {true, true};
    extended.extras->probe_channel = {31, 0};
    extended.extras->and_channels[63] = true;
    const auto pending = easiroc::beginManualApplyRequest({}, 1).pending;
    std::vector<easiroc::Transaction> operations;
    auto record_write = [&](std::uint32_t address, const std::vector<std::uint8_t>& data) {
      operations.push_back(easiroc::Transaction::write(address, data));
    };
    auto record_delay = [&](unsigned ms) { operations.push_back(easiroc::Transaction::delay(ms)); };
    const auto full = easiroc::executeManualApplyBackend(pending, stopped_enabled,
        extended, {}, false, 4000, {}, record_write, record_delay);
    check(full.terminal.last_attempt_succeeded && full.last_applied.extras == extended.extras,
          "full apply did not record all settings");
    check(operations.size() == 16, "full plan must contain exactly 16 operations");
    const std::array<std::uint32_t, 16> full_addresses{
        0,3,0x3d,0,0,0,0, 0,3,0x3d,0,0,0,0, 0x78,0x88};
    for (unsigned i = 0; i < operations.size(); ++i) {
      if (i == 4 || i == 11)
        check(operations[i].type == easiroc::TransactionType::kDelay && operations[i].delay_ms == 100,
              "serial delay order changed");
      else check(operations[i].type == easiroc::TransactionType::kWrite && operations[i].address == full_addresses[i],
                 "unexpected register or wrong apply order");
    }
    check(operations[1].data.size() == 57 && operations[8].data.size() == 20 &&
          operations[14].data.size() == 11 && operations[14].data[2] == 1 &&
          operations[14].data[3] == 0x80 && operations[15].data == std::vector<std::uint8_t>{10},
          "encoded ASIC/probe/logic/width defaults differ");
    for (unsigned chip = 0; chip < 2; ++chip) {
      for (int signal = 0; signal < 5; ++signal) {
        auto changed = extended;
        changed.extras->probe_signal[chip] = signal;
        operations.clear();
        const auto encoded = easiroc::executeManualApplyBackend(pending, stopped_enabled,
            changed, {}, false, 4000, {}, record_write, record_delay);
        const auto expected = easiroc::SlowControlPolicy::encodeProbe(
            {static_cast<easiroc::ProbeOutput>(signal), changed.extras->probe_channel[chip] + int(32*chip)}, chip);
        check(encoded.last_applied_changed && operations[8+chip].data == std::vector<std::uint8_t>(expected.begin(), expected.end()),
              "probe signal/channel translation differs");
      }
    }
    for (int pattern = 0; pattern < 10; ++pattern) {
      auto changed = extended;
      changed.extras->logic_pattern = pattern;
      changed.extras->one_channel = 63;
      operations.clear();
      easiroc::executeManualApplyBackend(pending, stopped_enabled, changed, {}, false, 4000, {}, record_write, record_delay);
      check(operations[14].data[0] == pattern && operations[14].data[1] == (pattern == 0 ? 63 : 0),
            "logic pattern/one channel differs");
    }
    for (int width = 0; width <= 278; width = width == 0 ? 46 : width+8) {
      auto changed = extended; changed.extras->trigger_width_ns = width;
      operations.clear();
      easiroc::executeManualApplyBackend(pending, stopped_enabled, changed, {}, false, 4000, {}, record_write, record_delay);
      check(operations[15].data[0] == (width == 0 ? 0 : (width-38)/8), "width encoding differs");
    }
    std::vector<easiroc::AsicSlowControlSettings> invalids;
    for (int field = 0; field < 9; ++field) {
      auto changed = extended;
      switch (field) {
        case 0: changed.extras->probe_channel[1] = 32; break;
        case 1: changed.extras->probe_signal[0] = 5; break;
        case 2: changed.extras->logic_pattern = 10; break;
        case 3: changed.extras->one_channel = 64; break;
        case 4: changed.extras->hit_num_threshold = 65; break;
        case 5: changed.extras->trigger_width_ns = 47; break;
        case 6: changed.extras->trigger_width_ns = 286; break;
        case 7: changed.asic[1].input_dac[31] = 255; break;
        case 8: changed.apply_at_bor = true; break;
      }
      invalids.push_back(changed);
    }
    for (const auto& invalid : invalids) {
      operations.clear();
      const auto rejected = easiroc::executeManualApplyBackend(pending, stopped_enabled,
          invalid, full.last_applied, false, 4001, {}, record_write, record_delay);
      check(operations.empty() && !rejected.last_applied_changed && !rejected.apply_result.attempted,
            "invalid complete settings caused hardware access");
    }
    for (int gate = 0; gate < 5; ++gate) {
      auto context = stopped_enabled;
      if (gate == 0) context.run_state = easiroc::ManualApplyRunState::kRunning;
      if (gate == 1) context.run_state = easiroc::ManualApplyRunState::kPaused;
      if (gate == 2) context.run_state = easiroc::ManualApplyRunState::kUnknown;
      if (gate == 3) context.transition_clear = false;
      if (gate == 4) context.another_apply_in_progress = true;
      operations.clear();
      const auto rejected = easiroc::executeManualApplyBackend(pending, context,
          extended, full.last_applied, false, 4001, {}, record_write, record_delay);
      check(operations.empty() && rejected.terminal.state == easiroc::ManualApplyState::kRejected,
            "unsafe state did not reject full apply");
    }
    for (int fail_at = 1; fail_at <= 14; ++fail_at) {
      for (bool rollback_fails : {false, true}) {
        int writes = 0;
        auto changed = extended; changed.extras->trigger_width_ns = 126;
        const auto failed_full = easiroc::executeManualApplyBackend(
            easiroc::beginManualApplyRequest(full.terminal, 2).pending, stopped_enabled,
            changed, full.last_applied, false, 4002, {},
            [&](std::uint32_t, const std::vector<std::uint8_t>&) {
              ++writes;
              if (writes == fail_at || (rollback_fails && writes > fail_at)) throw std::runtime_error("injected failure");
            }, [](unsigned) {});
        check(failed_full.terminal.rollback_attempted &&
              failed_full.terminal.rollback_succeeded == !rollback_fails &&
              failed_full.hardware_state_indeterminate == rollback_fails &&
              failed_full.terminal.last_handled_request_id == 2 &&
              failed_full.terminal.last_successful_request_id == 1 &&
              !failed_full.last_applied_changed && failed_full.last_applied.extras == full.last_applied.extras &&
              writes == fail_at + (rollback_fails ? 1 : 14),
              "rollback must retransmit exactly once and preserve LastApplied");
        check(easiroc::slowControlPermitsDaqStart(failed_full.hardware_state_indeterminate,
                  failed_full.terminal.apply_in_progress) == !rollback_fails,
              "rollback failure did not inhibit DAQ START");
      }
    }
    auto migrated = easiroc::proposeLegacySlowControlMigration("moduleA", "192.168.10.40", frozen_settings, previous);
    check(migrated.settings.extras.has_value() && !migrated.last_applied.extras.has_value() &&
          migrated.last_applied.request_id == previous.request_id, "migration invented applied extras");
    auto other = migrated; other.id = "moduleB"; other.ip_address = "192.168.10.41";
    other.settings.asic[0].dac_code = 700;
    check(!easiroc::validateSlowControlDevices({migrated, other}) && migrated.settings.asic[0].dac_code != 700,
          "device settings are not independent");
    other.ip_address = migrated.ip_address;
    check(easiroc::validateSlowControlDevices({migrated, other}).has_value(), "duplicate IP accepted");
    other.ip_address = "192.168.010.41";
    check(easiroc::validateSlowControlDevices({other}).has_value(), "IP alias accepted");
    other.ip_address = "192.168.10.41"; other.id = "../unsafe";
    check(easiroc::validateSlowControlDevices({other}).has_value(), "unsafe ID accepted");

    migrated.settings.extras = extended.extras;
    other.id = "moduleB";
    std::vector<easiroc::SlowControlDevice> isolated{migrated, other};
    isolated[0].status = easiroc::acknowledgeStaleManualApplyRequest({}, 8, 5000);
    isolated[1].status = easiroc::acknowledgeStaleManualApplyRequest({}, 3, 5000);
    check(!easiroc::beginManualApplyRequest(isolated[0].status, 8).handled &&
          isolated[0].status.last_handled_request_id == 8 && isolated[1].status.last_handled_request_id == 3,
          "per-device startup acknowledgements replay or leak across devices");
    const auto device_request = easiroc::beginManualApplyRequest(isolated[1].status, 4);
    operations.clear();
    auto applied_device = easiroc::executeManualApplyBackend(device_request.pending, stopped_enabled,
        isolated[1].settings, isolated[1].last_applied, false, 5001, {}, record_write, record_delay);
    isolated[1].status = applied_device.terminal;
    isolated[1].last_applied = applied_device.last_applied;
    check(isolated[1].last_applied.request_id == 4 &&
          isolated[0].last_applied.request_id == migrated.last_applied.request_id &&
          isolated[0].status.last_handled_request_id == 8,
          "device apply changed another device result or LastApplied");

    for (const auto& unavailable : {easiroc::AppliedAsicSlowControlSettings{}, previous}) {
      int attempts = 0;
      const auto not_restored = easiroc::executeManualApplyBackend(pending, stopped_enabled,
          extended, unavailable, false, 5002, {},
          [&](std::uint32_t, const std::vector<std::uint8_t>&) { ++attempts; throw std::runtime_error("write failure"); },
          [](unsigned) {});
      check(attempts == 1 && !not_restored.terminal.rollback_attempted &&
            not_restored.hardware_state_indeterminate && !not_restored.last_applied_changed &&
            !not_restored.terminal.rollback_error.empty(), "missing/incompatible LastApplied used for rollback");
    }

    check(easiroc::slowControlPermitsDaqStart(false, false) &&
          !easiroc::slowControlPermitsDaqStart(true, false) &&
          !easiroc::slowControlPermitsDaqStart(false, true),
          "START gate ignores indeterminate/in-progress state");

    // Exercise the same typed schema traversal used by the device ODB codec.
    using Value = std::variant<int, bool, std::array<int,32>, std::array<bool,32>, std::array<bool,64>>;
    std::map<std::string, Value> mock_store;
    auto payload = extended;
    check(easiroc::visitSlowControlSettings(payload, [&](const std::string& path, auto& value) {
      mock_store.insert_or_assign("moduleA/Settings/" + path, value); return true;
    }) && mock_store.size() == 28, "device codec omitted a field");
    auto decoded = easiroc::AsicSlowControlSettings{};
    decoded.extras = easiroc::SlowControlExtras{};
    auto read_mock = [&](const std::string& path, auto& value) {
      using T = std::decay_t<decltype(value)>;
      const auto found = mock_store.find("moduleA/Settings/" + path);
      if (found == mock_store.end()) return false;
      const auto* typed = std::get_if<T>(&found->second);
      if (!typed) return false;
      value = *typed;
      return true;
    };
    check(easiroc::visitSlowControlSettings(decoded, read_mock) &&
          decoded.extras == extended.extras && sameAsic(decoded.asic[0], extended.asic[0]) &&
          sameAsic(decoded.asic[1], extended.asic[1]), "device Settings roundtrip changed fields");
    mock_store["moduleA/Settings/SelectableLogic/AndChannels"] = std::array<bool,32>{};
    check(!easiroc::visitSlowControlSettings(decoded, read_mock), "wrong BOOL array length accepted");
    mock_store.erase("moduleA/Settings/Probe/ASIC2/Channel");
    check(!easiroc::visitSlowControlSettings(decoded, read_mock), "missing probe field accepted");

    std::cout << "easiroc_manual_apply_test: " << tests_run
              << " checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << "easiroc_manual_apply_test: FAILED: " << error.what()
              << '\n';
    return 1;
  }
  return 0;
}
