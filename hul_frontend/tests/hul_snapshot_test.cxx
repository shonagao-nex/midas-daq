#include "hul_snapshot.h"

#include <cassert>
#include <chrono>

int main() {
  hul::SnapshotAccumulator a;
  const auto t = hul::Clock::now();
  a.reset(42); // BOR resets baseline and run identity.
  auto r = a.take(t, 2000, false);
  assert(!r.cumulative_valid && !r.fresh && r.run_number == 42);
  hul::ScalerEvent e;
  e.count.fill(100);
  e.self_counter = 0;
  a.accept(e, t, 12345);
  r = a.take(t, 2000, false);
  assert(r.cumulative_valid && r.fresh && r.valid_low == 0);
  e.count.fill(105);
  e.count[1] = 90;
  e.self_counter = 1;
  a.accept(e, t + std::chrono::milliseconds(500), 12845);
  r = a.take(t + std::chrono::milliseconds(500), 2000, true); // EOR final.
  assert(r.final && r.run_number == 42 && r.fresh);
  assert(r.delta[0] == 5 && (r.valid_low & 1));
  assert(!(r.valid_low & 2));
  assert(r.receive_unix_ms == 12845);
  r = a.take(t + std::chrono::milliseconds(600), 2000, false);
  assert(!r.fresh && r.valid_low == 0 && r.cumulative[1] == 90);
  e.count.fill(110);
  e.self_counter = 2;
  a.accept(e, t + std::chrono::seconds(5), 17345);
  r = a.take(t + std::chrono::seconds(5), 2000, false);
  assert(r.fresh && r.valid_low == 0); // stale gap resets delta baseline.
  e.self_counter = 3;
  a.accept(e, t + std::chrono::seconds(6), 18345);
  r = a.take(t + std::chrono::seconds(6), 2000, true, true);
  assert(r.cumulative_valid && !r.fresh && r.fault && r.valid_low == 0);
  a.reset(43);
  r = a.take(t, 2000, true);
  assert(r.final && !r.cumulative_valid && r.run_number == 43);
}
