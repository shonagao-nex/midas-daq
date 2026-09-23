#ifndef ANA_HISTOGRAM_CONFIG_LOADER_H
#define ANA_HISTOGRAM_CONFIG_LOADER_H

#include "HistogramConfig.h"

#include <vector>

class MVOdb;

namespace ana {

class HistogramConfigLoader {
 public:
  struct Result {
    std::vector<HistogramConfig> configs;
    bool loaded_from_odb = false;
    bool created_defaults = false;
  };

  Result Load(MVOdb* odb, bool create_defaults_if_missing) const;

  static constexpr const char* kOdbPath = "/Analyzer/Histograms";

 private:
  bool CreateDefaults(MVOdb* odb) const;
};

}  // namespace ana

#endif
