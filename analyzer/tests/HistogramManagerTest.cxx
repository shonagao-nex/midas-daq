#include "HistogramManager.h"

#include "TDirectory.h"
#include "TH1.h"
#include "TGraph.h"
#include "TList.h"
#include "TMultiGraph.h"
#include "TMemFile.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

bool Check(bool condition, const std::string& message) {
  if (condition) return true;
  std::fprintf(stderr, "FAIL: %s\n", message.c_str());
  return false;
}

}  // namespace

int main() {
  const std::vector<ana::HistogramConfig> configs{
      {"enabled", "TH1D", "qdc0[0]", 100, 0.0, 1000.0, "", true,
       "QDC title", "QDC raw", "Counts", "QDC0", "Ch00"},
      {"disabled", "TH1D", "event", 100, 0.0, 1000.0, "", false,
       "Disabled", "x", "y", "QDC0", "Ch01"},
      {"missing_nested", "TH1D", "tle0[0][0]", 100, 0.0, 1000.0, "",
       true, "", "", "", "", ""},
  };

  TMemFile output("histogram_manager_test.root", "RECREATE");
  ana::HistogramManager manager(configs);
  bool okay =
      Check(manager.BeginRun(&output, false), "valid configs should book");

  ana::DecodedEvent event;
  event.counters.event = 1;
  event.v792.qdc0[0] = 123;
  manager.Fill(event);
  okay &= Check(manager.Entries("enabled") == 1,
                "valid fixed value should fill");
  okay &= Check(manager.Entries("disabled") == 0,
                "disabled histogram should not fill");
  okay &= Check(manager.Entries("missing_nested") == 0,
                "missing nested index should not fill");

  event.v792.qdc0[0] = ana::kInvalidValue;
  manager.Fill(event);
  okay &= Check(manager.Entries("enabled") == 1,
                "-999 sentinel should not fill");

  TDirectory* directory = output.GetDirectory("Histograms");
  TDirectory* qdc_directory =
      directory ? directory->GetDirectory("QDC0") : nullptr;
  okay &= Check(qdc_directory && qdc_directory->Get("enabled"),
                "enabled histogram should be booked");
  okay &= Check(qdc_directory && !qdc_directory->Get("disabled"),
                "disabled histogram should not be booked");
  if (qdc_directory && qdc_directory->Get("enabled")) {
    auto* histogram = dynamic_cast<TH1*>(qdc_directory->Get("enabled"));
    okay &= Check(histogram && std::string(histogram->GetTitle()) ==
                              "QDC title" &&
                      std::string(histogram->GetXaxis()->GetTitle()) ==
                          "QDC raw" &&
                      std::string(histogram->GetYaxis()->GetTitle()) ==
                          "Counts",
                  "histogram titles should be applied");
  }

  const std::vector<ana::HistogramConfig> reloaded{
      {"enabled", "TH1D", "qdc0[1]", 20, 0.0, 2000.0, "", true,
       "Reloaded", "raw", "Counts", "QDC0", "Ch00"},
      {"added", "TH1D", "event", 10, 0.0, 10.0, "", true,
       "Added", "raw", "Counts", "Event", "Ch00"},
  };
  okay &= Check(manager.ApplyConfigs(reloaded),
                "changed configuration should be applied");
  okay &= Check(manager.ActiveCount() == 2,
                "reload should add and remove histograms");
  okay &= Check(manager.Entries("enabled") == 0,
                "expression/binning change should reset contents");
  TDirectory* event_directory =
      directory ? directory->GetDirectory("Event") : nullptr;
  okay &= Check(qdc_directory && !qdc_directory->Get("missing_nested") &&
                    event_directory && event_directory->Get("added"),
                "reload should remove stale object and book new object");

  auto disabled_reload = reloaded;
  disabled_reload[0].enabled = false;
  okay &= Check(manager.ApplyConfigs(disabled_reload),
                "Enabled change should be applied");
  okay &= Check(manager.ActiveCount() == 1 &&
                    qdc_directory && !qdc_directory->Get("enabled"),
                "disabled object should be deleted");
  okay &= Check(manager.ApplyConfigs(reloaded),
                "re-enabled configuration should be applied");
  okay &= Check(manager.ActiveCount() == 2 &&
                    qdc_directory && qdc_directory->Get("enabled") &&
                    manager.Entries("enabled") == 0,
                "re-enabled object should be booked with empty contents");
  manager.EndRun();

  auto waveforms = ana::DefaultWaveformConfigs();
  waveforms.push_back(configs.front());
  manager.SetConfigs(waveforms);
  okay &= Check(manager.BeginRun(&output, false), "waveforms book");
  const auto find_history = [&manager](const char* path) {
    return dynamic_cast<TMultiGraph*>(manager.FindPlot(path));
  };
  auto* ch0 = find_history("FADC0/h_fadc0_ch00");
  auto* ch1 = find_history("FADC0/h_fadc0_ch01");
  if (!Check(ch0 && ch1, "channel history objects exist")) return 1;
  auto* traces = ch0->GetListOfGraphs();
  auto* other_traces = ch1->GetListOfGraphs();
  const auto graph = [](TList* list, int index) {
    return static_cast<TGraph*>(list->At(index));
  };
  okay &= Check(traces->GetSize() == 20 && graph(traces, 19)->GetLineWidth() == 0,
                "fixed empty slots are present for ROOT Web monitoring");

  ana::DecodedEvent waveform_event;
  waveform_event.v792.qdc0[0] = 123;
  waveform_event.v1720.fadc0[0] = {100, 0, 4095};
  waveform_event.v1720.fadc0[1] = {101, 201};
  manager.Fill(waveform_event, true, false);
  okay &= Check(manager.Entries("enabled") == 1 && graph(traces, 19)->GetLineWidth() == 0,
                "ordinary fill does not update waveform slots");
  manager.Fill(waveform_event, false, true);
  auto* oldest = graph(traces, 19);
  okay &= Check(manager.Entries("enabled") == 1 && oldest->GetN() == 3 &&
                    oldest->GetPointX(0) == 0 && oldest->GetPointY(1) == 0 &&
                    oldest->GetPointY(2) == 4095,
                "waveform fill does not accumulate scalar entries; zero ADC is valid");

  waveform_event.Clear();
  for (int event = 1; event < 20; ++event) {
    waveform_event.v1720.fadc0[0] = {100 + event, 200 + event};
    manager.Fill(waveform_event, false, true);
  }
  okay &= Check(graph(traces, 0) == oldest && graph(traces, 19)->GetPointY(0) == 119,
                "history fills in arrival order");
  waveform_event.v1720.fadc0[0] = {120, ana::kInvalidValue};
  manager.Fill(waveform_event, false, true);
  okay &= Check(traces->GetSize() == 20 && graph(traces, 0)->GetPointY(0) == 101 &&
                    graph(traces, 19) == oldest && oldest->GetN() == 1 &&
                    oldest->GetPointY(0) == 120 && oldest->GetLineWidth() == 2 &&
                    graph(traces, 18)->GetLineWidth() == 1 &&
                    graph(traces, 18)->GetLineColor() != oldest->GetLineColor(),
                "the oldest slot is reused and the newest trace is emphasized");
  okay &= Check(graph(other_traces, 19)->GetPointY(0) == 101 &&
                    graph(other_traces, 18)->GetLineWidth() == 0,
                "a missing channel does not append to its history");
  const auto snapshot = manager.Snapshot();
  auto* saved = snapshot.empty() ? nullptr : dynamic_cast<TMultiGraph*>(snapshot[0].get());
  manager.Reset();
  okay &= Check(graph(traces, 19)->GetLineWidth() == 0 && saved && saved != ch0 &&
                    graph(saved->GetListOfGraphs(), 19)->GetPointY(0) == 120,
                "reset clears live traces without affecting the PDF snapshot");

  auto window = waveforms.front();
  window.min = 1.5;
  window.max = 3.5;
  window.bins = 2;
  manager.ApplyConfigs({window});
  waveform_event.v1720.fadc0[0] = {10, 20, 30, 40, 50};
  manager.Fill(waveform_event);
  auto* cropped = find_history("FADC0/h_fadc0_ch00");
  auto* newest = cropped ? graph(cropped->GetListOfGraphs(), 19) : nullptr;
  okay &= Check(newest && newest->GetN() == 2 && newest->GetPointX(0) == 2 &&
                    newest->GetPointY(0) == 30 && newest->GetPointY(1) == 40,
                "the configured sample window selects the plotted points");
  window.enabled = false;
  manager.ApplyConfigs({window});
  okay &= Check(manager.ActiveCount() == 0 &&
                    output.GetDirectory("Histograms/FADC0")->Get("h_fadc0_ch00") == nullptr,
                "disabling a waveform removes its published ROOT object");
  manager.EndRun();

  const ana::HistogramConfig hit_counts{
      "hits", "TH1D", "hit_channels(tdc0[5], tdc0[1], tdc0[2], tle0[10], ttr0[3], etle0[5], ettr0[0])",
      0, 0, 0, "", true, "TDC hit counts", "Channel", "Hit events", "TDC", "Ch00"};
  manager.SetConfigs({hit_counts});
  okay &= Check(manager.BeginRun(&output, false), "hit count histogram books from the expression");
  auto* hits = manager.FindHistogram("TDC/hits");
  if (!Check(hits && hits->GetNbinsX() == 7, "one bin per specified channel")) return 1;
  const auto* axis = hits->GetXaxis();
  okay &= Check(std::string(axis->GetBinLabel(1)) == "V775 ch5" &&
                    std::string(axis->GetBinLabel(2)) == "V775 ch1" &&
                    std::string(axis->GetBinLabel(4)) == "V1190 LE ch10",
                "mixed TDC channels retain expression order and identify their source");
  ana::DecodedEvent tdc_event;
  tdc_event.v775.tdc0[5] = 11;
  tdc_event.v775.tdc0[1] = 0;
  tdc_event.v1190.tle0[10] = {0, 27, 28};
  tdc_event.v1190.ttr0[3] = {0, 0};
  tdc_event.easiroc.etle0[5] = {ana::kInvalidValue, 11, 12};
  tdc_event.easiroc.ettr0[0] = {0, ana::kInvalidValue};
  manager.Fill(tdc_event, false, true);
  okay &= Check(hits->GetEntries() == 0, "hit counts obey histogram prescale");
  manager.Fill(tdc_event);
  okay &= Check(hits->GetEntries() == 3 && hits->GetBinContent(1) == 1 &&
                    hits->GetBinContent(2) == 0 && hits->GetBinContent(3) == 0 &&
                    hits->GetBinContent(4) == 1 && hits->GetBinContent(5) == 0 &&
                    hits->GetBinContent(6) == 1 && hits->GetBinContent(7) == 0,
                "only positive hits count, once per event even for multi-hit channels");
  tdc_event.Clear();
  tdc_event.v775.tdc0[1] = 7;
  tdc_event.v1190.tle0[10] = {2, 3};
  tdc_event.v1190.ttr0[3] = {13, 14};
  tdc_event.easiroc.ettr0[0] = {1, 2};
  manager.Fill(tdc_event);
  okay &= Check(hits->GetEntries() == 7 && hits->GetBinContent(1) == 1 &&
                    hits->GetBinContent(2) == 1 && hits->GetBinContent(4) == 2 &&
                    hits->GetBinContent(5) == 1 && hits->GetBinContent(7) == 1,
                "counts accumulate across events in the selected channel bins");
  manager.Reset();
  okay &= Check(hits->GetEntries() == 0 && std::string(axis->GetBinLabel(1)) == "V775 ch5",
                "run reset clears counts and keeps channel labels");
  manager.EndRun();

  for (const auto* invalid : {"hit_channels()", "hit_channels(tdc0[32])",
                               "hit_channels(tle0[2][0])", "hit_channels(qdc0[0])",
                               "hit_channels(tdc0[1], tdc0[01])", "hit_channels(tdc0[1],)"}) {
    auto config = hit_counts;
    config.expression = invalid;
    okay &= Check(ana::ValidateHistogramConfigs({config}).skipped == 1,
                  std::string("reject invalid channel list: ") + invalid);
  }

  if (!okay) return 1;
  std::printf("HistogramManager tests passed\n");
  return 0;
}
