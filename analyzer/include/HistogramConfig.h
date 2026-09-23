#ifndef ANA_HISTOGRAM_CONFIG_H
#define ANA_HISTOGRAM_CONFIG_H

#include <cstddef>
#include <string>
#include <vector>

namespace ana {

struct HistogramConfig {
  std::string hist_name;
  std::string type;
  std::string expression;
  int bins = 0;
  double min = 0.0;
  double max = 0.0;
  std::string cut;
  bool enabled = true;
  std::string title;
  std::string x_title;
  std::string y_title;
  std::string group;
  std::string slot;
};

bool operator==(const HistogramConfig& left, const HistogramConfig& right);
bool operator!=(const HistogramConfig& left, const HistogramConfig& right);

struct HistogramConfigValidationResult {
  std::vector<HistogramConfig> configs;
  std::size_t skipped = 0;
};

std::vector<HistogramConfig> DefaultHistogramConfigs();

HistogramConfigValidationResult ValidateHistogramConfigs(
    const std::vector<HistogramConfig>& configs);

}  // namespace ana

#endif
