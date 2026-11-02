#include "hul_run_guard.h"

#include <cassert>

int main() {
  hul::FinalGuard guard;
  guard.begin(42); // BOR
  assert(!guard.prepare(43)); // wrong RUN cannot finish another RUN.
  assert(guard.prepare(42)); // EOR
  assert(guard.may_enqueue());
  guard.enqueue_result(true);
  assert(!guard.may_enqueue()); // no duplicate if flush fails.
  assert(guard.may_flush());
  guard.flush_result(false);
  assert(!guard.complete() && guard.may_flush());
  guard.flush_result(true);
  assert(guard.complete() && !guard.may_flush());
  guard.begin(43);
  assert(guard.prepare(43) && guard.may_enqueue());
  guard.enqueue_result(false); // send result ambiguous: do not retry.
  assert(!guard.may_enqueue() && !guard.may_flush() && !guard.complete());
}
