#include "OnlineHistogramPrescale.h"

#include "mvodb.h"

#include <cstdio>
#include <memory>

namespace ana {
namespace {

constexpr const char* kDirectory = "Analyzer/OnlineHistogram";
using Kind = OnlineHistogramPrescale::Kind;

const char* Key(Kind kind) {
  return kind == Kind::kWaveform ? "WaveformPrescale" : "FillPrescale";
}

int DefaultValue(Kind kind) {
  return kind == Kind::kWaveform ? 200 : 1;
}

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

OnlineHistogramPrescale::OnlineHistogramPrescale(Kind kind)
    : kind_(kind), effective_value_(DefaultValue(kind)) {}

bool OnlineHistogramPrescale::EnsureOdb(MVOdb* odb, Kind kind) {
  if (!odb || odb->IsReadOnly()) return false;
  MVOdbError error;
  auto directory = OpenDirectory(odb, true, &error);
  if (!directory || error.fError) return false;
  int value = DefaultValue(kind);
  directory->RI(Key(kind), &value, true, &error);
  return !error.fError;
}

void OnlineHistogramPrescale::BeginRun(MVOdb* odb) {
  effective_value_ = DefaultValue(kind_);
  warned_invalid_ = false;
  warned_read_error_ = false;
  Read(odb, true);
}

void OnlineHistogramPrescale::Poll(MVOdb* odb) { Read(odb, false); }

void OnlineHistogramPrescale::Read(MVOdb* odb, bool announce) {
  MVOdbError error;
  auto directory = OpenDirectory(odb, false, &error);
  int requested = DefaultValue(kind_);
  if (directory && !error.fError)
    directory->RI(Key(kind_), &requested, false, &error);
  if (!directory || error.fError) {
    if (!warned_read_error_) {
      std::fprintf(stderr, "WARNING: cannot read /%s/%s; using %d\n",
                   kDirectory, Key(kind_), DefaultValue(kind_));
      warned_read_error_ = true;
    }
    requested = DefaultValue(kind_);
  } else {
    warned_read_error_ = false;
  }
  Apply(requested, announce);
}

void OnlineHistogramPrescale::Apply(int requested, bool announce) {
  const bool invalid = requested <= 0;
  if (invalid && !warned_invalid_)
    std::fprintf(stderr,
                 "WARNING: /%s/%s=%d is invalid; using %d\n",
                 kDirectory, Key(kind_), requested, DefaultValue(kind_));
  warned_invalid_ = invalid;

  const int effective = invalid ? DefaultValue(kind_) : requested;
  if (announce || effective != effective_value_)
    std::printf("Online histogram %s = %d\n", Key(kind_), effective);
  effective_value_ = effective;
}

bool OnlineHistogramPrescale::ShouldFill(
    AnalyzerMode mode, std::size_t event_count) const {
  if (mode == AnalyzerMode::kOffline) return true;
  return event_count > 0 &&
         (event_count - 1) % static_cast<std::size_t>(effective_value_) == 0;
}

}  // namespace ana
