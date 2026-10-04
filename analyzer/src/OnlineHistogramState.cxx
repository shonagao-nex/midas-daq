#include "OnlineHistogramState.h"

#include "TDirectory.h"
#include "mvodb.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <memory>
#include <utility>

namespace ana {

bool OnlineHistogramState::Initialize(
    TDirectory* parent, std::vector<HistogramConfig> histogram_configs,
    std::vector<PageConfig> page_configs) {
  // Canvases contain non-owning pointers to histograms. Remove them first.
  pages.ClearCanvases();
  histograms.SetConfigs(std::move(histogram_configs));
  const bool booked = histograms.BeginRun(parent, false);
  const bool page_directory_ready = pages.BeginRun(parent, &histograms);
  pages.ApplyConfigs(std::move(page_configs), &histograms);
  initialized_ = booked && page_directory_ready;
  return initialized_;
}

bool OnlineHistogramState::BeginRun(
    TDirectory* parent, std::vector<HistogramConfig> histogram_configs,
    std::vector<PageConfig> page_configs, int run_number) {
  if (!initialized_) {
    if (!Initialize(parent, std::move(histogram_configs),
                    std::move(page_configs)))
      return false;
  } else {
    const bool histogram_changed =
        !histograms.ConfigsMatch(histogram_configs);
    if (histogram_changed) {
      pages.ClearCanvases();
      histograms.ApplyConfigs(std::move(histogram_configs));
    }
    const bool pages_changed =
        pages.ApplyConfigs(std::move(page_configs), &histograms);
    if (histogram_changed && !pages_changed) pages.Rebuild(&histograms);
  }
  histograms.Reset();
  last_run_number_ = run_number;
  has_run_ = true;
  return true;
}

void OnlineHistogramState::EndRun() {
  // No Fill calls occur after manalyzer destroys the run object at EOR.
}

void OnlineHistogramState::Clear() {
  pages.Clear();
  histograms.EndRun();
  has_run_ = false;
  initialized_ = false;
}

bool OnlineHistogramState::ExportPdf(const std::string& output_path,
                                     std::string* error) const {
  return pdf_writer_.Write(histograms, last_run_number_, output_path, error);
}

void OnlineHistogramState::PollPdfRequest(MVOdb* odb) {
  if (!odb || !has_run_) return;
  std::unique_ptr<MVOdb> request_directory(
      odb->Chdir("Analyzer/HistogramPdf", false));
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

  const char* home = std::getenv("HOME");
  const std::filesystem::path plot_directory =
      std::filesystem::path(home ? home : "") / "midas/midas/plots";
  if (output_file.empty()) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &local);
    output_file =
        (plot_directory /
         ("run" + std::to_string(last_run_number_) + "_" + timestamp +
          ".pdf"))
            .string();
  } else if (!std::filesystem::path(output_file).is_absolute()) {
    output_file = (plot_directory / output_file).string();
  }

  std::string write_error;
  if (ExportPdf(output_file, &write_error))
    std::printf("HistogramPdfWriter: wrote %s\n", output_file.c_str());

  // Acknowledge an explicit request even on failure to avoid repeated output.
  request_directory->WB("Request", false);
}

}  // namespace ana
