#ifndef ANA_PAGE_CONFIG_LOADER_H
#define ANA_PAGE_CONFIG_LOADER_H

#include "PageConfig.h"

#include <vector>

class MVOdb;

namespace ana {

class PageConfigLoader {
 public:
  struct Result {
    std::vector<PageConfig> pages;
    bool odb_path_found = false;
    bool loaded_from_odb = false;
  };

  // Read-only loader. Missing /Analyzer/Pages uses in-memory defaults.
  Result Load(MVOdb* odb) const;

  // Explicit initialization only; existing /Analyzer/Pages is never changed.
  bool CreateDefaults(MVOdb* odb) const;

  static constexpr const char* kOdbPath = "/Analyzer/Pages";
};

}  // namespace ana

#endif
