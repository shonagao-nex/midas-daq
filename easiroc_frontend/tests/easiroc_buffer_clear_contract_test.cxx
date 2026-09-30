#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
void expect(bool condition, const char* message) {
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
  const std::string source = readFile("feeasiroc.cxx");
  const auto begin =
      source.find("std::optional<DWORD> accept_manual_buffer_clear_request()");
  const auto end = source.find("void reset_software_readout_state()", begin);
  expect(begin != std::string::npos && end != std::string::npos,
         "cannot isolate EASIROC manual clear function");
  const std::string clear = source.substr(begin, end - begin);
  expect(clear.find("bufferClearRunStateRejection") != std::string::npos,
         "manual clear does not apply STOPPED-only policy");
  expect(clear.find("TcpConnection connection") != std::string::npos &&
             clear.find("connection.drain") != std::string::npos,
         "manual clear does not perform bounded TCP drain");
  expect(clear.find("rbcp->write") == std::string::npos &&
             clear.find("stopValue") == std::string::npos,
         "manual clear unexpectedly writes an EASIROC register");
  expect(clear.find("Software Reset") == std::string::npos &&
             clear.find("Board Reset") == std::string::npos &&
             clear.find("resetPA") == std::string::npos &&
             clear.find("sendSlowControl") == std::string::npos,
         "manual clear contains a hardware reset/configuration operation");
  expect(clear.find("pending_events.clear") != std::string::npos,
         "manual clear does not clear host pending events");
  std::cout << "easiroc_buffer_clear_contract_test: 5 checks passed\n";
}
