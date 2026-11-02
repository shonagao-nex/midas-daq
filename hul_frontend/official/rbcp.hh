#ifndef RBCPDEC_HH
#define RBCPDEC_HH

/*************************************************
*                                                *
* SiTCP Remote Bus Control Protocol              *
* Header file                                    *
*                                                *
* 2010/05/31 Tomohisa Uchida                     *
*                                                *
*************************************************/
#include<stdint.h>

namespace RBCP{

struct RbcpHeader{
  uint8_t  type;
  uint8_t  command;
  uint8_t  id;
  uint8_t  length;
  uint32_t address;
};
};

#endif
