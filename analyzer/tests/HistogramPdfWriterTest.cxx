#include "HistogramManager.h"
#include "HistogramPdfWriter.h"

#include "TMemFile.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
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
      {"filled", "TH1D", "event", 10, 0.0, 10.0, "", true, "Filled",
       "Event", "Counts", "Event", "Ch00"},
      {"empty", "TH1D", "qdc0[0]", 10, 0.0, 10.0, "", true, "Empty",
       "QDC raw", "Counts", "QDC0", "Ch00"},
      {"disabled", "TH1D", "event", 10, 0.0, 10.0, "", false,
       "Disabled", "Event", "Counts", "Event", "Ch01"},
  };

  TMemFile parent("histogram_pdf_writer_test.root", "RECREATE");
  ana::HistogramManager manager(configs);
  bool okay = Check(manager.BeginRun(&parent, false),
                    "histograms should book for PDF test");
  ana::DecodedEvent event;
  event.counters.event = 5;
  manager.Fill(event);

  constexpr const char* output = "/tmp/histogram_pdf_writer_test.pdf";
  ana::HistogramPdfWriter writer;
  std::string error;
  okay &= Check(writer.Write(manager, 62, output, &error),
                "multipage PDF should be written");
  okay &= Check(std::filesystem::exists(output) &&
                    std::filesystem::file_size(output) > 0,
                "PDF should be non-empty");

  std::ifstream stream(output, std::ios::binary);
  const std::string contents((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
  okay &= Check(contents.rfind("%PDF-", 0) == 0,
                "output should have a PDF header");
  okay &= Check(contents.find("%%EOF") != std::string::npos,
                "multipage PDF should be closed");
  okay &= Check(manager.ActiveCount() == 2,
                "disabled histogram should not be exported");
  manager.EndRun();

  if (!okay) return 1;
  std::printf("HistogramPdfWriter tests passed: 2 enabled pages\n");
  return 0;
}
