#include "hul_monitor.h"
#include <cassert>
#include <poll.h>
#include <iostream>
int main() {
  int server = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  assert(bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))==0);
  socklen_t len=sizeof(addr); assert(getsockname(server,reinterpret_cast<sockaddr*>(&addr),&len)==0);
  const unsigned port=ntohs(addr.sin_port);
  hul::ReachabilityMonitor monitor;
  auto now=hul::ReachabilityMonitor::Clock::now();
  auto tick=[&](bool enabled,bool allowed){return std::string(monitor.tick(enabled,allowed,"127.0.0.1",port,now));};
  unsigned char req[16]{}; sockaddr_in client{}; len=sizeof(client);
  assert(tick(false,true)=="Disabled"); assert(recv(server,req,sizeof(req),0)<0);
  assert(tick(true,false)=="Unknown"); assert(recv(server,req,sizeof(req),0)<0);
  assert(tick(true,true)=="Unknown");
  assert(recvfrom(server,req,sizeof(req),0,reinterpret_cast<sockaddr*>(&client),&len)==8);
  assert(req[0]==0xff && req[1]==0xc0 && req[3]==1);
  assert(req[4]==0x20 && req[5]==0x10 && req[6]==0 && req[7]==0);
  unsigned char reply[9]; std::memcpy(reply,req,8); reply[1]=0xc8; reply[8]=3;
  assert(sendto(server,reply,9,0,reinterpret_cast<sockaddr*>(&client),len)==9);
  assert(tick(true,true)=="Comm OK");
  // Register content is not a success predicate: every payload byte is valid.
  for (unsigned value : {0u, 3u, 15u, 255u}) {
    monitor.cancel(); tick(true,true);
    assert(recvfrom(server,req,sizeof(req),0,reinterpret_cast<sockaddr*>(&client),&len)==8);
    std::memcpy(reply,req,8); reply[1]=0xc8; reply[8]=value;
    sendto(server,reply,9,0,reinterpret_cast<sockaddr*>(&client),len);
    assert(tick(true,true)=="Comm OK");
  }
  // OFF cancels an in-flight transaction, and never sends another request.
  now+=std::chrono::seconds(3); tick(true,true);
  assert(recvfrom(server,req,sizeof(req),0,reinterpret_cast<sockaddr*>(&client),&len)==8);
  assert(tick(false,true)=="Disabled"); assert(recv(server,req,sizeof(req),0)<0);
  assert(tick(true,true)=="Unknown");
  assert(recvfrom(server,req,sizeof(req),0,reinterpret_cast<sockaddr*>(&client),&len)==8);
  now+=std::chrono::milliseconds(201); assert(tick(true,true)=="Disconnected");
  // Transition admission cancels, so BOR/EOR can run without a monitor socket.
  assert(tick(true,false)=="Unknown"); assert(recv(server,req,sizeof(req),0)<0);
  tick(true,true); assert(recvfrom(server,req,sizeof(req),0,reinterpret_cast<sockaddr*>(&client),&len)==8);
  std::memcpy(reply,req,8); reply[1]=0xc8; reply[2]++; // mismatched transaction must fail
  sendto(server,reply,9,0,reinterpret_cast<sockaddr*>(&client),len);
  assert(tick(true,true)=="Disconnected");
  close(server);
  std::cout<<"monitor: OFF/no-send, ACK, timeout, invalid ACK, transition cancellation passed\n";
}
