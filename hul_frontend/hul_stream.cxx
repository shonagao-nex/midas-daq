#include "hul_stream.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>

namespace hul {
namespace {
uint32_t word(const uint8_t* p, ByteOrder order) {
  if (order == ByteOrder::Little)
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

bool header(const uint8_t* bytes, ByteOrder* order, std::string* error) {
  ByteOrder candidate = *order;
  if (candidate == ByteOrder::Unknown) {
    if (word(bytes, ByteOrder::Little) == 0xFFFF4CA1)
      candidate = ByteOrder::Little;
    else if (word(bytes, ByteOrder::Big) == 0xFFFF4CA1)
      candidate = ByteOrder::Big;
  }
  if (candidate == ByteOrder::Unknown ||
      word(bytes, candidate) != 0xFFFF4CA1) {
    *error = "Scaler magic/byte order mismatch";
    return false;
  }
  const uint32_t h2 = word(bytes + 4, candidate);
  const uint32_t h3 = word(bytes + 8, candidate);
  if ((h2 & 0xFFFFF800u) != 0xFF000000u || (h2 & 0x7FFu) != kChannels) {
    *error = "Scaler header2/body length mismatch";
    return false;
  }
  if ((h3 & 0xFFF00000u) != 0xFF000000u) {
    *error = "Scaler header3 fixed bits/HRM mismatch";
    return false;
  }
  *order = candidate;
  return true;
}
}  // namespace

bool decode_frame(const uint8_t* bytes, size_t length, ByteOrder* order,
                  ScalerEvent* event, std::string* error) {
  if (!bytes || !order || !event || !error || length != kFrameBytes) {
    if (error) *error = "Scaler frame size mismatch";
    return false;
  }
  ByteOrder parsed = *order;
  if (!header(bytes, &parsed, error)) return false;
  ScalerEvent result;
  const uint32_t h3 = word(bytes + 8, parsed);
  result.self_counter = h3 & 0xFFFFu;
  result.tag = (h3 >> 16) & 0xFu;
  size_t seen_u = 0, seen_d = 0;
  for (size_t i = 0; i < kChannels; ++i) {
    const uint32_t w = word(bytes + kHeaderBytes + i * 4, parsed);
    const uint32_t block = w >> 28;
    if (block == 8 && seen_u < 32)
      result.count[seen_u++] = w & 0x0FFFFFFFu;
    else if (block == 9 && seen_d < 32)
      result.count[32 + seen_d++] = w & 0x0FFFFFFFu;
    else {
      *error = "Scaler block ID or channel count mismatch";
      return false;
    }
  }
  if (seen_u != 32 || seen_d != 32) {
    *error = "Scaler block count mismatch";
    return false;
  }
  *order = parsed;
  *event = result;
  return true;
}

StreamReader::ExactStatus StreamReader::exact(uint8_t* dst, size_t size,
                                               int timeout_ms,
                                               std::string* error) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  size_t received = 0;
  while (received < size) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) {
      if (received) *error = "Scaler partial frame timeout";
      return received ? ExactStatus::Error : ExactStatus::Idle;
    }
    pollfd pfd{fd_, POLLIN, 0};
    const int ready = poll(&pfd, 1, static_cast<int>(remaining));
    if (ready == 0) continue;
    if (ready < 0) {
      if (errno == EINTR) continue;
      *error = "Scaler TCP poll error";
      return ExactStatus::Error;
    }
    if (pfd.revents & (POLLERR | POLLNVAL)) {
      *error = "Scaler TCP socket error";
      return ExactStatus::Error;
    }
    const ssize_t n = recv(fd_, dst + received, size - received, 0);
    if (n == 0) {
      if (received) *error = "Scaler TCP EOF within frame";
      return received ? ExactStatus::Error : ExactStatus::Eof;
    }
    if (n < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
      *error = "Scaler TCP receive error";
      return ExactStatus::Error;
    }
    received += static_cast<size_t>(n);
  }
  return ExactStatus::Complete;
}

ReadStatus StreamReader::next(ScalerEvent* event, int timeout_ms,
                              std::string* error) {
  if (!event || !error || timeout_ms <= 0) return ReadStatus::Error;
  uint8_t frame[kFrameBytes]{};
  const auto h = exact(frame, kHeaderBytes, timeout_ms, error);
  if (h == ExactStatus::Idle) return ReadStatus::Idle;
  if (h == ExactStatus::Eof) return ReadStatus::Eof;
  if (h == ExactStatus::Error) return ReadStatus::Error;
  ByteOrder candidate = order_;
  if (!header(frame, &candidate, error)) return ReadStatus::Error;
  const auto b = exact(frame + kHeaderBytes, kFrameBytes - kHeaderBytes,
                       timeout_ms, error);
  if (b != ExactStatus::Complete) {
    if (b == ExactStatus::Idle || b == ExactStatus::Eof)
      *error = "Scaler EOF/timeout within body";
    return ReadStatus::Error;
  }
  if (!decode_frame(frame, sizeof(frame), &candidate, event, error))
    return ReadStatus::Error;
  order_ = candidate;
  return ReadStatus::Event;
}
}  // namespace hul
