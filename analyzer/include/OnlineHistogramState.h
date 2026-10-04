#ifndef ANA_ONLINE_HISTOGRAM_STATE_H
#define ANA_ONLINE_HISTOGRAM_STATE_H

#include "HistogramManager.h"
#include "HistogramPdfWriter.h"
#include "PageManager.h"

#include <string>
#include <vector>

class TDirectory;
class MVOdb;

namespace ana {

// Owned by the analyzer factory, not by a manalyzer run object. The latter is
// deleted at EOR, while ROOT Web must retain the last run's objects.
class OnlineHistogramState {
 public:
  bool Initialize(TDirectory* parent, std::vector<HistogramConfig> histograms,
                  std::vector<PageConfig> pages);
  bool BeginRun(TDirectory* parent, std::vector<HistogramConfig> histograms,
                std::vector<PageConfig> pages, int run_number);
  void EndRun();  // Stop filling by ending the run object; retain ROOT objects.
  void Clear();   // Process shutdown only.
  bool ExportPdf(const std::string& output_path,
                 std::string* error = nullptr) const;
  void PollPdfRequest(MVOdb* odb);

  HistogramManager histograms;
  PageManager pages;

 private:
  HistogramPdfWriter pdf_writer_;
  int last_run_number_ = 0;
  bool has_run_ = false;
  bool initialized_ = false;
};

}  // namespace ana

#endif
