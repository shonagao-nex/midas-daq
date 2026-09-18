#include "easiroc_status.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

namespace {
constexpr char kDefaultIpAddress[] = "192.168.10.26";
}  // namespace

int main(int argc, char** argv) {
  if (argc > 2) {
    std::cerr << "Usage: " << argv[0] << " [IP address]\n";
    return 2;
  }
  const std::string host = argc == 2 ? argv[1] : kDefaultIpAddress;
  bool rbcp_ok = false;
  bool tcp_ok = false;

  try {
    const auto firmware = easiroc::readFirmwareVersion(host);
    std::cout << "RBCP firmware read: OK\n"
              << "  NIM-EASIROC at " << host << '\n'
              << "  Firmware version: " << firmware.versionString() << '\n'
              << "  Synthesized on: " << firmware.synthesisDateString() << '\n'
              << "  Raw bytes:" << std::hex << std::setfill('0');
    for (const auto byte : firmware.raw)
      std::cout << ' ' << std::setw(2) << static_cast<unsigned>(byte);
    std::cout << std::dec << '\n';
    rbcp_ok = true;
  } catch (const std::exception& error) {
    std::cerr << "RBCP firmware read: FAILED: " << error.what() << '\n';
  }

  try {
    easiroc::probeDataConnection(host);
    std::cout << "TCP data connection: OK (port " << easiroc::kTcpDataPort
              << ")\n";
    tcp_ok = true;
  } catch (const std::exception& error) {
    std::cerr << "TCP data connection: FAILED (port " << easiroc::kTcpDataPort
              << "): " << error.what() << '\n';
  }

  return rbcp_ok && tcp_ok ? 0 : 1;
}
