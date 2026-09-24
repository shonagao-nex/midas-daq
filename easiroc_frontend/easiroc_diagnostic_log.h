#ifndef EASIROC_FRONTEND_EASIROC_DIAGNOSTIC_LOG_H
#define EASIROC_FRONTEND_EASIROC_DIAGNOSTIC_LOG_H

#include <optional>

namespace easiroc {

enum class DiagnosticLogEvent { kInitialOk, kNoLog, kError, kRestored };

class DiagnosticLogPolicy {
 public:
  DiagnosticLogEvent observe(bool ok) {
    if (!ok) {
      previous_ok_ = false;
      return DiagnosticLogEvent::kError;
    }
    if (!previous_ok_) {
      previous_ok_ = true;
      return DiagnosticLogEvent::kInitialOk;
    }
    if (!*previous_ok_) {
      previous_ok_ = true;
      return DiagnosticLogEvent::kRestored;
    }
    return DiagnosticLogEvent::kNoLog;
  }

  void reset() { previous_ok_.reset(); }

 private:
  std::optional<bool> previous_ok_;
};

}  // namespace easiroc

#endif
