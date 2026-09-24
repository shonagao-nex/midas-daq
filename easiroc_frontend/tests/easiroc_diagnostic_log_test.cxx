#include "easiroc_diagnostic_log.h"

#include <iostream>
#include <stdexcept>

int main() {
  using easiroc::DiagnosticLogEvent;
  easiroc::DiagnosticLogPolicy policy;

  if (policy.observe(true) != DiagnosticLogEvent::kInitialOk ||
      policy.observe(true) != DiagnosticLogEvent::kNoLog ||
      policy.observe(false) != DiagnosticLogEvent::kError ||
      policy.observe(false) != DiagnosticLogEvent::kError ||
      policy.observe(true) != DiagnosticLogEvent::kRestored ||
      policy.observe(true) != DiagnosticLogEvent::kNoLog)
    throw std::runtime_error("diagnostic log transition sequence failed");

  policy.reset();
  if (policy.observe(true) != DiagnosticLogEvent::kInitialOk)
    throw std::runtime_error("diagnostic log reset failed");

  policy.reset();
  if (policy.observe(false) != DiagnosticLogEvent::kError ||
      policy.observe(true) != DiagnosticLogEvent::kRestored)
    throw std::runtime_error("initial failure and recovery failed");

  std::cout << "easiroc_diagnostic_log_test: 9 checks passed\n";
}
