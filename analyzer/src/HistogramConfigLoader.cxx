#include "HistogramConfigLoader.h"

#include "mvodb.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace ana {
namespace {

using OdbPtr = std::unique_ptr<MVOdb>;
constexpr const char* kOdbPathFromRoot = "Analyzer/Histograms";

OdbPtr FindDirectory(MVOdb* odb, const char* path) {
  if (!odb) return {};
  const bool print_errors = odb->GetPrintError();
  if (!odb->IsReadOnly()) odb->SetPrintError(false);
  MVOdbError error;
  const char* relative_path = path[0] == '/' ? path + 1 : path;
  OdbPtr directory(odb->Chdir(relative_path, false, &error));
  if (!odb->IsReadOnly()) odb->SetPrintError(print_errors);
  return directory;
}

bool ReportReadError(const char* subkey, const char* field,
                     const MVOdbError& error) {
  if (!error.fError) return true;
  std::fprintf(stderr,
               "WARNING: histogram ODB subkey \"%s\": cannot read %s: %s\n",
               subkey, field, error.fErrorString.c_str());
  return false;
}

bool ReadConfig(MVOdb* directory, const std::string& subkey,
                HistogramConfig* config) {
  OdbPtr histogram(directory->Chdir(subkey.c_str(), false));
  if (!histogram) {
    std::fprintf(stderr,
                 "WARNING: ODB entry \"%s/%s\" skipped: expected a "
                 "subdirectory\n",
                 HistogramConfigLoader::kOdbPath, subkey.c_str());
    return false;
  }

  if (!histogram->IsReadOnly()) histogram->SetPrintError(false);
  bool okay = true;
  MVOdbError error;
  histogram->RS("HistName", &config->hist_name, false, 0, &error);
  okay &= ReportReadError(subkey.c_str(), "HistName", error);
  error = MVOdbError{};
  histogram->RS("Type", &config->type, false, 0, &error);
  okay &= ReportReadError(subkey.c_str(), "Type", error);
  error = MVOdbError{};
  histogram->RS("Expression", &config->expression, false, 0, &error);
  okay &= ReportReadError(subkey.c_str(), "Expression", error);
  error = MVOdbError{};
  histogram->RI("Bins", &config->bins, false, &error);
  okay &= ReportReadError(subkey.c_str(), "Bins", error);
  error = MVOdbError{};
  histogram->RD("Min", &config->min, false, &error);
  okay &= ReportReadError(subkey.c_str(), "Min", error);
  error = MVOdbError{};
  histogram->RD("Max", &config->max, false, &error);
  okay &= ReportReadError(subkey.c_str(), "Max", error);
  error = MVOdbError{};
  histogram->RS("Cut", &config->cut, false, 0, &error);
  okay &= ReportReadError(subkey.c_str(), "Cut", error);
  error = MVOdbError{};
  histogram->RB("Enabled", &config->enabled, false, &error);
  okay &= ReportReadError(subkey.c_str(), "Enabled", error);
  return okay;
}

bool CreateConfig(MVOdb* directory, const HistogramConfig& config) {
  MVOdbError error;
  OdbPtr histogram(directory->Chdir(config.hist_name.c_str(), true, &error));
  if (!histogram || error.fError) return false;

  std::string hist_name = config.hist_name;
  std::string type = config.type;
  std::string expression = config.expression;
  int bins = config.bins;
  double min = config.min;
  double max = config.max;
  std::string cut = config.cut;
  bool enabled = config.enabled;

  histogram->RS("HistName", &hist_name, true, 64, &error);
  if (error.fError) return false;
  histogram->RS("Type", &type, true, 32, &error);
  if (error.fError) return false;
  histogram->RS("Expression", &expression, true, 128, &error);
  if (error.fError) return false;
  histogram->RI("Bins", &bins, true, &error);
  if (error.fError) return false;
  histogram->RD("Min", &min, true, &error);
  if (error.fError) return false;
  histogram->RD("Max", &max, true, &error);
  if (error.fError) return false;
  histogram->RS("Cut", &cut, true, 128, &error);
  if (error.fError) return false;
  histogram->RB("Enabled", &enabled, true, &error);
  return !error.fError;
}

}  // namespace

HistogramConfigLoader::Result HistogramConfigLoader::Load(
    MVOdb* odb, bool create_defaults_if_missing) const {
  Result result;
  OdbPtr directory = FindDirectory(odb, kOdbPath);

  if (!directory && create_defaults_if_missing && odb && !odb->IsReadOnly()) {
    result.created_defaults = CreateDefaults(odb);
    if (!result.created_defaults) {
      result.configs = DefaultHistogramConfigs();
      return result;
    }
    directory = FindDirectory(odb, kOdbPath);
  }

  if (!directory) {
    result.configs = DefaultHistogramConfigs();
    return result;
  }

  if (!directory->IsReadOnly()) directory->SetPrintError(false);
  std::vector<std::string> names;
  MVOdbError error;
  directory->ReadDir(&names, nullptr, nullptr, nullptr, nullptr, &error);
  if (error.fError) {
    std::fprintf(stderr,
                 "WARNING: cannot read histogram ODB directory %s: %s; "
                 "using default configuration\n",
                 kOdbPath, error.fErrorString.c_str());
    result.configs = DefaultHistogramConfigs();
    return result;
  }
  // The installed XML/JSON MVOdb backends used for offline BOR snapshots do
  // not implement ReadDir(). An empty read-only result is therefore treated
  // as unavailable configuration. Live MidasOdb implements ReadDir(), and an
  // intentionally empty writable directory remains an empty configuration.
  if (names.empty() && directory->IsReadOnly()) {
    result.configs = DefaultHistogramConfigs();
    return result;
  }

  for (const auto& name : names) {
    HistogramConfig config;
    if (ReadConfig(directory.get(), name, &config))
      result.configs.push_back(std::move(config));
  }
  result.loaded_from_odb = true;
  return result;
}

bool HistogramConfigLoader::CreateDefaults(MVOdb* odb) const {
  MVOdbError error;
  OdbPtr directory(odb->Chdir(kOdbPathFromRoot, true, &error));
  if (!directory || error.fError) {
    std::fprintf(stderr,
                 "WARNING: cannot create histogram ODB directory %s: %s\n",
                 kOdbPath, error.fErrorString.c_str());
    return false;
  }

  for (const auto& config : DefaultHistogramConfigs()) {
    if (!CreateConfig(directory.get(), config)) {
      std::fprintf(stderr,
                   "WARNING: cannot create default histogram ODB subkey "
                   "\"%s\"\n",
                   config.hist_name.c_str());
      return false;
    }
  }
  std::printf("HistogramConfigLoader: created default configuration at %s\n",
              kOdbPath);
  return true;
}

}  // namespace ana
