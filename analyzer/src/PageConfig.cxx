#include "PageConfig.h"

#include <cstdio>
#include <utility>

namespace ana {

bool IsSupportedPageLayout(const PageConfig& page) {
  return (page.rows == 3 && page.columns == 3) ||
         (page.rows == 4 && page.columns == 4) ||
         (page.rows == 4 && page.columns == 5) ||
         (page.rows == 4 && page.columns == 8);
}

std::vector<PageConfig> DefaultPageConfigs() {
  std::vector<PageConfig> pages;
  const auto add = [&pages](const char* name, const char* group,
                            const char* histogram_prefix) {
    PageConfig page;
    page.name = name;
    page.rows = 4;
    page.columns = 8;
    for (int channel = 0; channel < 32; ++channel) {
      char histogram[64];
      std::snprintf(histogram, sizeof(histogram), "%s/h_%s_ch%02d", group,
                    histogram_prefix, channel);
      page.pads.emplace_back(histogram);
    }
    pages.push_back(std::move(page));
  };
  add("QDC0_00_31", "QDC0", "qdc0");
  add("TDC0_00_31", "TDC0", "tdc0");
  add("V1190_TLE_00_31", "TLE0", "tle0");
  add("V1190_TTR_00_31", "TTR0", "ttr0");
  return pages;
}

}  // namespace ana
