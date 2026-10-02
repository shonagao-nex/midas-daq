#include "EventInspector.h"

#include "midasio.h"
#include "mvodb.h"

#include "TDirectory.h"
#include "TFile.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

namespace ana {

namespace {

bool ReadEquipmentEnabled(MVOdb* odb, const char* equipment, bool* enabled) {
  if (!odb) return false;
  const bool print_error = odb->GetPrintError();
  odb->SetPrintError(false);
  MVOdbError error;
  std::unique_ptr<MVOdb> common(odb->Chdir(equipment, false, &error));
  odb->SetPrintError(print_error);
  if (!common || error.fError) return false;
  common->SetPrintError(false);
  common->RB("Enabled", enabled, false, &error);
  return !error.fError;
}

EventBuilder::ExpectedSources ReadExpectedSources(MVOdb* odb) {
  bool vme_enabled = false;
  bool easiroc_enabled = false;
  const bool valid =
      ReadEquipmentEnabled(odb, "Equipment/VME/Common", &vme_enabled) &&
      ReadEquipmentEnabled(odb, "Equipment/EASIROC/Common", &easiroc_enabled);
  if (!valid || (!vme_enabled && !easiroc_enabled)) {
    std::fprintf(stderr,
                 "WARNING: equipment Enabled settings unavailable or both "
                 "disabled; expecting both sources for this run\n");
    return EventBuilder::ExpectedSources::kBoth;
  }
  if (vme_enabled && easiroc_enabled)
    return EventBuilder::ExpectedSources::kBoth;
  return vme_enabled ? EventBuilder::ExpectedSources::kVmeOnly
                     : EventBuilder::ExpectedSources::kEasirocOnly;
}

const char* ExpectedSourcesName(EventBuilder::ExpectedSources sources) {
  switch (sources) {
    case EventBuilder::ExpectedSources::kVmeOnly: return "VME only";
    case EventBuilder::ExpectedSources::kEasirocOnly: return "EASIROC only";
    case EventBuilder::ExpectedSources::kBoth: return "VME and EASIROC";
  }
  return "unknown";
}

}  // namespace

EventInspector::EventInspector(TARunInfo* runinfo, EventInspectorOptions options,
                               OnlineHistogramState* online_state)
    : TARunObject(runinfo),
      options_(options),
      diagnostics_(options.debug_events),
      online_state_(options.mode == AnalyzerMode::kOnline ? online_state
                                                       : nullptr),
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

HistogramManager& EventInspector::Histograms() {
  return online_state_ ? online_state_->histograms : histogram_manager_;
}

PageManager& EventInspector::Pages() {
  return online_state_ ? online_state_->pages : page_manager_;
}

void EventInspector::BeginRun(TARunInfo* runinfo) {
  diagnostics_.Reset();
  decoded_events_ = 0;
  decoded_limit_reached_ = false;
  builder_.Clear();
  const bool online = options_.mode == AnalyzerMode::kOnline;
  if (online) {
    const auto expected_sources = ReadExpectedSources(runinfo->fOdb);
    builder_.SetExpectedSources(expected_sources);
    std::printf("EventBuilder expected sources: %s\n",
                ExpectedSourcesName(expected_sources));
  }
  if (online) histogram_prescale_.BeginRun(runinfo->fOdb);
  auto histogram_configs = online
                               ? histogram_config_loader_.Load(runinfo->fOdb)
                               : HistogramConfigLoader::Result{
                                     DefaultHistogramConfigs(), false, false};
  if (online && !histogram_configs.odb_path_found) {
    std::fprintf(stderr,
                 "WARNING: %s not found; using in-memory default histogram "
                 "configuration; ODB was not modified.\n",
                 HistogramConfigLoader::kOdbPath);
  } else {
    std::printf("HistogramConfigLoader: using %s configuration\n",
                histogram_configs.loaded_from_odb ? "ODB" : "default");
  }
  TDirectory* histogram_parent = nullptr;
  if (online) {
    histogram_parent = TARootHelper::fgDir;
  } else {
    TFile* output_file = runinfo->fRoot ? runinfo->fRoot->fOutputFile : nullptr;
    histogram_parent = output_file;
    if (tree_writer_) tree_writer_->BeginRun(output_file);
  }
  if (online) {
    auto pages = page_config_loader_.Load(runinfo->fOdb);
    online_state_->BeginRun(histogram_parent,
                            std::move(histogram_configs.configs),
                            std::move(pages.pages), runinfo->fRunNo);
  } else {
    histogram_manager_.SetConfigs(std::move(histogram_configs.configs));
    histogram_manager_.BeginRun(histogram_parent, true);
  }
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
  if (histogram_prescale_.ShouldFill(options_.mode, decoded_events_))
    Histograms().Fill(event);
  if (options_.decoded_event_limit > 0 &&
      decoded_events_ >= options_.decoded_event_limit)
    decoded_limit_reached_ = true;
}

void EventInspector::PollOnlineControls(TARunInfo* runinfo) {
  const auto now = std::chrono::steady_clock::now();
  if (now < next_online_poll_) return;
  next_online_poll_ = now + std::chrono::seconds(1);

  histogram_prescale_.Poll(runinfo->fOdb);

  auto loaded = histogram_config_loader_.Load(runinfo->fOdb);
  const bool histogram_changed =
      loaded.loaded_from_odb &&
      !Histograms().ConfigsMatch(loaded.configs);
  if (histogram_changed) {
    Pages().ClearCanvases();
    Histograms().ApplyConfigs(std::move(loaded.configs));
    Pages().Rebuild(&Histograms());
    std::printf("HistogramConfigLoader: applied live ODB update (%zu active)\n",
                Histograms().ActiveCount());
  }
  auto pages = page_config_loader_.Load(runinfo->fOdb);
  if (Pages().ApplyConfigs(std::move(pages.pages), &Histograms())) {
    std::printf("PageConfigLoader: applied live page update (%zu pages)\n",
                Pages().ActiveCount());
  }
  PollPdfRequest(runinfo);
}

void EventInspector::PollPdfRequest(TARunInfo* runinfo) {
  if (runinfo && online_state_) online_state_->PollPdfRequest(runinfo->fOdb);
}

void EventInspector::AnalyzeSpecialEvent(TARunInfo*, TMEvent* event) {
  if (event) diagnostics_.RecordSpecialEvent(*event);
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

void EventInspector::InspectEvent(TMEvent& event) {
  if (!diagnostics_.BeginEvent(event)) return;

  const RawEventSource source = Classify(event);
  if (source == RawEventSource::kVme || source == RawEventSource::kEasiroc) {
    diagnostics_.RecordSourceEvent(event, source);
    builder_.AddEvent(event, source);
  } else if (event.event_id == 0x0001) {
    std::fprintf(stderr,
                 "WARNING: physics event id=0x0001 serial=%u has unknown or "
                 "ambiguous bank composition; event not built\n",
                 event.serial_number);
  }
}

void EventInspector::EndRun(TARunInfo* runinfo) {
  if (decoded_limit_reached_)
    builder_.DiscardPending();
  else
    builder_.Finish();
  const auto tree_entries = tree_writer_ ? tree_writer_->Entries() : 0;
  const auto event_hist_entries = Histograms().Entries("h_event");
  if (tree_writer_) tree_writer_->EndRun();
  if (options_.mode == AnalyzerMode::kOnline) {
    online_state_->EndRun();
  } else {
    page_manager_.Clear();
    histogram_manager_.EndRun();
  }
  diagnostics_.PrintRunSummary(runinfo->fRunNo,
                               options_.mode == AnalyzerMode::kOffline,
                               tree_entries, decoded_events_, event_hist_entries,
                               builder_.GetStatistics());
}

}  // namespace ana
