#ifndef EASIROC_FRONTEND_RBCP_H
#define EASIROC_FRONTEND_RBCP_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class RbcpClient {
 public:
  explicit RbcpClient(std::string host, std::uint16_t port = 4660);
  std::vector<std::uint8_t> read(std::uint32_t address, std::size_t length);
  void write(std::uint32_t address, const std::vector<std::uint8_t>& data);
  void write(std::uint32_t address, std::uint8_t value);

 private:
  std::vector<std::uint8_t> readPacket(std::uint32_t address,
                                       std::uint8_t length);
  void writePacket(std::uint32_t address, const std::uint8_t* data,
                   std::uint8_t length);
  std::string host_;
  std::uint16_t port_;
  std::uint8_t packet_id_ = 0;
};

#endif
