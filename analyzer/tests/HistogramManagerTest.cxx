#include "HistogramManager.h"

#include "TDirectory.h"
#include "TH1.h"
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

  if (!okay) return 1;
  std::printf("HistogramManager tests passed\n");
  return 0;
}
