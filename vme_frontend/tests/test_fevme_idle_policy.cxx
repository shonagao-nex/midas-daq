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

}  // namespace

int main() {
  const std::string source = readFile("fevme.cxx");
  require(!source.empty(), "cannot read fevme.cxx");
  require(source.find("BOOL frontend_call_loop = TRUE") != std::string::npos,
          "frontend status housekeeping loop is not enabled");
  require(source.find("RO_RUNNING,           // Readout condition") !=
              std::string::npos,
          "polled equipment is not restricted to RUNNING");
  require(source.find("FRONTEND_IDLE_SLEEP_MS = 10") != std::string::npos,
          "STOPPED/PAUSED idle wait is not 10 ms");
  require(source.find("if (run_state != STATE_RUNNING)\n"
                      "    ss_sleep(FRONTEND_IDLE_SLEEP_MS);") !=
              std::string::npos,
          "frontend_loop does not yield CPU outside RUNNING");

  const std::size_t poll_begin = source.find("INT poll_event(");
  const std::size_t poll_end = source.find("INT interrupt_configure(", poll_begin);
  require(poll_begin != std::string::npos && poll_end != std::string::npos,
          "cannot locate poll_event body");
  const std::string poll = source.substr(poll_begin, poll_end - poll_begin);
  require(poll.find("v792_DataReady") != std::string::npos,
          "poll_event no longer checks V792 readiness");
  require(poll.find("ss_sleep") == std::string::npos &&
              poll.find("cm_yield") == std::string::npos,
          "RUNNING poll_event gained trigger-latency wait");

  std::cout << "test_fevme_idle_policy: 6 checks passed\n";
}
