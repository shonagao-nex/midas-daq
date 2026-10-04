#ifndef ANA_EVENT_INSPECTOR_H
#define ANA_EVENT_INSPECTOR_H

#include "AnalyzerMode.h"
#include "EventBuilder.h"
#include "EventDiagnostics.h"
#include "HistogramConfigLoader.h"
#include "HistogramManager.h"
#include "PageConfigLoader.h"
#include "PageManager.h"
#include "OnlineHistogramState.h"
#include "OnlineHistogramPrescale.h"
#include "RootTreeWriter.h"
#include "manalyzer.h"

#include <chrono>
#include <memory>

namespace ana {

class EventInspector : public TARunObject {
 public:
  EventInspector(TARunInfo* runinfo, EventInspectorOptions options,
                 OnlineHistogramState* online_state = nullptr);

  void BeginRun(TARunInfo* runinfo) override;
  void EndRun(TARunInfo* runinfo) override;
  TAFlowEvent* Analyze(TARunInfo* runinfo, TMEvent* event, TAFlags* flags,
                       TAFlowEvent* flow) override;
  void AnalyzeSpecialEvent(TARunInfo* runinfo, TMEvent* event) override;

 private:
  void InspectEvent(TMEvent& event);
  void ConsumeDecodedEvent(const DecodedEvent& event);
  void PollOnlineControls(TARunInfo* runinfo);
  void PollPdfRequest(TARunInfo* runinfo);
  static RawEventSource Classify(const TMEvent& event);

  EventInspectorOptions options_;
  EventDiagnostics diagnostics_;
  std::size_t decoded_events_ = 0;
  bool decoded_limit_reached_ = false;
  bool monitor_pending_ = false;
  OnlineHistogramPrescale histogram_prescale_;
  std::unique_ptr<RootTreeWriter> tree_writer_;
  OnlineHistogramState* online_state_ = nullptr;  // Factory-owned; online only.
  HistogramConfigLoader histogram_config_loader_;
  HistogramManager histogram_manager_;
  PageConfigLoader page_config_loader_;
  PageManager page_manager_;
  HistogramManager& Histograms();
  PageManager& Pages();
  EventBuilder builder_;
  std::chrono::steady_clock::time_point next_online_poll_{};
};

}  // namespace ana

#endif
