#include"UDPRBCP.hh"
#include"rbcp.hh"
// Local change: interactive BitDump display was removed from DoRBCP.

#include<cstdlib>
#include<cstdio>
#include<cstring>
#include<arpa/inet.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/select.h>
#include<netinet/in.h>
#include<cerrno>
#include<climits>

namespace RBCP
{

UDPRBCP::UDPRBCP(const std::string ip_addr, const uint32_t port,
		 const RbcpDebugMode mode)
  :
  ip_addr_(ip_addr),
  port_(port),
  length_rd_(0),
  mode_(mode)
{
  send_header_.type = RBCP::UDPRBCP::kRbcpVer_;
  send_header_.id   = 0;
}

UDPRBCP::~UDPRBCP()
{

}

void
UDPRBCP::SetDispMode(const RbcpDebugMode mode)
{
  mode_ = mode;
}

void
UDPRBCP::SetRbcpVer(const uint8_t version)
{
  send_header_.type = version;
}

void
UDPRBCP::SetRbcpId(const uint8_t id)
{
  send_header_.id = id;
}

// Local change: reject invalid requests before memcpy/8-bit length truncation.
int
UDPRBCP::SetWD(uint32_t address, const uint32_t length,
	       const uint8_t* send_data)
{
  if (length == 0 || length > UINT8_MAX || length > kSizeUdpBuf_ - sizeof(RbcpHeader) || !send_data)
    return -1;
  send_header_.command = kRbcpCmdWr_;
  send_header_.length  = length;
  send_header_.address = htonl(address);

  memcpy(wd_buffer_, send_data, length);
  return 0;
}

int
UDPRBCP::SetRD(const uint32_t address, const uint32_t length)
{
  if (length == 0 || length > UINT8_MAX || length > kSizeUdpBuf_ - sizeof(RbcpHeader))
    return -1;
  send_header_.command = kRbcpCmdRd_;
  send_header_.length  = length;
  send_header_.address = htonl(address);
  return 0;
}

// Local change from official CoreLib/src/UDPRBCP.cc:72-237:
// keep its header/command encoding and ACK low-nibble test, but bound all
// socket operations and lengths. Writes, including reset strobes, are never
// automatically retried because a lost ACK does not prove the write failed.
int32_t
UDPRBCP::DoRBCP()
{
  const size_t header_size = sizeof(RbcpHeader);
  const size_t request_length = send_header_.length;
  if (request_length == 0 || request_length > UINT8_MAX ||
      request_length + header_size > kSizeUdpBuf_ ||
      (send_header_.command != kRbcpCmdWr_ && send_header_.command != kRbcpCmdRd_))
    return -1;

  sockaddr_in sitcp_addr{};
  sitcp_addr.sin_family = AF_INET;
  sitcp_addr.sin_port = htons(static_cast<uint16_t>(port_));
  if (port_ == 0 || port_ > UINT16_MAX ||
      inet_pton(AF_INET, ip_addr_.c_str(), &sitcp_addr.sin_addr) != 1)
    return -1;

  uint8_t snd_buf[kSizeUdpBuf_]{};
  memcpy(snd_buf, &send_header_, header_size);
  const bool writing = send_header_.command == kRbcpCmdWr_;
  const size_t packet_length = header_size + (writing ? request_length : 0);
  if (writing)
    memcpy(snd_buf + header_size, wd_buffer_, request_length);

  const int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) return -1;
  const int attempts = writing ? 1 : 3;
  length_rd_ = 0;
  for (int attempt = 0; attempt < attempts; ++attempt) {
    // Preserve the official request ID on retransmission; reset writes are
    // never retried. A late same-length reply remains an unverified case.
    const ssize_t sent = sendto(sock, snd_buf, packet_length, 0,
                                reinterpret_cast<sockaddr*>(&sitcp_addr),
                                sizeof(sitcp_addr));
    if (sent != static_cast<ssize_t>(packet_length)) {
      close(sock);
      return -1;
    }
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);
    timeval timeout{};
    timeout.tv_sec = 1;
    const int ready = select(sock + 1, &read_set, nullptr, nullptr, &timeout);
    if (ready < 0) {
      close(sock);
      return -1;
    }
    if (ready == 0) continue;
    uint8_t rcvd_buf[kSizeUdpBuf_]{};
    sockaddr_in source{};
    socklen_t source_length = sizeof(source);
    const ssize_t received = recvfrom(sock, rcvd_buf, sizeof(rcvd_buf), 0,
                                      reinterpret_cast<sockaddr*>(&source),
                                      &source_length);
    if (received < static_cast<ssize_t>(header_size) ||
        source_length != sizeof(source) ||
        source.sin_addr.s_addr != sitcp_addr.sin_addr.s_addr) {
      close(sock);
      return -1;
    }
    // Official response success check; other header fields are not assumed
    // to have fixed values without verified hardware captures.
    if ((rcvd_buf[1] & 0x0f) != 0x8) {
      close(sock);
      return -1;
    }
    if (!writing) {
      if (received - static_cast<ssize_t>(header_size) <
          static_cast<ssize_t>(request_length)) {
        close(sock);
        return -1;
      }
      memcpy(rd_buffer_, rcvd_buf + header_size, request_length);
      length_rd_ = request_length;
    }
    if (mode_ == kDebug)
      printf("RBCP ACK: %zd bytes\n", received);
    close(sock);
    return static_cast<int32_t>(received);
  }
  close(sock);
  return -3;
}

};
