#include "hul_bank.h"

#include "midas.h"

#include <cassert>
#include <cstdint>

int main() {
  alignas(8) char buffer[2048]{};
  hul::Record record;
  record.run_number = 42;
  record.final = true;
  record.cumulative_valid = true;
  record.fault = true;
  record.cumulative[63] = 0x0FFFFFFF;
  record.receive_unix_ms = 0x123456789aULL;
  const int size = hul::encode_record(buffer, record);
  assert(size > 0 && size < static_cast<int>(sizeof(buffer)));
  DWORD length = 0, type = 0;
  void* data = nullptr;
  assert(bk_find(reinterpret_cast<BANK_HEADER*>(buffer), "HCUM", &length,
                 &type, &data) != 0);
  assert(length == 64 && type == TID_DWORD);
  assert(static_cast<uint32_t*>(data)[63] == 0x0FFFFFFF);
  assert(bk_find(reinterpret_cast<BANK_HEADER*>(buffer), "HVAL", &length,
                 &type, &data) != 0);
  assert(length == 15 && type == TID_DWORD);
  auto* values = static_cast<uint32_t*>(data);
  assert(values[0] == 42 && values[1] == 1 && values[4] == 1);
  assert(values[9] == 0x3456789a && values[10] == 0x12);
}
