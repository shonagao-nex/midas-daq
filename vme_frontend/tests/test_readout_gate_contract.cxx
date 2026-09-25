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

std::string read_file(const char* path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), {}};
}

std::string section(const std::string& source, const char* begin,
                    const char* end) {
  const auto first = source.find(begin);
  require(first != std::string::npos, "cannot find function start");
  const auto last = source.find(end, first + 1);
  require(last != std::string::npos, "cannot find function end");
  return source.substr(first, last - first);
}

void gate_before_access(const std::string& body, const char* access,
                        const char* message) {
  const auto gate = body.find("if (!global_busy::readout_allowed())\n        return 0;");
  require(gate != std::string::npos && gate < body.find(access), message);
}
}  // namespace

int main() {
  const auto busy = read_file("global_busy.cxx");
  const auto frontend = read_file("fevme.cxx");
  require(!busy.empty() && !frontend.empty(), "cannot read frontend sources");

  require(busy.find("bool physics_readout_allowed = false;") != std::string::npos,
          "readout must start disabled");
  require(busy.find("void disable_readout() { physics_readout_allowed = false; }") !=
              std::string::npos &&
              busy.find("bool readout_allowed() { return physics_readout_allowed; }") !=
                  std::string::npos,
          "readout gate accessors must use the same state");
  require(busy.find("physics_readout_allowed = true;") ==
              busy.rfind("physics_readout_allowed = true;"),
          "readout must have only one enable point");
  const auto start400 = section(busy, "INT before_start(", "INT after_start(");
  require(start400.find("disable_readout();") < start400.find("publish_ready("),
          "START 400 must disable readout before ODB or BUSY work");

  const auto start500 = section(frontend, "INT begin_of_run(INT run_number, char *error)\n{",
                                "/* Handle the end of a MIDAS run. */");
  require(start500.find("readout_allowed") == std::string::npos &&
              start500.find("physics_readout_allowed") == std::string::npos,
          "START 500 must leave readout disabled");

  const auto start600 = section(busy, "INT after_start(", "INT before_stop(");
  const auto ready = start600.find("if (!vme_ready || !easiroc_ready)");
  const auto release = start600.find("if (!set_global_busy(false))");
  const auto enable = start600.find("physics_readout_allowed = true;");
  require(ready != std::string::npos && release > ready && enable > release &&
              enable < start600.find("return SUCCESS;"),
          "START 600 must enable only after readiness and BUSY release succeed");

  const auto stop400 = section(busy, "INT before_stop(", "INT start_abort(");
  const auto busy_abort = section(busy, "INT start_abort(", "\n}\n}");
  const auto eor = section(frontend, "INT end_of_run(INT run_number, char *error)\n{",
                           "/* Roll back hardware");
  const auto frontend_abort = section(frontend, "static INT start_abort(INT run_number, char *error)\n{",
                                      "/* Handle a MIDAS run pause. */");
  const auto init = section(frontend, "INT frontend_init()\n{", "/* Close the MIDAS VME interface");
  require(stop400.find("disable_readout();") < stop400.find("set_global_busy(true)"),
          "STOP 400 must disable readout first");
  require(busy_abort.find("disable_readout();") < busy_abort.find("publish_ready("),
          "STARTABORT 400 must disable readout first");
  require(frontend_abort.find("disable_readout();") < frontend_abort.find("publish_ready("),
          "STARTABORT 500 must disable readout first");
  require(eor.find("disable_readout();") < eor.find("stop_v1720e_and_publish_state"),
          "EOR must disable readout before hardware work");
  require(init.find("disable_readout();") != std::string::npos,
          "frontend_init must disable readout");

  const auto poll = section(frontend, "INT poll_event(", "INT interrupt_configure(");
  const auto read = section(frontend, "INT read_vme_event(",
                            "INT read_vme_configuration_event(");
  gate_before_access(poll, "v792_DataReady(",
                     "poll_event can access VME before readout is allowed");
  gate_before_access(read, "wait_for_v1190_data_ready(",
                     "read_vme_event can access VME before readout is allowed");
  std::cout << "test_readout_gate_contract: passed\n";
}
