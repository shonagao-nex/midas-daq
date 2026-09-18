#include "tcp_probe.h"

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace {
std::runtime_error systemError(const std::string& operation, int error) {
  return std::runtime_error(operation + ": " + std::strerror(error));
}
}  // namespace

TcpConnection::TcpConnection(const std::string& host, std::uint16_t port,
                             int timeout_milliseconds) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* raw_addresses = nullptr;
  const std::string service = std::to_string(port);
  const int gai_result =
      getaddrinfo(host.c_str(), service.c_str(), &hints, &raw_addresses);
  if (gai_result != 0) {
    throw std::runtime_error(std::string("getaddrinfo: ") +
                             gai_strerror(gai_result));
  }
  struct AddressList {
    addrinfo* value;
    ~AddressList() { freeaddrinfo(value); }
  } addresses{raw_addresses};

  fd_ = socket(addresses.value->ai_family, addresses.value->ai_socktype,
               addresses.value->ai_protocol);
  if (fd_ < 0) throw systemError("socket", errno);
  try {
    const int current_flags = fcntl(fd_, F_GETFL, 0);
    if (current_flags < 0) throw systemError("fcntl(F_GETFL)", errno);
    if (fcntl(fd_, F_SETFL, current_flags | O_NONBLOCK) < 0)
      throw systemError("fcntl(F_SETFL)", errno);

    if (connect(fd_, addresses.value->ai_addr,
                addresses.value->ai_addrlen) == 0) {
      return;
    }
    if (errno != EINPROGRESS) throw systemError("connect", errno);

    pollfd descriptor{fd_, POLLOUT, 0};
    int poll_result;
    do {
      poll_result = poll(&descriptor, 1, timeout_milliseconds);
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result < 0) throw systemError("poll", errno);
    if (poll_result == 0)
      throw std::runtime_error("TCP connection timeout after " +
                               std::to_string(timeout_milliseconds) + " ms");

    int socket_error = 0;
    socklen_t error_length = sizeof(socket_error);
    if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &socket_error,
                   &error_length) < 0)
      throw systemError("getsockopt(SO_ERROR)", errno);
    if (socket_error != 0) throw systemError("connect", socket_error);
  } catch (...) {
    close(fd_);
    fd_ = -1;
    throw;
  }
}

TcpConnection::~TcpConnection() {
  if (fd_ >= 0) close(fd_);
}

bool TcpConnection::dataAvailable(int timeout_milliseconds) {
  pollfd descriptor{fd_, POLLIN, 0};
  int poll_result;
  do {
    poll_result = poll(&descriptor, 1, timeout_milliseconds);
  } while (poll_result < 0 && errno == EINTR);
  if (poll_result < 0) throw systemError("poll", errno);
  if (poll_result == 0) return false;
  if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0)
    throw std::runtime_error("TCP socket reported an error");
  return (descriptor.revents & (POLLIN | POLLHUP)) != 0;
}

std::vector<std::uint8_t> TcpConnection::receive(
    std::size_t max_bytes, int timeout_milliseconds) {
  if (max_bytes == 0) return {};
  pollfd descriptor{fd_, POLLIN, 0};
  int poll_result;
  do {
    poll_result = poll(&descriptor, 1, timeout_milliseconds);
  } while (poll_result < 0 && errno == EINTR);
  if (poll_result < 0) throw systemError("poll", errno);
  if (poll_result == 0)
    throw std::runtime_error("TCP receive timeout after " +
                             std::to_string(timeout_milliseconds) + " ms");
  if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0)
    throw std::runtime_error("TCP socket reported an error");

  std::vector<std::uint8_t> data(max_bytes);
  ssize_t received;
  do {
    received = recv(fd_, data.data(), data.size(), 0);
  } while (received < 0 && errno == EINTR);
  if (received < 0) throw systemError("recv", errno);
  if (received == 0) throw std::runtime_error("TCP connection closed by peer");
  data.resize(static_cast<std::size_t>(received));
  return data;
}

std::size_t TcpConnection::drain(int quiet_milliseconds,
                                 int max_total_milliseconds) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(max_total_milliseconds);
  std::size_t total = 0;
  for (;;) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
      throw std::runtime_error("TCP pre-acquisition drain did not become quiet");
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                               deadline - now)
                               .count();
    const int wait = static_cast<int>(
        remaining < quiet_milliseconds ? remaining : quiet_milliseconds);
    pollfd descriptor{fd_, POLLIN, 0};
    int result;
    do {
      result = poll(&descriptor, 1, wait);
    } while (result < 0 && errno == EINTR);
    if (result < 0) throw systemError("poll", errno);
    if (result == 0) return total;
    if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0)
      throw std::runtime_error("TCP socket reported an error during drain");
    total += receive(4096, 0).size();
  }
}

void probeTcpConnection(const std::string& host, std::uint16_t port,
                        int timeout_milliseconds) {
  TcpConnection connection(host, port, timeout_milliseconds);
}
