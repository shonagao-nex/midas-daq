#ifndef ANA_ANALYZER_MODE_H
#define ANA_ANALYZER_MODE_H

#include <cstddef>

namespace ana {

enum class AnalyzerMode { kOnline, kOffline };

struct EventInspectorOptions {
  AnalyzerMode mode = AnalyzerMode::kOnline;
  std::size_t decoded_event_limit = 0;
};

}  // namespace ana

#endif
