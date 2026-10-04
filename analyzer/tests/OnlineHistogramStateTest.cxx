#include "OnlineHistogramState.h"

#include "TCanvas.h"
#include "TDirectory.h"
#include "TH1.h"
#include "TMemFile.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

bool Check(bool condition, const char* message) {
  if (condition) return true;
  std::fprintf(stderr, "FAIL: %s\n", message);
  return false;
}

ana::HistogramConfig Config(int bins) {
  return {"h_qdc0_ch00", "TH1D", "qdc0[0]", bins, 0, 1000, "", true,
          "QDC", "raw", "Counts", "QDC0", "Ch00"};
}

}  // namespace

int main() {
  TMemFile root("online_histogram_state_test.root", "RECREATE");
  ana::OnlineHistogramState state;
  ana::PageConfig page{"Monitor", 3, 3, {"QDC0/h_qdc0_ch00"}};
  bool okay = Check(state.Initialize(&root, {Config(100)}, {page}),
                    "startup books histograms and page");
  auto* first = state.histograms.FindHistogram("QDC0/h_qdc0_ch00");
  auto* pages_dir = root.GetDirectory("Pages");
  auto* first_page = pages_dir
      ? dynamic_cast<TCanvas*>(pages_dir->Get("Monitor")) : nullptr;
  okay &= Check(first && first->GetEntries() == 0 && first_page,
                "startup publishes empty histogram and page before BOR");
  okay &= Check(state.BeginRun(&root, {Config(100)}, {page}, 42),
                "first BOR accepts startup objects");
  okay &= Check(state.histograms.FindHistogram("QDC0/h_qdc0_ch00") == first &&
                    pages_dir->Get("Monitor") == first_page,
                "BOR with unchanged ODB config reuses startup objects");
  ana::DecodedEvent event;
  event.v792.qdc0[0] = 12;
  state.histograms.Fill(event);
  okay &= Check(first && first->GetEntries() == 1,
                "RUNNING Fill increments entries");
  okay &= Check(first_page != nullptr, "RUNNING page exists");

  state.EndRun();
  okay &= Check(state.histograms.FindHistogram("QDC0/h_qdc0_ch00") == first,
                "EOR retains histogram identity");
  okay &= Check(first->GetEntries() == 1 && first->GetBinContent(
                    first->FindBin(12)) == 1,
                "EOR retains entries and contents");
  okay &= Check(pages_dir->Get("Monitor") == first_page,
                "EOR retains page canvas identity");

  const auto pdf_path = std::filesystem::temp_directory_path() /
      ("online_histogram_state_" + std::to_string(getpid()) + ".pdf");
  std::string error;
  okay &= Check(state.ExportPdf(pdf_path.string(), &error),
                "PDF export works after EOR");
  okay &= Check(std::filesystem::exists(pdf_path) &&
                    std::filesystem::file_size(pdf_path) > 0,
                "PDF output is nonempty");
  std::filesystem::remove(pdf_path);

  okay &= Check(state.BeginRun(&root, {Config(100)}, {page}, 43),
                "next BOR resets unchanged configuration");
  okay &= Check(state.histograms.FindHistogram("QDC0/h_qdc0_ch00") == first &&
                    first->GetEntries() == 0 &&
                    pages_dir->Get("Monitor") == first_page,
                "next BOR resets histogram without replacing it or its page");
  state.histograms.Fill(event);
  state.EndRun();

  okay &= Check(state.BeginRun(&root, {Config(25)}, {page}, 44),
                "BOR accepts changed histogram configuration");
  auto* second = state.histograms.FindHistogram("QDC0/h_qdc0_ch00");
  okay &= Check(second && second->GetNbinsX() == 25 &&
                    second->GetEntries() == 0,
                "structural ODB change rebooks histogram with empty contents");
  okay &= Check(pages_dir->Get("Monitor") != nullptr,
                "histogram change rebuilds page references");
  state.EndRun();

  ana::PageConfig updated_page{"Updated", 3, 3,
                                {"QDC0/h_qdc0_ch00"}};
  okay &= Check(state.BeginRun(&root, {Config(25)}, {updated_page}, 45),
                "BOR accepts changed page configuration");
  okay &= Check(pages_dir->Get("Updated") != nullptr &&
                    pages_dir->Get("Monitor") == nullptr,
                "page ODB change replaces old canvas");
  state.EndRun();
  state.Clear();
  if (!okay) return 1;
  std::puts("Online histogram lifecycle tests passed");
  return 0;
}
