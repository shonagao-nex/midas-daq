#ifndef EASIROC_FRONTEND_TCP_PROBE_H
#define EASIROC_FRONTEND_TCP_PROBE_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

class TcpConnection {
 public:
  TcpConnection(const std::string& host, std::uint16_t port,
                int timeout_milliseconds);
  ~TcpConnection();
  TcpConnection(const TcpConnection&) = delete;
  TcpConnection& operator=(const TcpConnection&) = delete;

  // Returns up to max_bytes, throws on timeout, EOF, or socket error.
  std::vector<std::uint8_t> receive(std::size_t max_bytes,
                                    int timeout_milliseconds);

  // Waits for readable data without consuming it. A zero timeout is a
  // non-blocking poll. EOF is reported as readable and is diagnosed by the
  // subsequent receive().
  bool dataAvailable(int timeout_milliseconds);

  // Discards data until the socket is quiet. Throws if max_total time expires
  // while data continues to arrive, so acquisition never starts ambiguously.
  std::size_t drain(int quiet_milliseconds, int max_total_milliseconds);

 private:
  int fd_ = -1;
};

// Establishes and immediately closes a TCP connection. No data is sent or
// received, and this does not issue any DAQ or register command.
void probeTcpConnection(const std::string& host, std::uint16_t port,
                        int timeout_milliseconds);

#endif
