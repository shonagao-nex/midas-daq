#include "PageManager.h"

#include "HistogramManager.h"

#include "TCanvas.h"
#include "TDirectory.h"
#include "TH1.h"
#include "TPad.h"

#include <cstdio>
#include <utility>

namespace ana {

PageManager::~PageManager() { Clear(); }

bool PageManager::BeginRun(TDirectory* parent,
                           const HistogramManager* histograms) {
  Clear();
  if (!parent) return false;
  parent->cd();
  pages_directory_ = parent->GetDirectory("Pages");
  if (!pages_directory_) pages_directory_ = parent->mkdir("Pages");
  if (!pages_directory_) return false;
  return Rebuild(histograms);
}

bool PageManager::ApplyConfigs(std::vector<PageConfig> pages,
                               const HistogramManager* histograms) {
  if (pages == requested_pages_) return false;
  requested_pages_ = std::move(pages);
  if (!pages_directory_) return true;
  return Rebuild(histograms);
}

void PageManager::ClearCanvases() {
  if (pages_directory_) {
    for (auto* canvas : canvases_) pages_directory_->Remove(canvas);
  }
  for (auto* canvas : canvases_) delete canvas;
  canvases_.clear();
}

void PageManager::Clear() {
  ClearCanvases();
  requested_pages_.clear();
  pages_directory_ = nullptr;
}

bool PageManager::Rebuild(const HistogramManager* histograms) {
  ClearCanvases();
  bool okay = true;
  for (const auto& config : requested_pages_)
    okay = BuildCanvas(config, histograms) && okay;
  return okay;
}

bool PageManager::BuildCanvas(const PageConfig& config,
                              const HistogramManager* histograms) {
  if (!pages_directory_ || !IsSupportedPageLayout(config)) return false;
  pages_directory_->cd();
  auto* canvas = new TCanvas(config.name.c_str(), config.name.c_str(), 1200,
                             900);
  canvas->Divide(config.columns, config.rows);
  const auto pad_count = static_cast<std::size_t>(config.rows * config.columns);
  for (std::size_t index = 0; index < pad_count; ++index) {
    TPad* pad = dynamic_cast<TPad*>(canvas->cd(static_cast<int>(index + 1)));
    if (!pad) continue;
    pad->Clear();
    if (index >= config.pads.size() || config.pads[index].empty()) continue;
    TH1* histogram = histograms
                         ? histograms->FindHistogram(config.pads[index])
                         : nullptr;
    if (histogram) histogram->Draw();
  }
  canvas->Modified();
  canvas->Update();
  pages_directory_->Append(canvas, true);
  canvases_.push_back(canvas);
  return true;
}

}  // namespace ana
