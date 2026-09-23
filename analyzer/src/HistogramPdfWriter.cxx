#include "HistogramPdfWriter.h"

#include "HistogramManager.h"

#include "TCanvas.h"
#include "TH1.h"
#include "TLatex.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>

namespace ana {
namespace {

std::string Timestamp() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_r(&now, &local);
  char text[64];
  std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S %Z", &local);
  return text;
}

bool Fail(const std::string& message, std::string* error) {
  if (error) *error = message;
  std::fprintf(stderr, "ERROR: HistogramPdfWriter: %s\n", message.c_str());
  return false;
}

}  // namespace

bool HistogramPdfWriter::Write(const HistogramManager& manager,
                               int run_number,
                               const std::string& output_path,
                               std::string* error) const {
  auto histograms = manager.Snapshot();
  if (histograms.empty())
    return Fail("no enabled histograms to export", error);

  std::error_code filesystem_error;
  const std::filesystem::path path(output_path);
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), filesystem_error);
    if (filesystem_error)
      return Fail("cannot create output directory: " +
                      filesystem_error.message(),
                  error);
  }

  TCanvas canvas("histogram_pdf_canvas", "Histogram PDF", 1000, 750);
  const std::string open = output_path + "[";
  const std::string close = output_path + "]";
  canvas.Print(open.c_str());

  const std::string timestamp = Timestamp();
  for (const auto& histogram : histograms) {
    canvas.Clear();
    const std::string title = "Run " + std::to_string(run_number) + " - " +
                              histogram->GetName();
    histogram->SetTitle(title.c_str());
    histogram->Draw("HIST");

    TLatex label;
    label.SetNDC(true);
    label.SetTextSize(0.025);
    label.DrawLatex(0.12, 0.02,
                    ("Generated " + timestamp).c_str());
    canvas.Print(output_path.c_str(), title.c_str());
  }

  canvas.Print(close.c_str());
  if (error) error->clear();
  return true;
}

}  // namespace ana
