#include "hul_bank.h"

#include "midas.h"

namespace hul {
int encode_record(char* pevent, const Record& record) {
  bk_init32(pevent);
  uint32_t* data = nullptr;
  bk_create(pevent, "HCUM", TID_DWORD, reinterpret_cast<void**>(&data));
  for (auto value : record.cumulative) *data++ = value;
  bk_close(pevent, data);
  bk_create(pevent, "HDIF", TID_DWORD, reinterpret_cast<void**>(&data));
  for (auto value : record.delta) *data++ = value;
  bk_close(pevent, data);
  bk_create(pevent, "HVAL", TID_DWORD, reinterpret_cast<void**>(&data));
  *data++ = static_cast<uint32_t>(record.run_number);
  *data++ = record.final ? 1 : 0;
  *data++ = record.cumulative_valid ? 1 : 0;
  *data++ = record.fresh ? 1 : 0;
  *data++ = record.fault ? 1 : 0;
  *data++ = record.valid_low;
  *data++ = record.valid_high;
  *data++ = record.age_ms;
  *data++ = record.self_counter;
  *data++ = record.receive_unix_ms;
  *data++ = record.receive_unix_ms >> 32;
  *data++ = record.accepted_events;
  *data++ = record.accepted_events >> 32;
  *data++ = record.missing_events;
  *data++ = record.missing_events >> 32;
  bk_close(pevent, data);
  return bk_size(pevent);
}
}  // namespace hul
