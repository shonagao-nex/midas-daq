#include "PageConfig.h"

namespace ana {

bool IsSupportedPageLayout(const PageConfig& page) {
  return (page.rows == 3 && page.columns == 3) ||
         (page.rows == 4 && page.columns == 4) ||
         (page.rows == 4 && page.columns == 5);
}

}  // namespace ana
