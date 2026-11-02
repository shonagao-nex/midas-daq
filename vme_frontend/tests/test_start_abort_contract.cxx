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

std::string functionBody(const std::string& source, const char* signature,
                         const char* next_signature) {
  const auto begin = source.find(signature);
  const auto end = source.find(next_signature, begin + 1);
  require(begin != std::string::npos && end != std::string::npos,
          "cannot isolate required function");
  return source.substr(begin, end - begin);
}

}  // namespace

int main() {
  const std::string source = readFile("fevme.cxx");
  require(!source.empty(), "cannot read fevme.cxx");
  require(source.find(
              "cm_register_transition(TR_STARTABORT, start_abort, 500)") !=
              std::string::npos,
          "TR_STARTABORT is not registered at sequence 500");

  const auto start_attempt = source.find("gV1720State.lifecycle.start_attempted = true;");
  const auto hardware_start = source.find("v1720e_start(gVme, V1720E_BASE)");
  require(start_attempt != std::string::npos &&
              hardware_start != std::string::npos &&
              start_attempt < hardware_start,
          "V1720E start attempt is not tracked before the RUN write");

  const std::string stop = functionBody(
      source, "static bool stop_v1720e_and_publish_state(",
      "/* Low-level VME access helpers. */");
  require(stop.find("if (!gV1720State.lifecycle.start_attempted && !gV1720State.lifecycle.started)") ==
              std::string::npos,
          "rollback still skips untracked hardware RUN");
  require(stop.find("v1720e_stop_if_running(") != std::string::npos,
          "rollback does not check hardware RUN before stopping");
  require(stop.find("gV1720State.lifecycle.start_attempted = false") != std::string::npos &&
              stop.find("gV1720State.lifecycle.started = false") != std::string::npos,
          "successful rollback does not become idempotent");

  const std::string abort = functionBody(
      source, "static INT start_abort(INT run_number, char *error)\n{",
      "INT pause_run(INT run_number, char *error)");
  require(abort.find("readout_enable(FALSE)") != std::string::npos,
          "STARTABORT does not disable frontend readout");
  require(abort.find("stop_v1720e_and_publish_state(") !=
              std::string::npos &&
              abort.find("\"STARTABORT rollback\", &stop_outcome") !=
              std::string::npos,
          "STARTABORT does not use the common V1720E stop path");
  require(abort.find("run_state = STATE_STOPPED") != std::string::npos,
          "STARTABORT does not restore legacy MFE state");
  require(abort.find("clear_module_buffers") == std::string::npos &&
              abort.find("configure_v1720e_for_run") == std::string::npos &&
              abort.find("v1720e_software_clear") == std::string::npos,
          "STARTABORT performs forbidden clear or configuration work");

  const std::string eor = functionBody(
      source, "INT end_of_run(INT run_number, char *error)",
      "static INT start_abort(INT run_number, char *error)\n{");
  require(eor.find("stop_v1720e_and_publish_state(\"EOR\")") !=
              std::string::npos,
          "normal EOR no longer uses the common stop path");

  const std::string init = functionBody(
      source, "INT frontend_init()\n{", "INT frontend_exit()\n{");
  require(init.find("current_run_state == STATE_STOPPED") != std::string::npos &&
              init.find("STOPPED frontend startup recovery") != std::string::npos,
          "startup recovery is not limited to MIDAS STOPPED");
  const std::string exit = functionBody(
      source, "INT frontend_exit()\n{", "INT begin_of_run(INT run_number, char *error)");
  require(exit.find("gV1720State.lifecycle.startup_run_state != STATE_STOPPED || midas_active") !=
              std::string::npos &&
              exit.find("V1720E hardware stop failed during frontend exit") !=
              std::string::npos,
          "frontend exit does not protect active restarts or report stop failures");

  std::cout << "test_start_abort_contract: 11 checks passed\n";
}
