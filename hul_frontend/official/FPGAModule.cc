#include"FPGAModule.hh"
#include"UDPRBCP.hh"
#include<iostream>

namespace HUL{

// Constructor/Destructor --------------------------------------------------
FPGAModule::FPGAModule(RBCP::UDPRBCP& udp_rbcp)
  :
  udp_rbcp_(udp_rbcp)
{

}

FPGAModule::~FPGAModule()
{

}
// 32-bit length register --------------------------------------------------
// WriteModule -------------------------------------------------------------
int32_t
FPGAModule::WriteModule(const uint32_t local_address,
			const uint32_t write_data,
			const int32_t  n_cycle
			)
{
  // Local change: reject invalid cycle counts and propagate the first error.
  if(n_cycle < 1 || n_cycle > kMaxCycle){
    std::cerr << "#E :FPGAModule::WriteModule, too many cycle " 
	      << n_cycle << std::endl;
    return -1;
  }

  for(int32_t i = 0; i<n_cycle; ++i){
    uint8_t udp_wd = static_cast<uint8_t>((write_data >> kDataSize*i) & kDataMask);

    int32_t multi_byte_offset = i << kShiftMultiByteOffset;
    const int32_t ret = WriteModule_nByte(local_address+multi_byte_offset, &udp_wd, 1);
    if(ret < 0){
      std::cout << "#E :FPGAModule::WriteModule, Write error " << ret
		<< ", (" << i << "-th)" << std::endl;
      return ret;
    }
  }

  return 0;
}

// ReadModule ----------------------------------------------------------------
uint32_t
FPGAModule::ReadModule(const uint32_t local_address,
		       const int32_t n_cycle
		       )
{
  if(n_cycle < 1 || n_cycle > kMaxCycle){
    std::cerr << "#E :FPGAModule::ReadModule, too many cycle " 
	      << n_cycle << std::endl;
    return 0xeeeeeeee;
  }

  uint32_t data = 0;
  for(int32_t i = 0; i<n_cycle; ++i){
    int32_t multi_byte_offset = i << kShiftMultiByteOffset;
    if( ReadModule_nByte(local_address+multi_byte_offset, 1) > -1 && rd_data_.size() == 1){
      uint32_t tmp = static_cast<uint32_t>(rd_data_[0]);
      data += (tmp & 0xff) << kDataSize*i;
    }else{
      return 0xeeeeeeee;
    }
  }

  rd_word_ = data;
  return rd_word_;
}
  
// 64-bit length register --------------------------------------------------
// WriteModule -------------------------------------------------------------
int32_t
FPGAModule::WriteModule64(const uint32_t local_address,
			  const uint64_t write_data,
			  const int32_t  n_cycle
			  )
{
  if(n_cycle < 1 || n_cycle > kMaxCycle64){
    std::cerr << "#E :FPGAModule::WriteModule, too many cycle " 
	      << n_cycle << std::endl;
    return -1;
  }

  for(int32_t i = 0; i<n_cycle; ++i){
    uint8_t udp_wd = static_cast<uint8_t>((write_data >> kDataSize*i) & kDataMask);

    int32_t multi_byte_offset = i << kShiftMultiByteOffset;
    const int32_t ret = WriteModule_nByte(local_address+multi_byte_offset, &udp_wd, 1);
    if(ret < 0){
      std::cout << "#E :FPGAModule::WriteModule, Write error " << ret 
		<< ", (" << i << "-th)" << std::endl;
      return ret;
    }
  }

  return 0;
}

// ReadModule ----------------------------------------------------------------
uint64_t
FPGAModule::ReadModule64(const uint32_t local_address,
			 const int32_t n_cycle
			 )
{
  if(n_cycle < 1 || n_cycle > kMaxCycle64){
    std::cerr << "#E :FPGAModule::ReadModule, too many cycle " 
	      << n_cycle << std::endl;
    return 0xeeeeeeee;
  }

  uint64_t data = 0;
  for(int32_t i = 0; i<n_cycle; ++i){
    int32_t multi_byte_offset = i << kShiftMultiByteOffset;
    if( ReadModule_nByte(local_address+multi_byte_offset, 1) > -1 && rd_data_.size() == 1){
      uint64_t tmp = static_cast<uint64_t>(rd_data_[0]);
      data += (tmp & 0xff) << kDataSize*i;
    }else{
      return 0xeeeeeeeeeeeeeeee;
    }
  }

  rd_word64_ = data;
  return rd_word64_;
}


// WriteModule -------------------------------------------------------------
int32_t
FPGAModule::WriteModule_nByte(const uint32_t local_address,
			      const uint8_t* write_data,
			      const uint32_t n_byte
			      )
{
  uint32_t udp_addr = local_address;
  if (udp_rbcp_.SetWD(udp_addr, n_byte, write_data) < 0) return -1;

  int32_t ret = 0;
  if( 0 > (ret = udp_rbcp_.DoRBCP())){
      std::cout << "#E :FPGAModule::WriteModule_nByte, Write error " << ret 
		<< std::endl;
  }

  return ret;
}

// ReadModule_nByte --------------------------------------------------------
int32_t
FPGAModule::ReadModule_nByte(const uint32_t local_address,
			     const uint32_t n_byte
			     )
{
  rd_data_.clear();
  uint32_t udp_addr = local_address;

  if (udp_rbcp_.SetRD(udp_addr, n_byte) < 0) return -1;
  int32_t ret = 0;;
  if((ret = udp_rbcp_.DoRBCP()) > -1){ udp_rbcp_.CopyRD(rd_data_); }

  return ret;
}
};
