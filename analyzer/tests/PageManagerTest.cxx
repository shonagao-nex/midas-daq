#include "HistogramManager.h"
#include "PageManager.h"
#include "PageConfigLoader.h"

#include "TCanvas.h"
#include "TDirectory.h"
#include "TH1.h"
#include "TMemFile.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {
bool Check(bool condition, const char* message) {
  if (condition) return true;
  std::fprintf(stderr, "FAIL: %s\n", message);
  return false;
}
}  // namespace

int main() {
  const std::vector<ana::HistogramConfig> histogram_configs{
      {"h_qdc0_ch00", "TH1D", "qdc0[0]", 100, 0, 1000, "", true,
       "QDC", "raw", "Counts", "QDC0", "Ch00"},
      {"h_tdc0_ch00", "TH1D", "tdc0[0]", 100, 0, 1000, "", true,
       "TDC", "raw", "Counts", "TDC0", "Ch00"},
  };
  TMemFile output("page_manager_test.root", "RECREATE");
  ana::HistogramManager histograms(histogram_configs);
  bool okay = Check(histograms.BeginRun(&output, false), "histograms book");

  ana::DecodedEvent event;
  event.v792.qdc0[0] = 10;
  event.v775.tdc0[0] = 20;
  histograms.Fill(event);
  okay &= Check(histograms.Entries("h_qdc0_ch00") == 1,
                "histogram contents before page changes");

  ana::PageManager pages;
  okay &= Check(pages.BeginRun(&output, &histograms), "pages begin");
  ana::PageConfig page{"TriggerMonitor", 3, 3,
                       {"QDC0/h_qdc0_ch00", "TDC0/h_tdc0_ch00",
                        "Missing/h_missing"}};
  okay &= Check(pages.ApplyConfigs({page}, &histograms), "3x3 page apply");
  TDirectory* page_directory = output.GetDirectory("Pages");
  auto* canvas = page_directory
                     ? dynamic_cast<TCanvas*>(page_directory->Get("TriggerMonitor"))
                     : nullptr;
  okay &= Check(canvas && canvas->GetPad(1) && canvas->GetPad(2),
                "page canvas exists");
  okay &= Check(canvas && canvas->GetPad(1)->GetListOfPrimitives()->GetSize() > 0,
                "first cross-group pad has histogram");
  okay &= Check(canvas && canvas->GetPad(2)->GetListOfPrimitives()->GetSize() > 0,
                "second cross-group pad has histogram");
  okay &= Check(canvas && canvas->GetPad(3)->GetListOfPrimitives()->GetSize() == 0,
                "missing histogram leaves pad empty");

  page.rows = 4;
  page.columns = 4;
  page.pads.resize(16);
  okay &= Check(pages.ApplyConfigs({page}, &histograms), "4x4 page apply");
  page.rows = 4;
  page.columns = 5;
  page.pads.resize(20);
  okay &= Check(pages.ApplyConfigs({page}, &histograms), "4x5 page apply");
  okay &= Check(histograms.Entries("h_qdc0_ch00") == 1,
                "page changes preserve histogram contents");

  const auto defaults = ana::DefaultPageConfigs();
  const std::vector<std::string> expected_names{
      "QDC0_00_31", "TDC0_00_31", "V1190_TLE_00_31",
      "V1190_TTR_00_31"};
  const std::vector<std::string> groups{"QDC0", "TDC0", "TLE0", "TTR0"};
  const std::vector<std::string> prefixes{"qdc0", "tdc0", "tle0", "ttr0"};
  okay &= Check(defaults.size() == 4, "four standard pages exist");
  if (defaults.size() == 4) {
    for (std::size_t page_index = 0; page_index < defaults.size(); ++page_index) {
      const auto& standard = defaults[page_index];
      okay &= Check(standard.name == expected_names[page_index] &&
                        standard.rows == 4 && standard.columns == 8 &&
                        standard.pads.size() == 32 &&
                        ana::IsSupportedPageLayout(standard),
                    "standard page name, order, and 8x4 layout");
      if (standard.pads.size() != 32) continue;
      for (int channel = 0; channel < 32; ++channel) {
        char expected[64];
        std::snprintf(expected, sizeof(expected), "%s/h_%s_ch%02d",
                      groups[page_index].c_str(),
                      prefixes[page_index].c_str(), channel);
        okay &= Check(standard.pads[channel] == expected,
                      "pad order must be row-major channels 00 through 31");
      }
    }
  }
  const auto fallback = ana::PageConfigLoader{}.Load(nullptr);
  okay &= Check(!fallback.odb_path_found && fallback.pages == defaults,
                "missing ODB pages must use in-memory defaults");

  auto standard_histograms = ana::DefaultHistogramConfigs();
  std::vector<ana::HistogramConfig> selected;
  for (auto& config : standard_histograms) {
    if (config.group != "QDC0" && config.group != "TDC0" &&
        config.group != "TLE0" && config.group != "TTR0")
      continue;
    if (config.hist_name.size() >= 4 &&
        config.hist_name.substr(config.hist_name.size() - 4) == "ch05")
      config.enabled = false;
    selected.push_back(config);
  }
  pages.ClearCanvases();
  histograms.ApplyConfigs(std::move(selected));
  okay &= Check(pages.ApplyConfigs(defaults, &histograms),
                "standard pages apply");
  okay &= Check(pages.ActiveCount() == 4,
                "all four standard canvases are present");
  for (std::size_t page_index = 0; page_index < expected_names.size(); ++page_index) {
    auto* standard_canvas = dynamic_cast<TCanvas*>(
        page_directory->Get(expected_names[page_index].c_str()));
    okay &= Check(standard_canvas && standard_canvas->GetPad(32),
                  "standard canvas has 32 pads");
    if (!standard_canvas) continue;
    for (int channel = 0; channel < 32; ++channel) {
      auto* pad = standard_canvas->GetPad(channel + 1);
      const bool expected_histogram = channel != 5;
      const auto& path = defaults[page_index].pads[channel];
      const auto name = path.substr(path.find('/') + 1);
      okay &= Check(pad &&
                        (pad->GetListOfPrimitives()->FindObject(name.c_str()) !=
                         nullptr) == expected_histogram,
                    "channel pad draws enabled histogram or stays empty");
    }
  }

  pages.ClearCanvases();
  histograms.ApplyConfigs({histogram_configs.front()});
  okay &= Check(pages.Rebuild(&histograms), "rebuild after histogram deletion");
  pages.Clear();
  histograms.EndRun();
  if (!okay) return 1;
  std::printf("PageManager tests passed\n");
  return 0;
}
