#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace hul {

constexpr size_t kChannels = 64;
constexpr size_t kHeaderBytes = 12;
constexpr size_t kFrameBytes = kHeaderBytes + kChannels * 4;

enum class ByteOrder { Unknown, Little, Big };
enum class ReadStatus { Event, Idle, Eof, Error };

struct ScalerEvent {
  std::array<uint32_t, kChannels> count{};
  uint16_t self_counter = 0;
  uint8_t tag = 0;
};

// Header3 and block layout: HUL User Guide 4.6, pp.38-39.
bool decode_frame(const uint8_t* bytes, size_t length, ByteOrder* order,
                  ScalerEvent* event, std::string* error);

// Follows Scaler/src/DaqFuncs.cc:143-189, with explicit exact reads,
// 11-bit length validation before body read, and bounded wait/EOF handling.
class StreamReader {
 public:
  explicit StreamReader(int fd) : fd_(fd) {}
  ReadStatus next(ScalerEvent* event, int timeout_ms, std::string* error);
  ByteOrder byte_order() const { return order_; }

 private:
  enum class ExactStatus { Complete, Idle, Eof, Error };
  ExactStatus exact(uint8_t* dst, size_t size, int timeout_ms, std::string* error);
  int fd_;
  ByteOrder order_ = ByteOrder::Unknown;
};

}  // namespace hul
