#ifndef ANA_HISTOGRAM_ENABLE_CONTROL_H
#define ANA_HISTOGRAM_ENABLE_CONTROL_H

#include <cstddef>
#include <string>
#include <vector>

class MVOdb;

namespace ana {

struct HistogramEnableGroup {
  std::string name;
  std::vector<bool> enabled;
};

class HistogramEnableControl {
 public:
  static const std::vector<std::string>& GroupNames();
  static std::size_t ChannelCount(const std::string& group);
  static bool Read(MVOdb* odb, const std::string& group,
                   HistogramEnableGroup* result, std::string* error);
  // Explicit UI action only. Updates exactly one existing Enabled[] element.
  static bool Set(MVOdb* odb, const std::string& group, std::size_t channel,
                  bool enabled, std::string* error);
};

}  // namespace ana

#endif
