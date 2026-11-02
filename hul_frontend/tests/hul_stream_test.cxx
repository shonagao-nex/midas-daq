#include "hul_stream.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace {
void put(std::vector<uint8_t>& b, uint32_t value, bool big) {
  for (int i = 0; i < 4; ++i)
    b.push_back((value >> (8 * (big ? 3 - i : i))) & 0xff);
}
std::vector<uint8_t> frame(bool big) {
  std::vector<uint8_t> b;
  put(b, 0xFFFF4CA1, big);
  put(b, 0xFF000040, big);
  put(b, 0xFF020007, big);
  for (int i = 0; i < 32; ++i) put(b, 0x80000000u | i, big);
  for (int i = 0; i < 32; ++i) put(b, 0x90000000u | (100 + i), big);
  return b;
}
void write_all(int fd, const std::vector<uint8_t>& b, size_t chunk) {
  for (size_t i = 0; i < b.size(); i += chunk)
    assert(write(fd, b.data() + i, std::min(chunk, b.size() - i)) > 0);
  close(fd);
}
}  // namespace

int main() {
  for (bool big : {false, true}) {
    auto b = frame(big);
    hul::ByteOrder order = hul::ByteOrder::Unknown;
    hul::ScalerEvent event;
    std::string error;
    assert(hul::decode_frame(b.data(), b.size(), &order, &event, &error));
    assert(event.count[0] == 0 && event.count[31] == 31);
    assert(event.count[32] == 100 && event.count[63] == 131);
    assert(event.self_counter == 7 && event.tag == 2);

    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    std::thread writer(write_all, pair[1], b, 3);
    hul::StreamReader reader(pair[0]);
    assert(reader.next(&event, 1000, &error) == hul::ReadStatus::Event);
    writer.join();
    assert(reader.next(&event, 1000, &error) == hul::ReadStatus::Eof);
    close(pair[0]);
  }
  auto b = frame(false);
  b[4] = 0xff; // 255 body words instead of 64, reject before body read.
  int pair[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
  assert(write(pair[1], b.data(), 12) == 12);
  close(pair[1]);
  hul::StreamReader reader(pair[0]);
  hul::ScalerEvent event;
  std::string error;
  assert(reader.next(&event, 100, &error) == hul::ReadStatus::Error);
  close(pair[0]);
  b = frame(false);
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
  assert(write(pair[1], b.data(), 13) == 13);
  close(pair[1]);
  hul::StreamReader cut(pair[0]);
  assert(cut.next(&event, 100, &error) == hul::ReadStatus::Error);
  close(pair[0]);
  b = frame(false);
  b[12 + 4 * 63 + 3] = 0x70;
  hul::ByteOrder order = hul::ByteOrder::Unknown;
  assert(!hul::decode_frame(b.data(), b.size(), &order, &event, &error));
}
