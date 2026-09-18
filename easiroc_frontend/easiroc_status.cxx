#include "easiroc_status.h"

#include "rbcp.h"
#include "tcp_probe.h"

#include <algorithm>
#include <stdexcept>

namespace {

unsigned decodeBcd(const std::uint8_t* bytes, std::size_t length) {
  unsigned value = 0;
  for (std::size_t i = 0; i < length; ++i) {
    const unsigned high = bytes[i] >> 4;
    const unsigned low = bytes[i] & 0x0f;
    if (high > 9 || low > 9)
      throw std::runtime_error("firmware date is not BCD encoded");
    value = value * 100 + high * 10 + low;
  }
  return value;
}

}  // namespace

namespace easiroc {

std::string FirmwareVersion::versionString() const {
  return "v." + std::to_string(raw[0] >> 4) + "." +
         std::to_string(raw[0] & 0x0f) + "." +
         std::to_string(raw[1] >> 4) + "-p" +
         std::to_string(raw[1] & 0x0f);
}

std::string FirmwareVersion::synthesisDateString() const {
  return std::to_string(decodeBcd(raw.data() + 2, 2)) + "-" +
         std::to_string(decodeBcd(raw.data() + 4, 1)) + "-" +
         std::to_string(decodeBcd(raw.data() + 5, 1));
}

FirmwareVersion readFirmwareVersion(const std::string& host) {
  RbcpClient rbcp(host);
  const auto bytes = rbcp.read(kFirmwareVersionAddress, kFirmwareVersionLength);
  FirmwareVersion result;
  std::copy(bytes.begin(), bytes.end(), result.raw.begin());
  // Validate the BCD date while reporting this operation as successful.
  result.synthesisDateString();
  return result;
}

void probeDataConnection(const std::string& host) {
  probeTcpConnection(host, kTcpDataPort, kTcpConnectTimeoutMilliseconds);
}

}  // namespace easiroc
