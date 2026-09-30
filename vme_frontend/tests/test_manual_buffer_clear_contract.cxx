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
std::string functionBody(const std::string& source, const char* begin,
                         const char* end) {
  const auto first = source.find(begin);
  const auto last = source.find(end, first);
  require(first != std::string::npos && last != std::string::npos,
          "cannot isolate manual clear function");
  return source.substr(first, last - first);
}
}  // namespace

int main() {
  const std::string frontend = readFile("fevme.cxx");
  const std::string driver = readFile("v1720e.c");
  const std::string clear = functionBody(
      frontend, "static bool validate_manual_buffer_clear_request(",
      "static bool reset_module_event_counters()");
  const std::string flow = functionBody(
      frontend, "static void process_manual_buffer_clear_request()",
      "static bool reset_module_event_counters()");
  const auto validate = flow.find("validate_manual_buffer_clear_request(request)");
  const auto execute = flow.find("execute_manual_buffer_clear(request, result)");
  const auto finish = flow.find("finish_manual_buffer_clear_request(request, result)");
  require(validate != std::string::npos && execute != std::string::npos &&
              finish != std::string::npos && validate < execute &&
              execute < finish,
          "manual clear request flow changed order");
  const std::string v1720 = functionBody(
      driver, "int v1720e_software_clear(", "\n}");
  require(clear.find("STATE_STOPPED") != std::string::npos,
          "manual clear does not enforce STOPPED");
  require(clear.find("V792_BIT_SET2_RW") != std::string::npos &&
              clear.find("V792_BIT_CLEAR2_WO") != std::string::npos,
          "V792 Data Clear sequence missing");
  require(clear.find("V1190_SOFT_CLEAR") != std::string::npos,
          "V1190 Software Clear missing");
  require(clear.find("V775_BIT2_CLEAR_DATA") != std::string::npos,
          "V775 Data Clear missing");
  require(clear.find("v1720e_software_clear") != std::string::npos,
          "V1720E Software Clear missing");
  require(clear.find("RPV130_") == std::string::npos,
          "manual clear accesses RPV130");
  require(clear.find("configure_") == std::string::npos &&
              clear.find("Reset") == std::string::npos,
          "manual clear contains configuration or reset operation");
  require(v1720.find("REG_SOFTWARE_CLEAR") != std::string::npos &&
              v1720.find("REG_EVENT_STORED") != std::string::npos,
          "V1720E clear does not write SW_CLEAR and verify Event Stored");
  require(v1720.find("write32(vme, base, 0xEF24") == std::string::npos &&
              v1720.find("REG_SOFTWARE_RESET") == std::string::npos &&
              v1720.find("REG_CONFIG_RELOAD") == std::string::npos,
          "V1720E clear contains reset/reload command");
  std::cout << "test_manual_buffer_clear_contract: 10 checks passed\n";
}
