#include "OnlineHistogramPrescale.h"

#include "mvodb.h"

#include <cstdio>
#include <memory>

namespace ana {
namespace {

constexpr const char* kDirectory = "Analyzer/OnlineHistogram";

std::unique_ptr<MVOdb> OpenDirectory(MVOdb* odb, bool create,
                                     MVOdbError* error) {
  if (!odb) return {};
  const bool print_error = odb->GetPrintError();
  odb->SetPrintError(false);
  std::unique_ptr<MVOdb> directory(odb->Chdir(kDirectory, create, error));
  odb->SetPrintError(print_error);
  if (directory) directory->SetPrintError(false);
  return directory;
}

}  // namespace

bool OnlineHistogramPrescale::EnsureOdb(MVOdb* odb) {
  if (!odb || odb->IsReadOnly()) return false;
  MVOdbError error;
  auto directory = OpenDirectory(odb, true, &error);
  if (!directory || error.fError) return false;
  int value = 1;
  directory->RI("FillPrescale", &value, true, &error);
  return !error.fError;
}

void OnlineHistogramPrescale::BeginRun(MVOdb* odb) {
  effective_value_ = 1;
  warned_invalid_ = false;
  warned_read_error_ = false;
  Read(odb, true);
}

void OnlineHistogramPrescale::Poll(MVOdb* odb) { Read(odb, false); }

void OnlineHistogramPrescale::Read(MVOdb* odb, bool announce) {
  MVOdbError error;
  auto directory = OpenDirectory(odb, false, &error);
  int requested = 1;
  if (directory && !error.fError)
    directory->RI("FillPrescale", &requested, false, &error);
  if (!directory || error.fError) {
    if (!warned_read_error_) {
      std::fprintf(stderr,
                   "WARNING: cannot read %s; using 1\n", kOdbPath);
      warned_read_error_ = true;
    }
    requested = 1;
  } else {
    warned_read_error_ = false;
  }
  Apply(requested, announce);
}

void OnlineHistogramPrescale::Apply(int requested, bool announce) {
  const bool invalid = requested <= 0;
  if (invalid && !warned_invalid_)
    std::fprintf(stderr,
                 "WARNING: %s=%d is invalid; using 1\n",
                 kOdbPath, requested);
  warned_invalid_ = invalid;

  const int effective = invalid ? 1 : requested;
  if (announce || effective != effective_value_)
    std::printf("Online histogram FillPrescale = %d\n", effective);
  effective_value_ = effective;
}

bool OnlineHistogramPrescale::ShouldFill(
    AnalyzerMode mode, std::size_t decoded_event_count) const {
  if (mode == AnalyzerMode::kOffline) return true;
  return decoded_event_count > 0 &&
         (decoded_event_count - 1) % static_cast<std::size_t>(effective_value_) == 0;
}

}  // namespace ana
