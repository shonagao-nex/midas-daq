#include "HistogramManager.h"

#include "TDirectory.h"
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
      {"enabled", "TH1D", "qdc0[0]", 100, 0.0, 1000.0, "", true},
      {"disabled", "TH1D", "event", 100, 0.0, 1000.0, "", false},
      {"missing_nested", "TH1D", "tle0[0][0]", 100, 0.0, 1000.0, "",
       true},
  };

  TMemFile output("histogram_manager_test.root", "RECREATE");
  ana::HistogramManager manager(configs);
  bool okay = Check(manager.BeginRun(&output), "valid configs should book");

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
  okay &= Check(directory && directory->Get("enabled"),
                "enabled histogram should be booked");
  okay &= Check(directory && !directory->Get("disabled"),
                "disabled histogram should not be booked");
  manager.EndRun();

  if (!okay) return 1;
  std::printf("HistogramManager tests passed\n");
  return 0;
}
