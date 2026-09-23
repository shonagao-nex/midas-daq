#ifndef ANA_PAGE_MANAGER_H
#define ANA_PAGE_MANAGER_H

#include "PageConfig.h"

#include <cstddef>
#include <vector>

class TCanvas;
class TDirectory;

namespace ana {

class HistogramManager;

class PageManager {
 public:
  ~PageManager();

  bool BeginRun(TDirectory* parent, const HistogramManager* histograms);
  bool ApplyConfigs(std::vector<PageConfig> pages,
                    const HistogramManager* histograms);
  void ClearCanvases();
  void Clear();
  bool Rebuild(const HistogramManager* histograms);
  std::size_t ActiveCount() const { return canvases_.size(); }

 private:
  bool BuildCanvas(const PageConfig& config,
                   const HistogramManager* histograms);

  TDirectory* pages_directory_ = nullptr;
  std::vector<PageConfig> requested_pages_;
  std::vector<TCanvas*> canvases_;
};

}  // namespace ana

#endif
