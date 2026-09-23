#include "HistogramOdbInitializer.h"

#include "HistogramConfig.h"
#include "HistogramConfigLoader.h"

#include <algorithm>
#include <vector>

namespace ana {
namespace {

bool SameConfigs(std::vector<HistogramConfig> left,
                 std::vector<HistogramConfig> right) {
  const auto by_name = [](const HistogramConfig& a, const HistogramConfig& b) {
    return a.hist_name < b.hist_name;
  };
  std::sort(left.begin(), left.end(), by_name);
  std::sort(right.begin(), right.end(), by_name);
  return left == right;
}

}  // namespace

HistogramOdbInitializationResult InitializeHistogramOdb(MVOdb* odb) {
  HistogramOdbInitializationResult result;
  const auto defaults = DefaultHistogramConfigs();
  HistogramConfigLoader loader;

  if (!loader.CreateDefaults(odb)) {
    result.error = "histogram ODB defaults were not created";
    return result;
  }
  result.created = defaults.size();

  const auto loaded = loader.Load(odb);
  if (!loaded.odb_path_found || !loaded.loaded_from_odb) {
    result.error = "created histogram ODB tree could not be read back";
    return result;
  }
  result.loaded = loaded.configs.size();

  const auto validation = ValidateHistogramConfigs(loaded.configs);
  result.valid = validation.configs.size();
  if (validation.skipped != 0 || result.loaded != defaults.size() ||
      result.valid != defaults.size() ||
      !SameConfigs(validation.configs, defaults)) {
    result.error = "read-back histogram configuration does not match defaults";
    return result;
  }

  for (const auto& config : defaults)
    result.histogram_names.push_back(config.hist_name);
  result.okay = true;
  return result;
}

}  // namespace ana
