#ifndef ANA_EVENT_INSPECTOR_H
#define ANA_EVENT_INSPECTOR_H

#include "AnalyzerMode.h"
#include "EventBuilder.h"
#include "HistogramConfigLoader.h"
#include "HistogramManager.h"
#include "HistogramPdfWriter.h"
#include "PageConfigLoader.h"
#include "PageManager.h"
#include "RootTreeWriter.h"
#include "manalyzer.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <map>
#include <set>
#include <string>

namespace ana {

class EventInspector : public TARunObject {
 public:
  EventInspector(TARunInfo* runinfo, EventInspectorOptions options);

  void BeginRun(TARunInfo* runinfo) override;
  void EndRun(TARunInfo* runinfo) override;
  TAFlowEvent* Analyze(TARunInfo* runinfo, TMEvent* event, TAFlags* flags,
                       TAFlowEvent* flow) override;
  void AnalyzeSpecialEvent(TARunInfo* runinfo, TMEvent* event) override;

 private:
  struct BankStatistics {
    std::size_t events = 0;
    std::set<std::uint32_t> types;
    std::map<std::size_t, std::size_t> lengths;
  };

  struct SourceStatistics {
    std::size_t events = 0;
    std::set<std::uint32_t> serials;
    std::map<std::string, BankStatistics> banks;
  };

  void InspectEvent(TMEvent& event);
  void PrintDetailedEvent(TMEvent& event) const;
  void RecordSourceEvent(TMEvent& event, SourceStatistics& statistics);
  void PrintSourceSummary(const char* name,
                          const SourceStatistics& statistics) const;
  void ConsumeDecodedEvent(const DecodedEvent& event);
  void PollOnlineControls(TARunInfo* runinfo);
  void PollPdfRequest(TARunInfo* runinfo);
  static RawEventSource Classify(const TMEvent& event);
  static std::size_t ElementSize(std::uint32_t type);

  EventInspectorOptions options_;
  std::size_t detailed_limit_ = 8;
  std::size_t detailed_printed_ = 0;
  std::size_t decoded_events_ = 0;
  std::size_t malformed_events_ = 0;
  bool decoded_limit_reached_ = false;
  std::map<std::uint16_t, std::size_t> event_ids_;
  SourceStatistics vme_;
  SourceStatistics easiroc_;
  std::unique_ptr<RootTreeWriter> tree_writer_;
  HistogramConfigLoader histogram_config_loader_;
  HistogramManager histogram_manager_;
  HistogramPdfWriter histogram_pdf_writer_;
  PageConfigLoader page_config_loader_;
  PageManager page_manager_;
  EventBuilder builder_;
  std::chrono::steady_clock::time_point next_online_poll_{};
};

}  // namespace ana

#endif
