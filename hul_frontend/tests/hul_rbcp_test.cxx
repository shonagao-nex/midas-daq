#include "official/FPGAModule.hh"
#include "official/UDPRBCP.hh"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cassert>
#include <cstdint>
#include <thread>

namespace {
void transaction(bool valid, bool reading) {
  int server = socket(AF_INET, SOCK_DGRAM, 0);
  assert(server >= 0);
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(bind(server, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0);
  socklen_t size = sizeof(local);
  assert(getsockname(server, reinterpret_cast<sockaddr*>(&local), &size) == 0);
  std::thread responder([&] {
    uint8_t request[32]{};
    sockaddr_in source{};
    socklen_t source_size = sizeof(source);
    const ssize_t n = recvfrom(server, request, sizeof(request), 0,
                               reinterpret_cast<sockaddr*>(&source), &source_size);
    assert(n == (reading ? 8 : 9));
    request[1] = valid ? 0x88 : 0x80; // only official ACK low nibble is assumed.
    if (reading) request[8] = 0x5a;
    assert(sendto(server, request, reading ? 9 : 8, 0,
                  reinterpret_cast<sockaddr*>(&source), source_size) > 0);
    timeval timeout{0, 100000};
    setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    assert(recvfrom(server, request, sizeof(request), 0, nullptr, nullptr) < 0);
    close(server);
  });
  RBCP::UDPRBCP rbcp("127.0.0.1", ntohs(local.sin_port), RBCP::UDPRBCP::kNoDisp);
  HUL::FPGAModule module(rbcp);
  if (reading) {
    assert(module.ReadModule_nByte(0x10000000, 1) > 0);
    auto it = module.GetDataIteratorBegin();
    assert(it != module.GetDataIteratorEnd() && *it == 0x5a);
  } else if (valid) {
    assert(module.WriteModule(0x10000000, 1) == 0);
  } else {
    assert(module.WriteModule(0x10000000, 1) < 0);
  }
  responder.join();
}

void write_timeout_is_not_retried() {
  int server = socket(AF_INET, SOCK_DGRAM, 0);
  assert(server >= 0);
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(bind(server, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0);
  socklen_t size = sizeof(local);
  assert(getsockname(server, reinterpret_cast<sockaddr*>(&local), &size) == 0);
  std::thread responder([&] {
    uint8_t request[32]{};
    timeval timeout{1, 300000};
    setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    assert(recvfrom(server, request, sizeof(request), 0, nullptr, nullptr) == 9);
    assert(recvfrom(server, request, sizeof(request), 0, nullptr, nullptr) < 0);
    close(server);
  });
  RBCP::UDPRBCP rbcp("127.0.0.1", ntohs(local.sin_port), RBCP::UDPRBCP::kNoDisp);
  HUL::FPGAModule module(rbcp);
  assert(module.WriteModule(0x20000000, 1) < 0);
  responder.join();
}
}  // namespace

int main() {
  RBCP::UDPRBCP rbcp("127.0.0.1", 4660, RBCP::UDPRBCP::kNoDisp);
  uint8_t byte = 1;
  assert(rbcp.SetWD(0, 256, &byte) < 0);
  assert(rbcp.SetRD(0, 256) < 0);
  transaction(true, false);
  transaction(false, false);
  transaction(true, true);
  write_timeout_is_not_retried();
}
