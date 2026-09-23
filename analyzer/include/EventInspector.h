#ifndef ANA_EVENT_INSPECTOR_H
#define ANA_EVENT_INSPECTOR_H

#include "EventBuilder.h"
#include "RootTreeWriter.h"
#include "manalyzer.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace ana {

class EventInspector : public TARunObject {
 public:
  explicit EventInspector(TARunInfo* runinfo);

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
  static RawEventSource Classify(const TMEvent& event);
  static std::size_t ElementSize(std::uint32_t type);

  std::size_t detailed_limit_ = 8;
  std::size_t detailed_printed_ = 0;
  std::map<std::uint16_t, std::size_t> event_ids_;
  SourceStatistics vme_;
  SourceStatistics easiroc_;
  RootTreeWriter tree_writer_;
  EventBuilder builder_;
};

}  // namespace ana

#endif
