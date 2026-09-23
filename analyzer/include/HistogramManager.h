#ifndef ANA_HISTOGRAM_MANAGER_H
#define ANA_HISTOGRAM_MANAGER_H

#include "DecodedEvent.h"
#include "ExpressionResolver.h"
#include "HistogramConfig.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class TDirectory;
class TFile;
class TH1;

namespace ana {

class HistogramManager {
 public:
  HistogramManager();
  explicit HistogramManager(std::vector<HistogramConfig> configs);
  ~HistogramManager();

  void SetConfigs(std::vector<HistogramConfig> configs);
  bool BeginRun(TDirectory* parent_directory, bool write_at_end);
  bool ApplyConfigs(std::vector<HistogramConfig> configs);
  void Fill(const DecodedEvent& event);
  void EndRun();

  std::int64_t Entries(const std::string& hist_name) const;
  // Returns the booked histogram object without cloning it. The caller must
  // not delete or modify the returned object.
  TH1* FindHistogram(const std::string& path) const;
  std::vector<std::unique_ptr<TH1>> Snapshot() const;
  std::size_t ActiveCount() const;

 private:
  struct Histogram {
    HistogramConfig config;
    ParsedExpression expression;
    std::unique_ptr<TH1> object;
  };

  bool BookLocked();
  void ClearHistogramsLocked(bool write);

  mutable std::mutex mutex_;
  std::vector<HistogramConfig> requested_configs_;
  std::vector<HistogramConfig> configs_;
  std::vector<Histogram> histograms_;
  ExpressionResolver resolver_;
  TDirectory* directory_ = nullptr;
  bool write_at_end_ = false;
};

}  // namespace ana

#endif
