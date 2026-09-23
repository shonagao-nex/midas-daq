#include "EventInspector.h"

#include "midasio.h"
#include "mvodb.h"

#include "TDirectory.h"
#include "TFile.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>

namespace ana {

EventInspector::EventInspector(TARunInfo* runinfo, EventInspectorOptions options)
    : TARunObject(runinfo),
      options_(options),
      builder_([this](const DecodedEvent& event) { ConsumeDecodedEvent(event); }) {
  fModuleName = "EventInspector";
  if (options_.mode == AnalyzerMode::kOffline) {
    tree_writer_ = std::make_unique<RootTreeWriter>();
  } else if (runinfo && runinfo->fRoot) {
    // Run objects are constructed before TARootHelper::Init(). Clearing this
    // name prevents manalyzer from creating any online ROOT output file.
    runinfo->fRoot->fOutputFileName.clear();
  }
}

void EventInspector::BeginRun(TARunInfo* runinfo) {
  detailed_printed_ = 0;
  decoded_events_ = 0;
  malformed_events_ = 0;
  decoded_limit_reached_ = false;
  event_ids_.clear();
  vme_ = SourceStatistics{};
  easiroc_ = SourceStatistics{};
  builder_.Clear();
  const bool online = options_.mode == AnalyzerMode::kOnline;
  auto histogram_configs = online
                               ? histogram_config_loader_.Load(runinfo->fOdb,
                                                               true)
                               : HistogramConfigLoader::Result{
                                     DefaultHistogramConfigs(), false, false};
  histogram_manager_.SetConfigs(std::move(histogram_configs.configs));
  std::printf("HistogramConfigLoader: using %s configuration%s\n",
              histogram_configs.loaded_from_odb ? "ODB" : "default",
              histogram_configs.created_defaults ? " (newly created)" : "");
  TDirectory* histogram_parent = nullptr;
  if (online) {
    histogram_parent = TARootHelper::fgDir;
  } else {
    TFile* output_file = runinfo->fRoot ? runinfo->fRoot->fOutputFile : nullptr;
    histogram_parent = output_file;
    if (tree_writer_) tree_writer_->BeginRun(output_file);
  }
  histogram_manager_.BeginRun(histogram_parent, !online);
  next_online_poll_ = std::chrono::steady_clock::now() +
                      std::chrono::seconds(1);
  std::printf("EventInspector: begin run %d, file %s\n", runinfo->fRunNo,
              runinfo->fFileName.c_str());
}

TAFlowEvent* EventInspector::Analyze(TARunInfo* runinfo, TMEvent* event,
                                     TAFlags* flags, TAFlowEvent* flow) {
  if (options_.mode == AnalyzerMode::kOnline) PollOnlineControls(runinfo);
  if (event) InspectEvent(*event);
  if (decoded_limit_reached_ && flags) *flags |= TAFlag_QUIT;
  return flow;
}

void EventInspector::ConsumeDecodedEvent(const DecodedEvent& event) {
  if (options_.decoded_event_limit > 0 &&
      decoded_events_ >= options_.decoded_event_limit)
    return;
  ++decoded_events_;
  if (tree_writer_) tree_writer_->Fill(event);
  histogram_manager_.Fill(event);
  if (options_.decoded_event_limit > 0 &&
      decoded_events_ >= options_.decoded_event_limit)
    decoded_limit_reached_ = true;
}

void EventInspector::PollOnlineControls(TARunInfo* runinfo) {
  const auto now = std::chrono::steady_clock::now();
  if (now < next_online_poll_) return;
  next_online_poll_ = now + std::chrono::seconds(1);

  auto loaded = histogram_config_loader_.Load(runinfo->fOdb, false);
  if (loaded.loaded_from_odb &&
      histogram_manager_.ApplyConfigs(std::move(loaded.configs))) {
    std::printf("HistogramConfigLoader: applied live ODB update (%zu active)\n",
                histogram_manager_.ActiveCount());
  }
  PollPdfRequest(runinfo);
}

void EventInspector::PollPdfRequest(TARunInfo* runinfo) {
  if (!runinfo || !runinfo->fOdb) return;
  std::unique_ptr<MVOdb> request_directory(
      runinfo->fOdb->Chdir("Analyzer/HistogramPdf", false));
  if (!request_directory) return;
  request_directory->SetPrintError(false);

  bool request = false;
  MVOdbError error;
  request_directory->RB("Request", &request, false, &error);
  if (error.fError || !request) return;

  std::string output_file;
  error = MVOdbError{};
  request_directory->RS("OutputFile", &output_file, false, 0, &error);
  if (error.fError) output_file.clear();

  const std::filesystem::path plot_directory(
      "/home/daq/midas/midas/plots");
  if (output_file.empty()) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S",
                  &local);
    output_file =
        (plot_directory /
         ("run" + std::to_string(runinfo->fRunNo) + "_" + timestamp +
          ".pdf"))
            .string();
  } else if (!std::filesystem::path(output_file).is_absolute()) {
    output_file = (plot_directory / output_file).string();
  }

  std::string write_error;
  if (histogram_pdf_writer_.Write(histogram_manager_, runinfo->fRunNo,
                                  output_file, &write_error)) {
    std::printf("HistogramPdfWriter: wrote %s\n", output_file.c_str());
  }

  // Acknowledge an explicit request even on failure to avoid repeated output.
  request_directory->WB("Request", false);
}

void EventInspector::AnalyzeSpecialEvent(TARunInfo*, TMEvent* event) {
  if (!event) return;
  ++event_ids_[event->event_id];
  if (detailed_printed_ < detailed_limit_) {
    std::printf("Special event: id=0x%04x serial=%u timestamp=%u data_size=%u\n",
                event->event_id, event->serial_number, event->time_stamp,
                event->data_size);
    ++detailed_printed_;
  }
}

RawEventSource EventInspector::Classify(const TMEvent& event) {
  bool easiroc = false;
  bool vme = false;
  for (const auto& bank : event.banks) {
    if (bank.name == "EAHG") easiroc = true;
    if (bank.name == "ADC0" || bank.name == "TDC0" || bank.name == "TDC1" ||
        bank.name == "FADC")
      vme = true;
  }
  if (easiroc == vme) return RawEventSource::kUnknown;
  return easiroc ? RawEventSource::kEasiroc : RawEventSource::kVme;
}

std::size_t EventInspector::ElementSize(std::uint32_t type) {
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

void EventInspector::RecordSourceEvent(TMEvent& event,
                                       SourceStatistics& statistics) {
  ++statistics.events;
  statistics.serials.insert(event.serial_number);
  for (const auto& bank : event.banks) {
    BankStatistics& bank_statistics = statistics.banks[bank.name];
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

void EventInspector::PrintDetailedEvent(TMEvent& event) const {
  std::printf("Event: id=0x%04x serial=%u timestamp=%u data_size=%u\n",
              event.event_id, event.serial_number, event.time_stamp,
              event.data_size);
  for (const auto& bank : event.banks) {
    const std::size_t length = bank.data_size / ElementSize(bank.type);
    std::printf("  bank %-4s type=%u length=%zu bytes=%u\n",
                bank.name.c_str(), bank.type, length, bank.data_size);
  }
}

void EventInspector::InspectEvent(TMEvent& event) {
  ++event_ids_[event.event_id];
  if (event.error) {
    ++malformed_events_;
    std::fprintf(stderr,
                 "WARNING: malformed MIDAS event id=0x%04x serial=%u\n",
                 event.event_id, event.serial_number);
    return;
  }
  event.FindAllBanks();
  if (detailed_printed_ < detailed_limit_) {
    PrintDetailedEvent(event);
    ++detailed_printed_;
  }

  const RawEventSource source = Classify(event);
  if (source == RawEventSource::kVme) {
    RecordSourceEvent(event, vme_);
    builder_.AddEvent(event, source);
  } else if (source == RawEventSource::kEasiroc) {
    RecordSourceEvent(event, easiroc_);
    builder_.AddEvent(event, source);
  } else if (event.event_id == 0x0001) {
    std::fprintf(stderr,
                 "WARNING: physics event id=0x0001 serial=%u has unknown or "
                 "ambiguous bank composition; event not built\n",
                 event.serial_number);
  }
}

void EventInspector::PrintSourceSummary(
    const char* name, const SourceStatistics& statistics) const {
  std::printf("\n%s\n", name);
  std::printf("  events            : %zu\n", statistics.events);
  if (statistics.serials.empty()) {
    std::printf("  serial first/last : n/a\n  serial gaps       : 0\n");
  } else {
    const std::uint32_t first = *statistics.serials.begin();
    const std::uint32_t last = *statistics.serials.rbegin();
    const std::size_t span = static_cast<std::size_t>(last - first) + 1;
    const std::size_t gaps = span - statistics.serials.size();
    std::printf("  serial first/last : %u / %u\n", first, last);
    std::printf("  serial gaps       : %zu\n", gaps);
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

void EventInspector::EndRun(TARunInfo* runinfo) {
  if (decoded_limit_reached_)
    builder_.DiscardPending();
  else
    builder_.Finish();
  const auto tree_entries = tree_writer_ ? tree_writer_->Entries() : 0;
  const auto event_hist_entries = histogram_manager_.Entries("h_event");
  const auto qdc_hist_entries = histogram_manager_.Entries("h_qdc0_ch0");
  const auto eadc_hist_entries = histogram_manager_.Entries("h_eadc0_ch0");
  const auto tle_hist_entries =
      histogram_manager_.Entries("h_tle0_ch0_hit0");
  const auto fadc_hist_entries =
      histogram_manager_.Entries("h_fadc0_ch0_sample0");
  if (tree_writer_) tree_writer_->EndRun();
  histogram_manager_.EndRun();
  std::printf("\nRun %d summary\n", runinfo->fRunNo);
  if (options_.mode == AnalyzerMode::kOffline) {
    std::printf("ROOT Events entries : %lld\n",
                static_cast<long long>(tree_entries));
  } else {
    std::printf("Decoded events      : %zu\n", decoded_events_);
  }
  PrintSourceSummary("EASIROC", easiroc_);
  PrintSourceSummary("VME", vme_);

  std::printf("\nEvent IDs\n");
  for (const auto& [event_id, count] : event_ids_)
    std::printf("  0x%04x : %zu\n", event_id, count);

  const auto& stats = builder_.GetStatistics();
  std::printf("\nEventBuilder\n");
  std::printf("  paired            : %zu\n", stats.paired);
  std::printf("  VME only          : %zu\n", stats.vme_only);
  std::printf("  EASIROC only      : %zu\n", stats.easiroc_only);
  std::printf("  counter mismatches: %zu\n", stats.counter_mismatches);
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
  std::printf("  h_qdc0_ch0             : %lld\n",
              static_cast<long long>(qdc_hist_entries));
  std::printf("  h_eadc0_ch0            : %lld\n",
              static_cast<long long>(eadc_hist_entries));
  std::printf("  h_tle0_ch0_hit0        : %lld\n",
              static_cast<long long>(tle_hist_entries));
  std::printf("  h_fadc0_ch0_sample0    : %lld\n",
              static_cast<long long>(fadc_hist_entries));

  const auto print_raw_counter = [](const char* name,
                                    const EventBuilder::Statistics::RawCounters& counter) {
    std::printf("  %-17s:", name);
    if (counter.values.empty()) {
      std::printf(" unavailable\n");
      return;
    }
    const auto first = *counter.values.begin();
    const auto last = *counter.values.rbegin();
    const auto span = static_cast<std::size_t>(last - first) + 1;
    const auto gaps = span - counter.values.size();
    std::printf(" %lld / %lld (events=%zu gaps=%zu)\n",
                static_cast<long long>(first), static_cast<long long>(last),
                counter.values.size(), gaps);
  };
  std::printf("\nHardware raw counters (first / last)\n");
  print_raw_counter("V792", stats.v792);
  print_raw_counter("V775", stats.v775);
  print_raw_counter("V1190", stats.v1190);
  print_raw_counter("V1720", stats.v1720);
  print_raw_counter("NIM-EASIROC", stats.nim_easiroc);
}

}  // namespace ana
