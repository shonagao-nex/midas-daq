#include "HistogramManager.h"
#include "PageManager.h"

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

  pages.ClearCanvases();
  histograms.ApplyConfigs({histogram_configs.front()});
  okay &= Check(pages.Rebuild(&histograms), "rebuild after histogram deletion");
  pages.Clear();
  histograms.EndRun();
  if (!okay) return 1;
  std::printf("PageManager tests passed\n");
  return 0;
}
