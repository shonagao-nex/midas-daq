#ifndef ANA_EVENT_DIAGNOSTICS_H
#define ANA_EVENT_DIAGNOSTICS_H

#include "EventBuilder.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>

class TMEvent;

namespace ana {

// Per-run event diagnostics. EventBuilder remains responsible for pairing and
// decoder statistics; this class records raw-event details and prints the EOR
// summary from both sets of measurements.
class EventDiagnostics {
 public:
  explicit EventDiagnostics(bool debug_events = false)
      : debug_events_(debug_events) {}

  void Reset();
  bool BeginEvent(TMEvent& event);
  void RecordSpecialEvent(const TMEvent& event);
  void RecordSourceEvent(const TMEvent& event, RawEventSource source);
  void PrintRunSummary(int run_number, bool offline, std::int64_t tree_entries,
                       std::size_t decoded_events,
                       std::int64_t event_hist_entries,
                       const EventBuilder::Statistics& builder_stats) const;

  std::size_t EventIdCount(std::uint16_t event_id) const;
  std::size_t SerialGaps(RawEventSource source) const;
  static std::size_t RawCounterGaps(
      const EventBuilder::Statistics::RawCounters& counters);
  std::size_t DetailedPrinted() const { return detailed_printed_; }

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

  static std::size_t ElementSize(std::uint32_t type);
  static std::size_t CountGaps(const std::set<std::uint32_t>& serials);
  static void PrintSourceSummary(const char* name,
                                 const SourceStatistics& statistics);
  void PrintDetailedEvent(const TMEvent& event) const;

  bool debug_events_ = false;
  static constexpr std::size_t kDetailedLimit = 8;
  std::size_t detailed_printed_ = 0;
  std::size_t malformed_events_ = 0;
  std::map<std::uint16_t, std::size_t> event_ids_;
  SourceStatistics vme_;
  SourceStatistics easiroc_;
};

}  // namespace ana

#endif
