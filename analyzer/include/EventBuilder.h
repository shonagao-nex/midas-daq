#ifndef ANA_EVENT_BUILDER_H
#define ANA_EVENT_BUILDER_H

#include "DecodedEvent.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>

class TMEvent;

namespace ana {

enum class RawEventSource { kUnknown, kVme, kEasiroc };

class EventBuilder {
 public:
  using Consumer = std::function<void(const DecodedEvent&)>;

  struct Statistics {
    struct RawCounters {
      std::set<std::int64_t> values;
    };

    std::size_t paired = 0;
    std::size_t vme_only = 0;
    std::size_t easiroc_only = 0;
    std::size_t counter_mismatches = 0;
    std::size_t duplicate_source = 0;
    std::size_t decoder_errors = 0;
    std::set<std::uint32_t> vme_only_counters;
    std::set<std::uint32_t> easiroc_only_counters;
    RawCounters v792;
    RawCounters v775;
    RawCounters v1190;
    RawCounters v1720;
    RawCounters nim_easiroc;
  };

  explicit EventBuilder(Consumer consumer = {});

  void AddEvent(TMEvent& event, RawEventSource source);
  void Finish();
  void DiscardPending();
  void Clear();

  const Statistics& GetStatistics() const { return statistics_; }
  std::size_t PendingCount() const { return pending_.size(); }

 private:
  struct PendingEvent {
    DecodedEvent decoded;
    bool has_vme = false;
    bool has_easiroc = false;
  };

  void DecodeVme(TMEvent& event, PendingEvent& pending);
  void DecodeEasiroc(TMEvent& event, PendingEvent& pending);
  void Emit(std::map<std::uint32_t, PendingEvent>::iterator position);

  Consumer consumer_;
  std::map<std::uint32_t, PendingEvent> pending_;
  Statistics statistics_;
};

}  // namespace ana

#endif
