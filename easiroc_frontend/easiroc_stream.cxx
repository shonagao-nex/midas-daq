#include "easiroc_stream.h"

#include <array>
#include <stdexcept>

namespace easiroc {
namespace {
constexpr std::uint32_t kHeaderFlag = 0x08000000;

std::array<std::uint8_t, 4> firstWord(
    const std::vector<std::uint8_t>& buffer) {
  return {buffer[0], buffer[1], buffer[2], buffer[3]};
}
}  // namespace

std::vector<Event> EventStreamParser::push(const std::uint8_t* data,
                                           std::size_t size) {
  if (failed_)
    throw std::runtime_error("EASIROC stream parser is in failed state");
  if (size != 0 && data == nullptr)
    throw std::invalid_argument("null stream data with non-zero size");

  if (size != 0) buffer_.insert(buffer_.end(), data, data + size);
  try {
    return parseAvailable();
  } catch (...) {
    failed_ = true;
    throw;
  }
}

std::vector<Event> EventStreamParser::push(
    const std::vector<std::uint8_t>& data) {
  return push(data.data(), data.size());
}

std::vector<Event> EventStreamParser::parseAvailable() {
  std::vector<Event> events;
  while (buffer_.size() >= 4) {
    const std::uint32_t header = decodeWireWord(firstWord(buffer_));
    if ((header & kHeaderFlag) == 0)
      throw std::runtime_error("stream word at event boundary is not a header");

    const std::size_t data_size = header & 0x0fff;
    const std::size_t event_size = (data_size + 1) * 4;
    if (buffer_.size() < event_size) break;

    const std::vector<std::uint8_t> event_bytes(buffer_.begin(),
                                                buffer_.begin() + event_size);
    events.push_back(decodeEvent(event_bytes));
    buffer_.erase(buffer_.begin(), buffer_.begin() + event_size);
  }
  return events;
}

void EventStreamParser::finish() const {
  if (failed_)
    throw std::runtime_error("EASIROC stream parser is in failed state");
  if (!buffer_.empty())
    throw std::runtime_error("incomplete EASIROC event at end of stream");
}

}  // namespace easiroc
