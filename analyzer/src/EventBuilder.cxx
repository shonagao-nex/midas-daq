#include "EventBuilder.h"

#include "EasirocDecoder.h"
#include "V1190Decoder.h"
#include "V1720Decoder.h"
#include "V775Decoder.h"
#include "V792Decoder.h"
#include "midasio.h"

#include <cstdio>
#include <utility>

namespace ana {

EventBuilder::EventBuilder(Consumer consumer) : consumer_(std::move(consumer)) {}

void EventBuilder::DecodeVme(TMEvent& event, PendingEvent& pending) {
  const auto decode = [&](const char* bank_name, const auto& decoder) {
    if (event.FindBank(bank_name) && !decoder.Decode(event, pending.decoded)) {
      ++statistics_.decoder_errors;
      std::fprintf(stderr,
                   "WARNING: decoder rejected bank %s at VME serial %u; "
                   "event retained\n",
                   bank_name, event.serial_number);
    }
  };
  decode("ADC0", V792Decoder{});
  decode("TDC1", V775Decoder{});
  decode("TDC0", V1190Decoder{});
  decode("FADC", V1720Decoder{});
}

void EventBuilder::DecodeEasiroc(TMEvent& event, PendingEvent& pending) {
  if (!EasirocDecoder{}.Decode(event, pending.decoded)) {
    ++statistics_.decoder_errors;
    std::fprintf(stderr,
                 "WARNING: EASIROC decoder rejected serial %u; event retained\n",
                 event.serial_number);
  }
}

void EventBuilder::AddEvent(TMEvent& event, RawEventSource source) {
  if (source == RawEventSource::kUnknown) return;
  const std::uint32_t key = event.serial_number;
  auto [position, inserted] = pending_.try_emplace(key);
  (void)inserted;
  PendingEvent& pending = position->second;
  pending.decoded.counters.event = key;

  if (source == RawEventSource::kVme) {
    if (pending.has_vme) {
      ++statistics_.duplicate_source;
      std::fprintf(stderr,
                   "WARNING: duplicate VME frontend counter %u; duplicate not "
                   "merged\n",
                   key);
      return;
    }
    pending.has_vme = true;
    pending.decoded.counters.vme = key;
    DecodeVme(event, pending);
  } else {
    if (pending.has_easiroc) {
      ++statistics_.duplicate_source;
      std::fprintf(stderr,
                   "WARNING: duplicate EASIROC frontend counter %u; duplicate "
                   "not merged\n",
                   key);
      return;
    }
    pending.has_easiroc = true;
    pending.decoded.counters.easiroc = key;
    DecodeEasiroc(event, pending);
  }

  if (pending.has_vme && pending.has_easiroc) Emit(position);
}

void EventBuilder::Emit(std::map<std::uint32_t, PendingEvent>::iterator position) {
  PendingEvent& pending = position->second;
  if (pending.has_vme && pending.has_easiroc)
    ++statistics_.paired;
  else if (pending.has_vme)
    ++statistics_.vme_only;
  else
    ++statistics_.easiroc_only;

  const auto record = [](Statistics::RawCounters& statistics,
                         std::int64_t value) {
    if (value != kInvalidCounter) statistics.values.insert(value);
  };
  record(statistics_.v792, pending.decoded.counters.v792);
  record(statistics_.v775, pending.decoded.counters.v775);
  record(statistics_.v1190, pending.decoded.counters.v1190);
  record(statistics_.v1720, pending.decoded.counters.v1720);
  record(statistics_.nim_easiroc, pending.decoded.counters.nim_easiroc);

  if (consumer_) consumer_(pending.decoded);
  pending_.erase(position);
}

void EventBuilder::Finish() {
  while (!pending_.empty()) {
    auto position = pending_.begin();
    const auto counter = position->first;
    const PendingEvent& pending = position->second;
    if (!pending.has_vme || !pending.has_easiroc) {
      if (pending.has_vme) {
        std::fprintf(stderr,
                     "WARNING: frontend counter mismatch: VME=%u "
                     "EASIROC=missing (event retained)\n",
                     counter);
      } else {
        std::fprintf(stderr,
                     "WARNING: frontend counter mismatch: VME=missing "
                     "EASIROC=%u (event retained)\n",
                     counter);
      }
    }
    Emit(position);
  }
}

void EventBuilder::Clear() {
  pending_.clear();
  statistics_ = Statistics{};
}

}  // namespace ana
