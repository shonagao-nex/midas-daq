#include "HistogramManager.h"

#include "TDirectory.h"
#include "TFile.h"
#include "TH1D.h"

#include <cstdio>
#include <utility>

namespace ana {

HistogramManager::HistogramManager(std::vector<HistogramConfig> configs) {
  SetConfigs(std::move(configs));
}

void HistogramManager::SetConfigs(std::vector<HistogramConfig> configs) {
  EndRun();
  auto validation = ValidateHistogramConfigs(configs);
  configs_ = std::move(validation.configs);
}

bool HistogramManager::BeginRun(TFile* output_file) {
  EndRun();
  if (!output_file || !output_file->IsOpen() || output_file->IsZombie()) {
    std::fprintf(stderr,
                 "ERROR: HistogramManager cannot use an invalid ROOT file\n");
    return false;
  }

  output_file->cd();
  directory_ = output_file->GetDirectory("Histograms");
  if (!directory_) directory_ = output_file->mkdir("Histograms");
  if (!directory_) {
    std::fprintf(stderr,
                 "ERROR: HistogramManager cannot create Histograms directory\n");
    return false;
  }

  bool all_valid = true;
  for (const auto& config : configs_) {
    if (!config.enabled) continue;

    ParsedExpression expression = resolver_.Parse(config.expression);
    if (!expression.IsValid()) {
      std::fprintf(stderr,
                   "ERROR: histogram %s expression '%s' is invalid: %s\n",
                   config.hist_name.c_str(), config.expression.c_str(),
                   expression.error.c_str());
      all_valid = false;
      continue;
    }

    directory_->cd();
    auto* object = new TH1D(config.hist_name.c_str(), config.hist_name.c_str(),
                            config.bins, config.min, config.max);
    object->SetDirectory(directory_);
    histograms_.push_back({config, std::move(expression), object});
  }
  output_file->cd();
  return all_valid;
}

void HistogramManager::Fill(const DecodedEvent& event) {
  for (auto& histogram : histograms_) {
    const ResolveResult result = resolver_.Resolve(histogram.expression, event);
    if (result.valid) histogram.object->Fill(result.value);
  }
}

void HistogramManager::EndRun() {
  histograms_.clear();
  directory_ = nullptr;
}

std::int64_t HistogramManager::Entries(const std::string& hist_name) const {
  for (const auto& histogram : histograms_) {
    if (histogram.config.hist_name == hist_name)
      return static_cast<std::int64_t>(histogram.object->GetEntries());
  }
  return 0;
}

}  // namespace ana
