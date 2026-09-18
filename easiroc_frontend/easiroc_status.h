#ifndef EASIROC_FRONTEND_EASIROC_STATUS_H
#define EASIROC_FRONTEND_EASIROC_STATUS_H

#include <array>
#include <cstdint>
#include <string>

namespace easiroc {

constexpr std::uint32_t kFirmwareVersionAddress = 0xf0000000;
constexpr std::size_t kFirmwareVersionLength = 6;
constexpr std::uint16_t kTcpDataPort = 24;
constexpr int kTcpConnectTimeoutMilliseconds = 1000;

struct FirmwareVersion {
  std::array<std::uint8_t, kFirmwareVersionLength> raw{};

  std::string versionString() const;
  std::string synthesisDateString() const;
};

// Both functions are read-only. readFirmwareVersion() issues one RBCP read;
// probeDataConnection() connects and immediately closes without sending or
// receiving TCP stream data.
FirmwareVersion readFirmwareVersion(const std::string& host);
void probeDataConnection(const std::string& host);

}  // namespace easiroc

#endif
