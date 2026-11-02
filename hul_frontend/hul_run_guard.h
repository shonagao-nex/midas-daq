#pragma once

#include <cstdint>

namespace hul {

// An EOR event may have been enqueued without a successful flush. Never
// enqueue it twice; a failed enqueue is ambiguous and is not auto-retried.
class FinalGuard {
 public:
  void begin(int32_t run_number) {
    run_number_ = run_number;
    prepared_ = attempted_ = enqueued_ = flushed_ = false;
  }
  bool prepare(int32_t run_number) {
    if (run_number != run_number_ || run_number_ <= 0) return false;
    prepared_ = true;
    return true;
  }
  bool may_enqueue() const { return prepared_ && !attempted_; }
  void enqueue_result(bool success) {
    attempted_ = true;
    enqueued_ = success;
  }
  bool may_flush() const { return prepared_ && enqueued_ && !flushed_; }
  void flush_result(bool success) { if (success) flushed_ = true; }
  bool complete() const { return prepared_ && flushed_; }

 private:
  int32_t run_number_ = 0;
  bool prepared_ = false;
  bool attempted_ = false;
  bool enqueued_ = false;
  bool flushed_ = false;
};
}  // namespace hul
