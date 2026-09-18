#include "rbcp.h"

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace {
constexpr std::uint8_t kVersionType = 0xff;
constexpr std::uint8_t kReadCommand = 0xc0;
constexpr std::uint8_t kReadReply = 0xc8;
constexpr std::uint8_t kWriteCommand = 0x80;
constexpr std::uint8_t kWriteReply = 0x88;
constexpr int kTimeoutMilliseconds = 1000;
constexpr int kMaximumAttempts = 3;
constexpr std::size_t kHeaderSize = 8;
constexpr std::size_t kMaximumPayload = 255;

class FileDescriptor {
 public:
  explicit FileDescriptor(int value) : value_(value) {}
  ~FileDescriptor() {
    if (value_ >= 0) close(value_);
  }
  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;
  int get() const { return value_; }

 private:
  int value_;
};

std::runtime_error systemError(const std::string& operation) {
  return std::runtime_error(operation + ": " + std::strerror(errno));
}

std::uint32_t decodeBigEndian32(const std::uint8_t* bytes) {
  return (static_cast<std::uint32_t>(bytes[0]) << 24) |
         (static_cast<std::uint32_t>(bytes[1]) << 16) |
         (static_cast<std::uint32_t>(bytes[2]) << 8) |
         static_cast<std::uint32_t>(bytes[3]);
}
}  // namespace

RbcpClient::RbcpClient(std::string host, std::uint16_t port)
    : host_(std::move(host)), port_(port) {}

std::vector<std::uint8_t> RbcpClient::read(std::uint32_t address,
                                           std::size_t length) {
  std::vector<std::uint8_t> result;
  result.reserve(length);
  while (length != 0) {
    const auto packet_length = static_cast<std::uint8_t>(
        length > kMaximumPayload ? kMaximumPayload : length);
    auto packet = readPacket(address, packet_length);
    result.insert(result.end(), packet.begin(), packet.end());
    address += packet_length;
    length -= packet_length;
  }
  return result;
}

void RbcpClient::write(std::uint32_t address,
                       const std::vector<std::uint8_t>& data) {
  std::size_t offset = 0;
  while (offset != data.size()) {
    const auto packet_length = static_cast<std::uint8_t>(
        data.size() - offset > kMaximumPayload ? kMaximumPayload
                                               : data.size() - offset);
    writePacket(address + offset, data.data() + offset, packet_length);
    offset += packet_length;
  }
}

void RbcpClient::write(std::uint32_t address, std::uint8_t value) {
  write(address, std::vector<std::uint8_t>{value});
}

std::vector<std::uint8_t> RbcpClient::readPacket(std::uint32_t address,
                                                 std::uint8_t length) {
  std::string last_error = "RBCP transaction failed";
  for (int attempt = 0; attempt < kMaximumAttempts; ++attempt) {
    const std::uint8_t request_id = packet_id_++;
    try {
      addrinfo hints{};
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_DGRAM;
      addrinfo* raw_addresses = nullptr;
      const std::string service = std::to_string(port_);
      const int gai_result =
          getaddrinfo(host_.c_str(), service.c_str(), &hints, &raw_addresses);
      if (gai_result != 0) {
        throw std::runtime_error(std::string("getaddrinfo: ") +
                                 gai_strerror(gai_result));
      }
      struct AddressList {
        addrinfo* value;
        ~AddressList() { freeaddrinfo(value); }
      } addresses{raw_addresses};

      FileDescriptor socket_fd(
          socket(addresses.value->ai_family, addresses.value->ai_socktype,
                 addresses.value->ai_protocol));
      if (socket_fd.get() < 0) throw systemError("socket");

      std::array<std::uint8_t, kHeaderSize> request{};
      request[0] = kVersionType;
      request[1] = kReadCommand;
      request[2] = request_id;
      request[3] = length;
      request[4] = static_cast<std::uint8_t>(address >> 24);
      request[5] = static_cast<std::uint8_t>(address >> 16);
      request[6] = static_cast<std::uint8_t>(address >> 8);
      request[7] = static_cast<std::uint8_t>(address);

      const ssize_t sent =
          sendto(socket_fd.get(), request.data(), request.size(), 0,
                 addresses.value->ai_addr, addresses.value->ai_addrlen);
      if (sent < 0) throw systemError("sendto");
      if (static_cast<std::size_t>(sent) != request.size())
        throw std::runtime_error("sendto: incomplete datagram");

      pollfd descriptor{socket_fd.get(), POLLIN, 0};
      int poll_result;
      do {
        poll_result = poll(&descriptor, 1, kTimeoutMilliseconds);
      } while (poll_result < 0 && errno == EINTR);
      if (poll_result < 0) throw systemError("poll");
      if (poll_result == 0) throw std::runtime_error("RBCP reply timeout");
      if ((descriptor.revents & POLLIN) == 0)
        throw std::runtime_error("RBCP socket reported an error");

      std::array<std::uint8_t, kHeaderSize + kMaximumPayload> reply{};
      const ssize_t received =
          recv(socket_fd.get(), reply.data(), reply.size(), 0);
      if (received < 0) throw systemError("recv");
      const std::size_t reply_size = static_cast<std::size_t>(received);
      if (reply_size != kHeaderSize + length)
        throw std::runtime_error("invalid RBCP reply length");
      if (reply[0] != kVersionType)
        throw std::runtime_error("invalid RBCP version/type");
      if (reply[1] != kReadReply) {
        if ((reply[1] & 0x01) != 0)
          throw std::runtime_error("RBCP bus error");
        throw std::runtime_error("invalid RBCP reply command/flags");
      }
      if (reply[2] != request_id)
        throw std::runtime_error("invalid RBCP packet ID");
      if (reply[3] != length)
        throw std::runtime_error("invalid RBCP reply data length");
      if (decodeBigEndian32(reply.data() + 4) != address)
        throw std::runtime_error("invalid RBCP reply address");

      return {reply.begin() + kHeaderSize, reply.begin() + reply_size};
    } catch (const std::runtime_error& error) {
      last_error = error.what();
    }
  }
  throw std::runtime_error(last_error + " (after 3 attempts)");
}

void RbcpClient::writePacket(std::uint32_t address, const std::uint8_t* data,
                             std::uint8_t length) {
  if (length == 0) return;

  std::string last_error = "RBCP transaction failed";
  for (int attempt = 0; attempt < kMaximumAttempts; ++attempt) {
    const std::uint8_t request_id = packet_id_++;
    try {
      addrinfo hints{};
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_DGRAM;
      addrinfo* raw_addresses = nullptr;
      const std::string service = std::to_string(port_);
      const int gai_result =
          getaddrinfo(host_.c_str(), service.c_str(), &hints, &raw_addresses);
      if (gai_result != 0) {
        throw std::runtime_error(std::string("getaddrinfo: ") +
                                 gai_strerror(gai_result));
      }
      struct AddressList {
        addrinfo* value;
        ~AddressList() { freeaddrinfo(value); }
      } addresses{raw_addresses};

      FileDescriptor socket_fd(
          socket(addresses.value->ai_family, addresses.value->ai_socktype,
                 addresses.value->ai_protocol));
      if (socket_fd.get() < 0) throw systemError("socket");

      std::array<std::uint8_t, kHeaderSize + kMaximumPayload> request{};
      request[0] = kVersionType;
      request[1] = kWriteCommand;
      request[2] = request_id;
      request[3] = length;
      request[4] = static_cast<std::uint8_t>(address >> 24);
      request[5] = static_cast<std::uint8_t>(address >> 16);
      request[6] = static_cast<std::uint8_t>(address >> 8);
      request[7] = static_cast<std::uint8_t>(address);
      std::copy(data, data + length, request.begin() + kHeaderSize);
      const std::size_t request_size = kHeaderSize + length;

      const ssize_t sent =
          sendto(socket_fd.get(), request.data(), request_size, 0,
                 addresses.value->ai_addr, addresses.value->ai_addrlen);
      if (sent < 0) throw systemError("sendto");
      if (static_cast<std::size_t>(sent) != request_size)
        throw std::runtime_error("sendto: incomplete datagram");

      pollfd descriptor{socket_fd.get(), POLLIN, 0};
      int poll_result;
      do {
        poll_result = poll(&descriptor, 1, kTimeoutMilliseconds);
      } while (poll_result < 0 && errno == EINTR);
      if (poll_result < 0) throw systemError("poll");
      if (poll_result == 0) throw std::runtime_error("RBCP reply timeout");
      if ((descriptor.revents & POLLIN) == 0)
        throw std::runtime_error("RBCP socket reported an error");

      std::array<std::uint8_t, kHeaderSize + kMaximumPayload> reply{};
      const ssize_t received = recv(socket_fd.get(), reply.data(), reply.size(), 0);
      if (received < 0) throw systemError("recv");
      const std::size_t reply_size = static_cast<std::size_t>(received);
      if (reply_size != kHeaderSize + length)
        throw std::runtime_error("invalid RBCP reply length");
      if (reply[0] != kVersionType)
        throw std::runtime_error("invalid RBCP version/type");
      if (reply[1] != kWriteReply) {
        if ((reply[1] & 0x01) != 0)
          throw std::runtime_error("RBCP bus error");
        throw std::runtime_error("invalid RBCP reply command/flags");
      }
      if (reply[2] != request_id)
        throw std::runtime_error("invalid RBCP packet ID");
      if (reply[3] != length)
        throw std::runtime_error("invalid RBCP reply data length");
      if (decodeBigEndian32(reply.data() + 4) != address)
        throw std::runtime_error("invalid RBCP reply address");
      return;
    } catch (const std::runtime_error& error) {
      last_error = error.what();
    }
  }
  throw std::runtime_error(last_error + " (after 3 attempts)");
}
