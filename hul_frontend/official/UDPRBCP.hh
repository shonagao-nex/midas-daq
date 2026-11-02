#ifndef UDPRBCP_
#define UDPRBCP_

#include<vector>
#include<algorithm>
#include<string>
#include<stdint.h>

#include"rbcp.hh"
#include"Uncopyable.hh"

namespace RBCP{
  
const uint32_t gUdpPort = 4660;

class UDPRBCP
  : Uncopyable<UDPRBCP>
{
 public:
  static const uint32_t kSizeUdpBuf_ {4096};
  static const uint32_t kRbcpVer_    {0xFF};  
  enum RbcpDebugMode
    {
      kNoDisp, kInteractive, kDebug,
      kSizeRbcpDebugMode
    };
  
 private:
  const uint32_t kRbcpCmdWr_  {0x80};  
  const uint32_t kRbcpCmdRd_  {0xC0};  

  const std::string  ip_addr_;
  const uint32_t     port_;
  RbcpHeader         send_header_;

  int32_t            length_rd_;
  uint8_t            wd_buffer_[kSizeUdpBuf_];
  uint8_t            rd_buffer_[kSizeUdpBuf_];

  RbcpDebugMode      mode_;

public:
  UDPRBCP(const std::string ipAddr, const uint32_t port,
	  const RbcpDebugMode mode = kInteractive);
  virtual ~UDPRBCP();
  
  void SetDispMode(const RbcpDebugMode mode);
  void SetRbcpVer(const uint8_t verion);
  void SetRbcpId(const uint8_t id);
  // Local change: return failure for invalid lengths instead of truncating
  // the RBCP 8-bit length or overflowing the copied write buffer.
  int SetWD(const uint32_t address, const uint32_t length,
	     const uint8_t* send_data);
  int SetRD(const uint32_t address, const uint32_t length);
  int  DoRBCP();

  void CopyRD(std::vector<uint8_t>& buffer);
};

inline void UDPRBCP::CopyRD(std::vector<uint8_t>& buffer){
  std::copy(rd_buffer_, rd_buffer_ +length_rd_,
	    back_inserter(buffer)
	    );
}
};

#endif
