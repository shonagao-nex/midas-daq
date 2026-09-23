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

  // Read-only loader. Missing /Analyzer/Pages is a normal empty result.
  Result Load(MVOdb* odb) const;

  static constexpr const char* kOdbPath = "/Analyzer/Pages";
};

}  // namespace ana

#endif
