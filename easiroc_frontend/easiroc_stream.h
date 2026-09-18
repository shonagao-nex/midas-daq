#ifndef EASIROC_FRONTEND_EASIROC_STREAM_H
#define EASIROC_FRONTEND_EASIROC_STREAM_H

#include "easiroc_event.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace easiroc {

// Socket-independent incremental parser for the NIM-EASIROC TCP byte stream.
// A parser that reports malformed input must be discarded.
class EventStreamParser {
 public:
  std::vector<Event> push(const std::uint8_t* data, std::size_t size);
  std::vector<Event> push(const std::vector<std::uint8_t>& data);

  // Call at end-of-stream. Throws if a partial word/event remains buffered.
  void finish() const;

  std::size_t bufferedBytes() const { return buffer_.size(); }

 private:
  std::vector<Event> parseAvailable();

  std::vector<std::uint8_t> buffer_;
  bool failed_ = false;
};

}  // namespace easiroc

#endif
