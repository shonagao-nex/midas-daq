#ifndef ANA_PAGE_CONFIG_H
#define ANA_PAGE_CONFIG_H

#include <string>
#include <vector>

namespace ana {

struct PageConfig {
  std::string name;
  int rows = 0;
  int columns = 0;
  std::vector<std::string> pads;

  bool operator==(const PageConfig& other) const {
    return name == other.name && rows == other.rows &&
           columns == other.columns && pads == other.pads;
  }
};

bool IsSupportedPageLayout(const PageConfig& page);
std::vector<PageConfig> DefaultPageConfigs();

}  // namespace ana

#endif
