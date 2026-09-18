#include "easiroc_stream.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int tests_run = 0;

void check(bool condition, const std::string& message) {
  ++tests_run;
  if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void expectFailure(Function function, const std::string& message) {
  ++tests_run;
  try {
    function();
  } catch (const std::exception&) {
    return;
  }
  throw std::runtime_error(message);
}

std::array<std::uint8_t, 4> encode(std::uint32_t word) {
  return {static_cast<std::uint8_t>(0x80 | ((word >> 21) & 0x7f)),
          static_cast<std::uint8_t>((word >> 14) & 0x7f),
          static_cast<std::uint8_t>((word >> 7) & 0x7f),
          static_cast<std::uint8_t>(word & 0x7f)};
}

std::vector<std::uint8_t> makeEvent(
    const std::vector<std::uint32_t>& data_words) {
  std::vector<std::uint8_t> bytes;
  const auto header = encode(0x08000000 | data_words.size());
  bytes.insert(bytes.end(), header.begin(), header.end());
  for (const auto word : data_words) {
    const auto encoded = encode(word);
    bytes.insert(bytes.end(), encoded.begin(), encoded.end());
  }
  return bytes;
}

std::vector<std::uint8_t> concatenate(
    const std::vector<std::vector<std::uint8_t>>& parts) {
  std::vector<std::uint8_t> result;
  for (const auto& part : parts)
    result.insert(result.end(), part.begin(), part.end());
  return result;
}

std::vector<easiroc::Event> feedAtSplits(
    const std::vector<std::uint8_t>& bytes,
    const std::vector<std::size_t>& chunk_sizes) {
  easiroc::EventStreamParser parser;
  std::vector<easiroc::Event> events;
  std::size_t offset = 0;
  for (const std::size_t requested : chunk_sizes) {
    const std::size_t size =
        requested < bytes.size() - offset ? requested : bytes.size() - offset;
    auto parsed = parser.push(bytes.data() + offset, size);
    events.insert(events.end(), parsed.begin(), parsed.end());
    offset += size;
    if (offset == bytes.size()) break;
  }
  if (offset < bytes.size()) {
    auto parsed = parser.push(bytes.data() + offset, bytes.size() - offset);
    events.insert(events.end(), parsed.begin(), parsed.end());
  }
  parser.finish();
  return events;
}
}  // namespace

int main() {
  try {
    const auto event_a = makeEvent({0x00001234, 0x00205678, 0x00412345});
    const auto event_b = makeEvent({0x0008a321});
    const auto event_c = makeEvent({});
    const auto two_events = concatenate({event_a, event_b});
    const auto three_events = concatenate({event_a, event_b, event_c});

    check(feedAtSplits(event_a, {event_a.size()}).size() == 1,
          "one complete event in one chunk failed");
    check(feedAtSplits(event_a, {6}).size() == 1,
          "split inside a four-byte word failed");
    check(feedAtSplits(event_a, {2}).size() == 1,
          "split inside header failed");
    check(feedAtSplits(event_a, {10}).size() == 1,
          "split inside event data failed");
    check(feedAtSplits(event_a, std::vector<std::size_t>(event_a.size(), 1))
                  .size() == 1,
          "one-byte chunks failed");
    check(feedAtSplits(two_events, {two_events.size()}).size() == 2,
          "two events in one chunk failed");
    check(feedAtSplits(three_events, {three_events.size()}).size() == 3,
          "multiple events in one chunk failed");

    bool every_split_ok = true;
    for (std::size_t split = 1; split < two_events.size(); ++split) {
      const auto events = feedAtSplits(two_events, {split});
      every_split_ok = every_split_ok && events.size() == 2;
    }
    check(every_split_ok,
          "an arbitrary split across an event boundary failed");

    auto malformed = event_a;
    malformed[5] |= 0x80;
    expectFailure(
        [&] {
          easiroc::EventStreamParser parser;
          parser.push(malformed);
        },
        "malformed wire word was accepted");

    expectFailure(
        [] {
          easiroc::EventStreamParser parser;
          parser.push(std::vector<std::uint8_t>{0x80, 0x00, 0x00, 0x01});
        },
        "non-header at event boundary was accepted");

    expectFailure(
        [] {
          easiroc::EventStreamParser parser;
          parser.push(std::vector<std::uint8_t>{0xc0, 0x00, 0x00, 0x02,
                                                0x80, 0x00, 0x00, 0x01});
          parser.finish();
        },
        "header/data size mismatch was accepted at end-of-stream");

    {
      easiroc::EventStreamParser parser;
      const std::size_t split = event_a.size() - 3;
      check(parser.push(event_a.data(), split).empty() &&
                parser.bufferedBytes() == split,
            "incomplete event was not retained");
      const auto completed =
          parser.push(event_a.data() + split, event_a.size() - split);
      check(completed.size() == 1 && parser.bufferedBytes() == 0,
            "retained event did not complete on next chunk");
      parser.finish();
    }

    const auto ordered = feedAtSplits(three_events, {3, 8, 5, 1});
    check(ordered.size() == 3 && ordered[0].data_size == 3 &&
              ordered[1].data_size == 1 && ordered[2].data_size == 0,
          "continuous events were not returned in input order");

    std::cout << "easiroc_stream_test: " << tests_run << " checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << "easiroc_stream_test: FAILED: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
