#ifndef ANA_HISTOGRAM_MANAGER_H
#define ANA_HISTOGRAM_MANAGER_H

#include "DecodedEvent.h"
#include "ExpressionResolver.h"
#include "HistogramConfig.h"

#include <cstdint>
#include <string>
#include <vector>

class TDirectory;
class TFile;
class TH1;

namespace ana {

class HistogramManager {
 public:
  HistogramManager() = default;
  explicit HistogramManager(std::vector<HistogramConfig> configs);

  void SetConfigs(std::vector<HistogramConfig> configs);
  bool BeginRun(TFile* output_file);
  void Fill(const DecodedEvent& event);
  void EndRun();

  std::int64_t Entries(const std::string& hist_name) const;

 private:
  struct Histogram {
    HistogramConfig config;
    ParsedExpression expression;
    TH1* object = nullptr;  // Owned by directory_ / its TFile.
  };

  std::vector<HistogramConfig> configs_;
  std::vector<Histogram> histograms_;
  ExpressionResolver resolver_;
  TDirectory* directory_ = nullptr;
};

}  // namespace ana

#endif
