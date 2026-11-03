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

std::string readFile(const char* path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), {}};
}

std::string between(const std::string& source, const char* begin_text,
                    const char* end_text) {
  const auto begin = source.find(begin_text);
  const auto end = source.find(end_text, begin + 1);
  require(begin != std::string::npos && end != std::string::npos,
          "cannot isolate required source section");
  return source.substr(begin, end - begin);
}

}  // namespace

int main() {
  const std::string source = readFile("feeasiroc.cxx");
  require(!source.empty(), "cannot read feeasiroc.cxx");
  require(source.find(
              "cm_register_transition(TR_STARTABORT, start_abort, 500)") !=
              std::string::npos,
          "TR_STARTABORT is not registered at sequence 500");

  const std::string cleanup = between(
      source, "CleanupResult stop_acquisition(",
      "void handle_acquisition_error(");
  require(cleanup.find("g_state.daq_start_attempted && g_state.rbcp") !=
              std::string::npos &&
              cleanup.find("g_state.rbcp->write(stop.address, stop.value)") !=
                  std::string::npos,
          "cleanup does not issue DAQ OFF after a start attempt");
  require(cleanup.find("g_state.daq_start_attempted = false") !=
              std::string::npos &&
              cleanup.find("if (!g_state.daq_start_attempted) g_state.rbcp.reset()") !=
                  std::string::npos,
          "DAQ OFF success is not made idempotent or failure-retryable");
  require(cleanup.find("g_state.tcp.reset()") != std::string::npos &&
              cleanup.find("g_state.parser.reset()") != std::string::npos &&
              cleanup.find("g_state.pending_events.clear()") !=
                  std::string::npos,
          "cleanup does not reset run-local readout state");

  const auto attempt = source.find("g_state.daq_start_attempted = true;");
  const auto daq_on = source.find("g_state.rbcp->write(start.address, start.value)");
  require(attempt != std::string::npos && daq_on != std::string::npos &&
              attempt < daq_on,
          "DAQ ON attempt is not tracked before the hardware write");

  const std::string abort = between(
      source, "static INT start_abort(INT run_number, char* error) {",
      "INT pause_run(");
  require(abort.find("readout_enable(FALSE)") != std::string::npos &&
              abort.find("stop_acquisition(\"start_abort\", false)") !=
                  std::string::npos,
          "STARTABORT does not quiesce readout through common cleanup");
  require(abort.find("run_state = STATE_STOPPED") != std::string::npos,
          "STARTABORT does not restore legacy MFE state");
  require(abort.find("sendSlowControl") == std::string::npos &&
              abort.find("resetPA") == std::string::npos &&
              abort.find("applyAsic") == std::string::npos,
          "STARTABORT modifies slow-control configuration");

  const std::string eor = between(source, "INT end_of_run(",
                                  "static INT start_abort(");
  require(eor.find("stop_acquisition(\"end_of_run\", true)") !=
              std::string::npos,
          "normal EOR no longer uses common acquisition cleanup");

  const std::string bor = between(source, "INT begin_of_run(", "INT end_of_run(");
  require(bor.find("executeManualApplyBackend") == std::string::npos &&
              bor.find("applyAsicSlowControlAtBor") == std::string::npos &&
              bor.find("buildSlowControlApplyPlan") == std::string::npos &&
              bor.find("SlowControlEncoder") == std::string::npos,
          "BOR automatically applies slow-control settings");
  const auto inhibit = bor.find("slowControlPermitsDaqStart");
  const auto first_hardware = bor.find("stop_acquisition(\"begin_of_run\"");
  require(inhibit != std::string::npos && first_hardware != std::string::npos &&
              inhibit < first_hardware && bor.find("device_start_inhibit_reason") < first_hardware,
          "START inhibit is missing or occurs after BOR hardware access");
  const auto manual = between(source, "void process_manual_apply_request() {",
                             "std::optional<DWORD> accept_manual_buffer_clear_request()");
  require(manual.find("ApplyOdbLock operation_lock") < manual.find("prepare_manual_apply(") &&
              manual.find("record_manual_apply_result(") < manual.find("publish_manual_apply_result("),
          "legacy Apply does not serialize state checks through result publication");

  std::cout << "easiroc_start_abort_contract_test: 12 checks passed\n";
}
