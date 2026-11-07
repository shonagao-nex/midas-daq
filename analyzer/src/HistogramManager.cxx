#include "HistogramManager.h"

#include "TDirectory.h"
#include "TH1.h"
#include "TH1D.h"
#include "TGraph.h"
#include "TList.h"
#include "TMultiGraph.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

namespace ana {
namespace {

// Built-in ROOT blues, ordered by decreasing luminance. These color IDs
// also work when JSROOT requests an individual plot without canvas colors.
constexpr std::array<Color_t, 20> kWaveformColors{
    590, 851, 866, 868, 591, 592, 856, 852, 861, 865,
    867, 593, 594, 595, 855, 862, 597, 598, 854, 602};

void ClearTrace(TGraph& trace) {
  // Native ROOT reports a paint error for a zero-point graph. Keep an
  // invisible point below the ADC range in each unused monitoring slot.
  trace.Set(1);
  trace.SetPoint(0, 0, -1);
  trace.SetLineWidth(0);
  trace.SetMarkerSize(0);
}

std::unique_ptr<TMultiGraph> BookWaveform(const HistogramConfig& config) {
  const auto title = config.title + ";" + config.x_title + ";" + config.y_title;
  auto history = std::make_unique<TMultiGraph>(config.hist_name.c_str(),
                                               title.c_str());
  history->SetMinimum(0);
  history->SetMaximum(4096);
  // JSROOT monitoring can update existing curves but cannot add curve painters
  // when a multigraph grows. Start with all 20 slots already present.
  for (const auto color : kWaveformColors) {
    auto* trace = new TGraph();
    trace->SetLineColor(color);
    trace->SetMarkerStyle(20);
    ClearTrace(*trace);
    history->Add(trace, "L");
  }
  return history;
}

void UpdateWaveform(TMultiGraph& history, const HistogramConfig& config,
                    const std::vector<std::int32_t>& samples) {
  const auto first = static_cast<std::size_t>(config.min + 0.5);
  const auto last = std::min(samples.size(), first + config.bins);
  if (first >= last || std::none_of(samples.begin() + first, samples.begin() + last,
                                    [](auto adc) { return adc != kInvalidValue; }))
    return;

  auto* traces = history.GetListOfGraphs();
  // Move the oldest slot to the end so the newest curve is painted on top.
  auto* trace = static_cast<TGraph*>(traces->Remove(traces->First()));
  int points = 0;
  for (auto sample = first; sample < last; ++sample) {
    if (samples[sample] != kInvalidValue)
      trace->SetPoint(points++, static_cast<double>(sample), samples[sample]);
  }
  trace->Set(points);
  trace->SetLineWidth(2);
  trace->SetMarkerSize(0.6);
  history.Add(trace, points == 1 ? "P" : "L");
  std::size_t age_order = 0;
  for (auto* object : *traces) {
    auto* curve = static_cast<TGraph*>(object);
    const auto color = kWaveformColors[age_order++];
    curve->SetLineColor(color);
    curve->SetMarkerColor(color);
    if (curve != trace && curve->GetLineWidth() != 0) curve->SetLineWidth(1);
  }
}

}  // namespace

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

void HistogramManager::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& histogram : histograms_) {
    if (auto* history = dynamic_cast<TMultiGraph*>(histogram.object.get())) {
      for (auto* trace : *history->GetListOfGraphs())
        ClearTrace(*static_cast<TGraph*>(trace));
    } else {
      static_cast<TH1*>(histogram.object.get())->Reset();
    }
  }
}

bool HistogramManager::BookLocked() {
  bool all_valid = true;
  for (const auto& config : configs_) {
    if (!config.enabled) continue;

    ParsedExpression expression = config.type == "Waveform"
        ? resolver_.ParseWaveform(config.expression)
        : resolver_.Parse(config.expression);
    if (!expression.IsValid()) {
      std::fprintf(stderr,
                   "ERROR: histogram %s expression '%s' is invalid: %s\n",
                   config.hist_name.c_str(), config.expression.c_str(),
                   expression.error.c_str());
      all_valid = false;
      continue;
    }

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
    std::unique_ptr<TNamed> object;
    if (config.type == "Waveform") {
      object = BookWaveform(config);
      group_directory->Append(object.get());
    } else {
      const bool hit_counts = expression.kind == VariableKind::kHitChannels;
      const auto& channels = expression.hit_channels;
      auto histogram = std::make_unique<TH1D>(
          config.hist_name.c_str(), config.title.c_str(),
          hit_counts ? static_cast<int>(channels.size()) : config.bins,
          hit_counts ? -0.5 : config.min,
          hit_counts ? static_cast<double>(channels.size()) - 0.5 : config.max);
      histogram->GetXaxis()->SetTitle(config.x_title.c_str());
      histogram->GetYaxis()->SetTitle(config.y_title.c_str());
      if (!write_at_end_) {
        // Use only a few major intervals on the online plots, including 8x4 pages.
        if (!hit_counts) histogram->GetXaxis()->SetNdivisions(4);
        histogram->GetYaxis()->SetNdivisions(4);
      }
      if (hit_counts) {
        // Bin positions are categorical. Their mean and RMS have no meaning.
        histogram->SetStats(false);
        for (std::size_t i = 0; i < channels.size(); ++i)
          histogram->GetXaxis()->SetBinLabel(static_cast<int>(i + 1), channels[i].label.c_str());
      }
      histogram->SetDirectory(group_directory);
      object = std::move(histogram);
    }
    histograms_.push_back(
        {config, std::move(expression), std::move(object), group_directory});
  }
  return all_valid;
}

void HistogramManager::Fill(const DecodedEvent& event, bool fill_histograms,
                            bool fill_waveforms) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& histogram : histograms_) {
    if (histogram.config.type == "Waveform") {
      const auto channel = histogram.expression.channel;
      if (fill_waveforms && channel < event.v1720.fadc0.size())
        UpdateWaveform(*static_cast<TMultiGraph*>(histogram.object.get()),
                       histogram.config, event.v1720.fadc0[channel]);
      continue;
    }
    if (!fill_histograms) continue;
    auto* object = static_cast<TH1*>(histogram.object.get());
    if (histogram.expression.kind == VariableKind::kHitChannels) {
      const auto& channels = histogram.expression.hit_channels;
      for (std::size_t i = 0; i < channels.size(); ++i)
        if (HasPositiveHit(channels[i], event)) object->Fill(static_cast<double>(i));
      continue;
    }
    const ResolveResult result = resolver_.Resolve(histogram.expression, event);
    if (result.valid) object->Fill(result.value);
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
    if (histogram.config.hist_name != hist_name) continue;
    if (const auto* object = dynamic_cast<const TH1*>(histogram.object.get()))
      return static_cast<std::int64_t>(object->GetEntries());
  }
  return 0;
}

TNamed* HistogramManager::FindPlot(const std::string& path) const {
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

TH1* HistogramManager::FindHistogram(const std::string& path) const {
  return dynamic_cast<TH1*>(FindPlot(path));
}

std::vector<std::unique_ptr<TNamed>> HistogramManager::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::unique_ptr<TNamed>> snapshots;
  snapshots.reserve(histograms_.size());
  for (const auto& histogram : histograms_) {
    auto* clone = static_cast<TNamed*>(histogram.object->Clone());
    if (!clone) continue;
    if (auto* th1 = dynamic_cast<TH1*>(clone)) th1->SetDirectory(nullptr);
    snapshots.emplace_back(clone);
  }
  return snapshots;
}

std::size_t HistogramManager::ActiveCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return histograms_.size();
}

void HistogramManager::ClearHistogramsLocked(bool write) {
  for (auto& histogram : histograms_) {
    if (write) {
      TDirectory::TContext context(histogram.directory);
      histogram.object->Write(histogram.config.hist_name.c_str(), TObject::kOverwrite);
    }
    if (auto* th1 = dynamic_cast<TH1*>(histogram.object.get()))
      th1->SetDirectory(nullptr);
    else
      histogram.directory->Remove(histogram.object.get());
  }
  histograms_.clear();
}

}  // namespace ana
