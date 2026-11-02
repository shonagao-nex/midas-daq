#include "hul_stream.h"

#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

int main() {
  int pair[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
  std::array<uint8_t, hul::kFrameBytes> b{};
  auto put = [&](size_t index, uint32_t word) {
    for (int i = 0; i < 4; ++i) b[index + i] = word >> (8 * i);
  };
  put(0, 0xFFFF4CA1);
  put(4, 0xFF000040);
  for (int i = 0; i < 32; ++i) {
    put(12 + i * 4, 0x80000000u | i);
    put(12 + (i + 32) * 4, 0x90000000u | i);
  }
  std::thread writer([&] {
    for (int n = 0; n < 10000; ++n) {
      put(8, 0xFF000000u | n);
      size_t sent = 0;
      while (sent < b.size()) {
        const ssize_t count = write(pair[1], b.data() + sent, b.size() - sent);
        assert(count > 0);
        sent += count;
      }
    }
    close(pair[1]);
  });
  hul::StreamReader reader(pair[0]);
  hul::ScalerEvent event;
  std::string error;
  const auto start = std::chrono::steady_clock::now();
  for (int n = 0; n < 10000; ++n) {
    assert(reader.next(&event, 1000, &error) == hul::ReadStatus::Event);
    assert(event.self_counter == n);
  }
  writer.join();
  close(pair[0]);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start).count();
  std::printf("10000 frames / 2.68 MB offline: %lld ms\n", (long long)ms);
  assert(ms < 1000); // Equivalent to sustaining the requested 10 kHz input.
}
