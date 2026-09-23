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
    bool odb_path_found = false;
    bool loaded_from_odb = false;
  };

  // Read-only with respect to ODB. A missing tree falls back to in-memory
  // defaults and is never created as a side effect of loading.
  Result Load(MVOdb* odb) const;

  // Reserved for a future explicit administrative initialization action.
  // Normal online startup and live reload do not call this method.
  bool CreateDefaults(MVOdb* odb) const;

  static constexpr const char* kOdbPath = "/Analyzer/Histograms";
};

}  // namespace ana

#endif
