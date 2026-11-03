#pragma once
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <string>

namespace hul {
// Single-threaded, nonblocking RBCP read monitor. No write operation exists.
class ReachabilityMonitor {
 public:
  using Clock = std::chrono::steady_clock;
  ~ReachabilityMonitor() { cancel(); }
  void cancel() { if (fd_ >= 0) close(fd_); fd_ = -1; next_ = {}; state_ = "Unknown"; }
  const char* tick(bool enabled, bool allowed, const std::string& ip,
                   unsigned port, Clock::time_point now = Clock::now()) {
    if (!enabled || !allowed) {
      cancel(); state_ = enabled ? "Unknown" : "Disabled"; return state_;
    }
    if (std::strcmp(state_, "Disabled") == 0) state_ = "Unknown";
    if (ip != ip_ || port != port_) { cancel(); ip_ = ip; port_ = port; }
    if (fd_ >= 0) {
      unsigned char response[32];
      const auto n = recv(fd_, response, sizeof(response), MSG_DONTWAIT);
      if (n >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) || now >= deadline_) {
        state_ = n == 9 && response[0] == 0xff && response[1] == 0xc8 &&
          response[2] == request_[2] && response[3] == 1 &&
          std::memcmp(response + 4, request_.data() + 4, 4) == 0 ? "Comm OK" : "Disconnected";
        close(fd_); fd_ = -1;
      }
      return state_;
    }
    if (now < next_) return state_;
    next_ = now + std::chrono::seconds(2);
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
    if (!port || port > 65535 || inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
      state_ = "Unknown"; return state_;
    }
    fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd_ < 0) { state_ = "Unknown"; return state_; }
    // Scaler RegisterMap.hh SCR::kAddrEnableBlock (W/R), read only.
    request_ = {{0xff, 0xc0, ++id_, 1, 0x20, 0x10, 0, 0}};
    if (connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        send(fd_, request_.data(), request_.size(), 0) != 8) {
      close(fd_); fd_ = -1; state_ = "Disconnected"; return state_;
    }
    deadline_ = now + std::chrono::milliseconds(200);
    return state_;
  }
 private:
  int fd_ = -1;
  unsigned port_ = 0;
  std::string ip_;
  unsigned char id_ = 0;
  std::array<unsigned char, 8> request_{};
  Clock::time_point next_{}, deadline_{};
  const char* state_ = "Unknown";
};
}
