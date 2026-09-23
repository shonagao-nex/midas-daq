#include "HistogramManager.h"

#include "TDirectory.h"
#include "TH1.h"
#include "TH1D.h"

#include <cstdio>
#include <utility>

namespace ana {

HistogramManager::HistogramManager() = default;

HistogramManager::HistogramManager(std::vector<HistogramConfig> configs) {
  SetConfigs(std::move(configs));
}

HistogramManager::~HistogramManager() { EndRun(); }

void HistogramManager::SetConfigs(std::vector<HistogramConfig> configs) {
  std::lock_guard<std::mutex> lock(mutex_);
  requested_configs_ = configs;
  auto validation = ValidateHistogramConfigs(configs);
  configs_ = std::move(validation.configs);
}

bool HistogramManager::BeginRun(TDirectory* parent_directory,
                                bool write_at_end) {
  std::lock_guard<std::mutex> lock(mutex_);
  ClearHistogramsLocked(false);
  if (!parent_directory) {
    std::fprintf(stderr,
                 "ERROR: HistogramManager cannot use a null ROOT directory\n");
    return false;
  }

  parent_directory->cd();
  directory_ = parent_directory->GetDirectory("Histograms");
  if (!directory_) directory_ = parent_directory->mkdir("Histograms");
  if (!directory_) {
    std::fprintf(stderr,
                 "ERROR: HistogramManager cannot create Histograms directory\n");
    return false;
  }
  write_at_end_ = write_at_end;
  return BookLocked();
}

bool HistogramManager::ApplyConfigs(std::vector<HistogramConfig> configs) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (configs == requested_configs_) return false;

  requested_configs_ = configs;
  auto validation = ValidateHistogramConfigs(configs);
  configs_ = std::move(validation.configs);
  if (!directory_) return true;

  ClearHistogramsLocked(false);
  BookLocked();
  return true;
}

bool HistogramManager::ConfigsMatch(
    const std::vector<HistogramConfig>& configs) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return configs == requested_configs_;
}

bool HistogramManager::BookLocked() {
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

    auto object = std::make_unique<TH1D>(
        config.hist_name.c_str(), config.title.c_str(), config.bins,
        config.min, config.max);
    object->SetTitle(config.title.c_str());
    object->GetXaxis()->SetTitle(config.x_title.c_str());
    object->GetYaxis()->SetTitle(config.y_title.c_str());

    TDirectory* group_directory = directory_;
    if (!config.group.empty()) {
      group_directory = directory_->GetDirectory(config.group.c_str());
      if (!group_directory)
        group_directory = directory_->mkdir(config.group.c_str());
    }
    if (!group_directory) {
      std::fprintf(stderr,
                   "ERROR: cannot create ROOT histogram group directory %s\n",
                   config.group.c_str());
      all_valid = false;
      continue;
    }
    object->SetDirectory(group_directory);
    histograms_.push_back(
        {config, std::move(expression), std::move(object)});
  }
  return all_valid;
}

void HistogramManager::Fill(const DecodedEvent& event) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& histogram : histograms_) {
    const ResolveResult result = resolver_.Resolve(histogram.expression, event);
    if (result.valid) histogram.object->Fill(result.value);
  }
}

void HistogramManager::EndRun() {
  std::lock_guard<std::mutex> lock(mutex_);
  ClearHistogramsLocked(write_at_end_);
  directory_ = nullptr;
  write_at_end_ = false;
}

std::int64_t HistogramManager::Entries(const std::string& hist_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& histogram : histograms_) {
    if (histogram.config.hist_name == hist_name)
      return static_cast<std::int64_t>(histogram.object->GetEntries());
  }
  return 0;
}

TH1* HistogramManager::FindHistogram(const std::string& path) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto separator = path.find('/');
  const std::string group = separator == std::string::npos
                                ? std::string{}
                                : path.substr(0, separator);
  const std::string name = separator == std::string::npos
                               ? path
                               : path.substr(separator + 1);
  for (const auto& histogram : histograms_) {
    if (histogram.config.hist_name != name) continue;
    if (histogram.config.group == group) return histogram.object.get();
  }
  return nullptr;
}

std::vector<std::unique_ptr<TH1>> HistogramManager::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::unique_ptr<TH1>> snapshots;
  snapshots.reserve(histograms_.size());
  for (const auto& histogram : histograms_) {
    auto* clone = dynamic_cast<TH1*>(histogram.object->Clone());
    if (!clone) continue;
    clone->SetDirectory(nullptr);
    snapshots.emplace_back(clone);
  }
  return snapshots;
}

std::size_t HistogramManager::ActiveCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return histograms_.size();
}

void HistogramManager::ClearHistogramsLocked(bool write) {
  if (write && directory_) {
    for (const auto& histogram : histograms_) {
      if (histogram.object->GetDirectory())
        histogram.object->GetDirectory()->cd();
      histogram.object->Write(histogram.config.hist_name.c_str(),
                              TObject::kOverwrite);
    }
  }
  for (auto& histogram : histograms_)
    histogram.object->SetDirectory(nullptr);
  histograms_.clear();
}

}  // namespace ana
