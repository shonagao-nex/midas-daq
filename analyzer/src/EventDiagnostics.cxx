#include "EventDiagnostics.h"

#include "midasio.h"

#include <cstdio>

namespace ana {

void EventDiagnostics::Reset() {
  pending_warning_active_ = false;
  detailed_printed_ = 0;
  malformed_events_ = 0;
  event_ids_.clear();
  vme_ = SourceStatistics{};
  easiroc_ = SourceStatistics{};
}

std::string EventDiagnostics::PendingStatusMessage(
    const EventBuilder& builder, bool online_both_source) {
  if (!online_both_source) return {};
  const std::size_t count = builder.PendingCount();
  const bool warning = count >= 100;
  if (warning == pending_warning_active_) return {};
  pending_warning_active_ = warning;
  std::string message = warning
                            ? "WARNING: EventBuilder pending events accumulating: count="
                            : "INFO: EventBuilder pending events recovered: count=";
  message += std::to_string(count);
  if (count != 0) {
    message += ", oldest_serial=" +
               std::to_string(*builder.OldestPendingSerial());
    message += ", newest_serial=" +
               std::to_string(*builder.NewestPendingSerial());
  }
  return message;
}

std::size_t EventDiagnostics::ElementSize(std::uint32_t type) {
  switch (type) {
    case TID_WORD:
    case TID_SHORT:
      return 2;
    case TID_DWORD:
    case TID_INT:
    case TID_BOOL:
    case TID_FLOAT:
    case TID_BITFIELD:
      return 4;
    case TID_DOUBLE:
    case TID_INT64:
    case TID_UINT64:
      return 8;
    default:
      return 1;
  }
}

bool EventDiagnostics::BeginEvent(TMEvent& event) {
  ++event_ids_[event.event_id];
  if (event.error) {
    ++malformed_events_;
    std::fprintf(stderr,
                 "WARNING: malformed MIDAS event id=0x%04x serial=%u\n",
                 event.event_id, event.serial_number);
    return false;
  }
  event.FindAllBanks();
  if (debug_events_ && detailed_printed_ < kDetailedLimit) {
    PrintDetailedEvent(event);
    ++detailed_printed_;
  }
  return true;
}

void EventDiagnostics::RecordSpecialEvent(const TMEvent& event) {
  ++event_ids_[event.event_id];
  if (debug_events_ && detailed_printed_ < kDetailedLimit) {
    std::printf("Special event: id=0x%04x serial=%u timestamp=%u data_size=%u\n",
                event.event_id, event.serial_number, event.time_stamp,
                event.data_size);
    ++detailed_printed_;
  }
}

void EventDiagnostics::RecordSourceEvent(const TMEvent& event,
                                         RawEventSource source) {
  SourceStatistics* statistics = source == RawEventSource::kVme
                                     ? &vme_
                                     : source == RawEventSource::kEasiroc
                                           ? &easiroc_
                                           : nullptr;
  if (!statistics) return;
  ++statistics->events;
  statistics->serials.insert(event.serial_number);
  for (const auto& bank : event.banks) {
    BankStatistics& bank_statistics = statistics->banks[bank.name];
    ++bank_statistics.events;
    bank_statistics.types.insert(bank.type);
    const std::size_t element_size = ElementSize(bank.type);
    const std::size_t length = bank.data_size / element_size;
    ++bank_statistics.lengths[length];
    if (bank.data_size % element_size != 0) {
      std::fprintf(stderr,
                   "WARNING: bank %s byte size %u is not divisible by type "
                   "size %zu\n",
                   bank.name.c_str(), bank.data_size, element_size);
    }
  }
}

void EventDiagnostics::PrintDetailedEvent(const TMEvent& event) const {
  std::printf("Event: id=0x%04x serial=%u timestamp=%u data_size=%u\n",
              event.event_id, event.serial_number, event.time_stamp,
              event.data_size);
  for (const auto& bank : event.banks) {
    const std::size_t length = bank.data_size / ElementSize(bank.type);
    std::printf("  bank %-4s type=%u length=%zu bytes=%u\n",
                bank.name.c_str(), bank.type, length, bank.data_size);
  }
}

std::size_t EventDiagnostics::EventIdCount(std::uint16_t event_id) const {
  const auto found = event_ids_.find(event_id);
  return found == event_ids_.end() ? 0 : found->second;
}

std::size_t EventDiagnostics::CountGaps(
    const std::set<std::uint32_t>& serials) {
  if (serials.empty()) return 0;
  const std::size_t span =
      static_cast<std::size_t>(*serials.rbegin() - *serials.begin()) + 1;
  return span - serials.size();
}

std::size_t EventDiagnostics::SerialGaps(RawEventSource source) const {
  if (source == RawEventSource::kVme) return CountGaps(vme_.serials);
  if (source == RawEventSource::kEasiroc) return CountGaps(easiroc_.serials);
  return 0;
}

std::size_t EventDiagnostics::RawCounterGaps(
    const EventBuilder::Statistics::RawCounters& counters) {
  if (counters.values.empty()) return 0;
  const auto span = static_cast<std::size_t>(
                        *counters.values.rbegin() - *counters.values.begin()) +
                    1;
  return span - counters.values.size();
}

void EventDiagnostics::PrintSourceSummary(
    const char* name, const SourceStatistics& statistics) {
  std::printf("\n%s\n", name);
  std::printf("  events            : %zu\n", statistics.events);
  if (statistics.serials.empty()) {
    std::printf("  serial first/last : n/a\n  serial gaps       : 0\n");
  } else {
    const std::uint32_t first = *statistics.serials.begin();
    const std::uint32_t last = *statistics.serials.rbegin();
    std::printf("  serial first/last : %u / %u\n", first, last);
    std::printf("  serial gaps       : %zu\n", CountGaps(statistics.serials));
  }

  for (const auto& [bank_name, bank] : statistics.banks) {
    std::printf("\n  %s\n", bank_name.c_str());
    std::printf("    events          : %zu\n", bank.events);
    if (bank.lengths.size() == 1) {
      std::printf("    length          : %zu\n", bank.lengths.begin()->first);
    } else if (!bank.lengths.empty()) {
      std::printf("    length min/max  : %zu / %zu\n",
                  bank.lengths.begin()->first, bank.lengths.rbegin()->first);
      std::printf("    length counts   :");
      for (const auto& [length, count] : bank.lengths)
        std::printf(" %zu:%zu", length, count);
      std::printf("\n");
    }
    std::printf("    type            :");
    for (const auto type : bank.types) std::printf(" %u", type);
    std::printf("\n");
  }
}

void EventDiagnostics::PrintRunSummary(
    int run_number, bool offline, std::int64_t tree_entries,
    std::size_t decoded_events, std::int64_t event_hist_entries,
    const EventBuilder::Statistics& stats) const {
  std::printf("\nRun %d summary\n", run_number);
  if (offline) {
    std::printf("ROOT Events entries : %lld\n",
                static_cast<long long>(tree_entries));
  } else {
    std::printf("Decoded events      : %zu\n", decoded_events);
  }
  PrintSourceSummary("EASIROC", easiroc_);
  PrintSourceSummary("VME", vme_);

  std::printf("\nEvent IDs\n");
  for (const auto& [event_id, count] : event_ids_)
    std::printf("  0x%04x : %zu\n", event_id, count);

  std::printf("\nEventBuilder\n");
  std::printf("  paired            : %zu\n", stats.paired);
  std::printf("  VME only          : %zu\n", stats.vme_only);
  std::printf("  EASIROC only      : %zu\n", stats.easiroc_only);
  std::printf("  duplicate source  : %zu\n", stats.duplicate_source);
  std::printf("  decoder errors    : %zu\n", stats.decoder_errors);
  std::printf("  malformed events  : %zu\n", malformed_events_);

  const auto print_ranges = [](const char* name,
                               const std::set<std::uint32_t>& counters) {
    std::printf("  %-17s:", name);
    if (counters.empty()) {
      std::printf(" none\n");
      return;
    }
    auto position = counters.begin();
    std::uint32_t first = *position;
    std::uint32_t last = first;
    for (++position; position != counters.end(); ++position) {
      if (*position == last + 1) {
        last = *position;
        continue;
      }
      if (first == last)
        std::printf(" %u", first);
      else
        std::printf(" %u-%u", first, last);
      first = last = *position;
    }
    if (first == last)
      std::printf(" %u\n", first);
    else
      std::printf(" %u-%u\n", first, last);
  };
  std::printf("\nUnpaired frontend events\n");
  std::printf("  VME only          : %zu\n", stats.vme_only);
  print_ranges("range", stats.vme_only_counters);
  std::printf("  EASIROC only      : %zu\n", stats.easiroc_only);
  print_ranges("range", stats.easiroc_only_counters);

  std::printf("\nHistograms (entries)\n");
  std::printf("  h_event                : %lld\n",
              static_cast<long long>(event_hist_entries));

  const auto print_raw_counter = [](const char* name,
                                    const EventBuilder::Statistics::RawCounters& counter) {
    std::printf("  %-17s:", name);
    if (counter.values.empty()) {
      std::printf(" unavailable\n");
      return;
    }
    const auto first = *counter.values.begin();
    const auto last = *counter.values.rbegin();
    std::printf(" %lld / %lld (events=%zu gaps=%zu)\n",
                static_cast<long long>(first), static_cast<long long>(last),
                counter.values.size(), RawCounterGaps(counter));
  };
  std::printf("\nHardware raw counters (first / last)\n");
  print_raw_counter("V792", stats.v792);
  print_raw_counter("V775", stats.v775);
  print_raw_counter("V1190", stats.v1190);
  print_raw_counter("V1720", stats.v1720);
}

}  // namespace ana
