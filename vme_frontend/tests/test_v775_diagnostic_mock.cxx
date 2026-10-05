#include "v775_config.h"
#include <cassert>
#include <string>
#include <vector>

namespace {
struct Call { char kind; DWORD address; WORD value; };
std::vector<Call> calls;
WORD bits, status1;
DWORD fail_address;
unsigned fail_occurrence, occurrences, sleeps, polls;
bool fail_write;

bool read16(MVME_INTERFACE *, DWORD address, WORD &value, const char *)
{
  calls.push_back({'R', address, 0});
  if (address == fail_address && !fail_write && ++occurrences == fail_occurrence) return false;
  if (address == V775_BIT_SET2) value = bits;
  else if (address == V775_STATUS1) { value = status1; ++polls; }
  else value = 0;
  return true;
}
bool write16(MVME_INTERFACE *, DWORD address, WORD value, const char *)
{
  calls.push_back({'W', address, value});
  if (address == fail_address && fail_write && ++occurrences == fail_occurrence) return false;
  if (address == V775_BIT_SET2) bits |= value;
  if (address == V775_BIT_CLEAR2) bits &= ~value;
  return true;
}
void counter(MVME_INTERFACE *, DWORD, DWORD *value) { *value = 17; calls.push_back({'C', 0, 0}); }
int sleep_ms(int ms) { assert(ms == 1); ++sleeps; return 0; }
void reset() { calls.clear(); bits = status1 = 0; fail_address = 0; fail_occurrence = 0; occurrences = sleeps = polls = 0; fail_write = false; }
}

int main()
{
  const v775_config::DiagnosticAccess access = {nullptr, 0, read16, write16, counter, sleep_ms};
  v7xx_config::V775DiagnosticState state;
  WORD readback = 0, s1 = 0, s2 = 0; DWORD count = 0; unsigned done = 0;
  reset();
  assert(v775_config::save_diagnostic_settings(access, state));
  assert(v775_config::enable_empty_program(access, state, readback) == v775_config::EnableResult::Enabled);
  assert(v775_config::read_before_sw_comm(access, s1, s2, count) && count == 17);
  assert(v775_config::issue_sw_comm(access));
  status1 = V775_STATUS1_DATA_READY;
  assert(v775_config::poll_after_sw_comm(access, s1, done) == v775_config::PollResult::Ready && done == 1);
  assert(v775_config::read_after_sw_comm(access, s2, count));
  assert(v775_config::restore_diagnostic_settings(access, state, readback) == v775_config::RestoreResult::Restored);
  assert(calls.size() == 12);
  assert(calls[0].kind == 'R' && calls[0].address == V775_BIT_SET2);
  assert(calls[1].kind == 'W' && calls[1].address == V775_BIT_SET2 && calls[1].value == V775_BIT2_EMPTY_PROGRAM);
  assert(calls[6].kind == 'W' && calls[6].address == V775_SW_COMM && calls[6].value == 0);
  assert(calls[10].kind == 'W' && calls[10].address == V775_BIT_CLEAR2);

  reset(); state = {};
  assert(v775_config::save_diagnostic_settings(access, state));
  fail_address = V775_BIT_SET2; fail_occurrence = 1; fail_write = true;
  assert(v775_config::enable_empty_program(access, state, readback) == v775_config::EnableResult::AccessFailure);
  fail_address = 0;
  assert(v775_config::restore_diagnostic_settings(access, state, readback) == v775_config::RestoreResult::Restored);

  reset(); state = {};
  assert(v775_config::save_diagnostic_settings(access, state));
  assert(v775_config::enable_empty_program(access, state, readback) == v775_config::EnableResult::Enabled);
  fail_address = V775_SW_COMM; fail_occurrence = 1; fail_write = true;
  assert(!v775_config::issue_sw_comm(access));
  fail_address = 0;
  assert(v775_config::restore_diagnostic_settings(access, state, readback) == v775_config::RestoreResult::Restored);

  reset(); state = {};
  assert(v775_config::save_diagnostic_settings(access, state));
  assert(v775_config::enable_empty_program(access, state, readback) == v775_config::EnableResult::Enabled);
  assert(v775_config::issue_sw_comm(access));
  assert(v775_config::poll_after_sw_comm(access, s1, done) == v775_config::PollResult::Timeout);
  assert(done == 100 && sleeps == 99 && polls == 100);
  assert(v775_config::read_after_sw_comm(access, s2, count));
  assert(v775_config::restore_diagnostic_settings(access, state, readback) == v775_config::RestoreResult::Restored);

  reset(); state = {};
  assert(v775_config::save_diagnostic_settings(access, state));
  assert(v775_config::enable_empty_program(access, state, readback) == v775_config::EnableResult::Enabled);
  fail_address = V775_STATUS2; fail_occurrence = 1;
  assert(!v775_config::read_before_sw_comm(access, s1, s2, count));
  fail_address = 0;
  assert(v775_config::restore_diagnostic_settings(access, state, readback) == v775_config::RestoreResult::Restored);

  reset(); state = {};
  assert(v775_config::save_diagnostic_settings(access, state));
  assert(v775_config::enable_empty_program(access, state, readback) == v775_config::EnableResult::Enabled);
  fail_address = V775_BIT_CLEAR2; fail_occurrence = 1; fail_write = true;
  assert(v775_config::restore_diagnostic_settings(access, state, readback) == v775_config::RestoreResult::AccessFailure);
  assert(state.saved && state.empty_program_may_have_changed);
  fail_address = 0;
  assert(v775_config::restore_diagnostic_settings(access, state, readback) == v775_config::RestoreResult::Restored);
}
