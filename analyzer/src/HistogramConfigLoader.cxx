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

bool ReadConfig(MVOdb* group_directory, const std::string& group,
                const std::string& slot, HistogramConfig* config) {
  OdbPtr histogram(group_directory->Chdir(slot.c_str(), false));
  if (!histogram) {
    std::fprintf(stderr,
                 "WARNING: ODB entry \"%s/%s/%s\" skipped: expected a "
                 "subdirectory\n",
                 HistogramConfigLoader::kOdbPath, group.c_str(), slot.c_str());
    return false;
  }

  if (!histogram->IsReadOnly()) histogram->SetPrintError(false);
  bool okay = true;
  MVOdbError error;
  histogram->RS("HistName", &config->hist_name, false, 0, &error);
  okay &= ReportReadError(slot.c_str(), "HistName", error);
  error = MVOdbError{};
  histogram->RS("Title", &config->title, false, 0, &error);
  okay &= ReportReadError(slot.c_str(), "Title", error);
  error = MVOdbError{};
  histogram->RS("XTitle", &config->x_title, false, 0, &error);
  okay &= ReportReadError(slot.c_str(), "XTitle", error);
  error = MVOdbError{};
  histogram->RS("YTitle", &config->y_title, false, 0, &error);
  okay &= ReportReadError(slot.c_str(), "YTitle", error);
  error = MVOdbError{};
  histogram->RS("Type", &config->type, false, 0, &error);
  okay &= ReportReadError(slot.c_str(), "Type", error);
  error = MVOdbError{};
  histogram->RS("Expression", &config->expression, false, 0, &error);
  okay &= ReportReadError(slot.c_str(), "Expression", error);
  error = MVOdbError{};
  histogram->RI("Bins", &config->bins, false, &error);
  okay &= ReportReadError(slot.c_str(), "Bins", error);
  error = MVOdbError{};
  histogram->RD("Min", &config->min, false, &error);
  okay &= ReportReadError(slot.c_str(), "Min", error);
  error = MVOdbError{};
  histogram->RD("Max", &config->max, false, &error);
  okay &= ReportReadError(slot.c_str(), "Max", error);
  error = MVOdbError{};
  histogram->RS("Cut", &config->cut, false, 0, &error);
  okay &= ReportReadError(slot.c_str(), "Cut", error);
  error = MVOdbError{};
  histogram->RB("Enabled", &config->enabled, false, &error);
  okay &= ReportReadError(slot.c_str(), "Enabled", error);
  config->group = group;
  config->slot = slot;
  return okay;
}

bool WriteConfig(MVOdb* directory, const HistogramConfig& config) {
  MVOdbError error;
  OdbPtr histogram(directory->Chdir(config.slot.c_str(), true, &error));
  if (!histogram || error.fError) return false;

  std::string hist_name = config.hist_name;
  std::string title = config.title;
  std::string x_title = config.x_title;
  std::string y_title = config.y_title;
  std::string type = config.type;
  std::string expression = config.expression;
  int bins = config.bins;
  double min = config.min;
  double max = config.max;
  std::string cut = config.cut;
  bool enabled = config.enabled;

  histogram->RS("HistName", &hist_name, true, 64, &error);
  if (error.fError) return false;
  histogram->RS("Title", &title, true, 128, &error);
  if (error.fError) return false;
  histogram->RS("XTitle", &x_title, true, 128, &error);
  if (error.fError) return false;
  histogram->RS("YTitle", &y_title, true, 128, &error);
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

HistogramConfigLoader::Result HistogramConfigLoader::Load(MVOdb* odb) const {
  Result result;
  OdbPtr directory = FindDirectory(odb, kOdbPath);

  if (!directory) {
    result.configs = DefaultHistogramConfigs();
    return result;
  }
  result.odb_path_found = true;

  if (!directory->IsReadOnly()) directory->SetPrintError(false);
  std::vector<std::string> groups;
  MVOdbError error;
  directory->ReadDir(&groups, nullptr, nullptr, nullptr, nullptr, &error);
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
  if (groups.empty() && directory->IsReadOnly()) {
    result.configs = DefaultHistogramConfigs();
    return result;
  }

  for (const auto& group : groups) {
    OdbPtr group_directory(directory->Chdir(group.c_str(), false));
    if (!group_directory) {
      std::fprintf(stderr,
                   "WARNING: ODB group \"%s/%s\" skipped: expected a "
                   "subdirectory\n",
                   kOdbPath, group.c_str());
      continue;
    }
    std::vector<std::string> slots;
    error = MVOdbError{};
    group_directory->ReadDir(&slots, nullptr, nullptr, nullptr, nullptr,
                             &error);
    if (error.fError) {
      std::fprintf(stderr,
                   "WARNING: cannot read ODB group \"%s/%s\": %s\n",
                   kOdbPath, group.c_str(), error.fErrorString.c_str());
      continue;
    }
    for (const auto& slot : slots) {
      HistogramConfig config;
      if (ReadConfig(group_directory.get(), group, slot, &config))
        result.configs.push_back(std::move(config));
    }
  }
  result.loaded_from_odb = true;
  return result;
}

bool HistogramConfigLoader::CreateDefaults(MVOdb* odb) const {
  if (!odb || odb->IsReadOnly()) {
    std::fprintf(stderr,
                 "WARNING: explicit histogram ODB initialization requires "
                 "a writable ODB connection\n");
    return false;
  }
  if (FindDirectory(odb, kOdbPath)) {
    std::fprintf(stderr,
                 "ERROR: %s already exists. Nothing was modified.\n",
                 kOdbPath);
    return false;
  }
  MVOdbError error;
  OdbPtr directory(odb->Chdir(kOdbPathFromRoot, true, &error));
  if (!directory || error.fError) {
    std::fprintf(stderr,
                 "WARNING: cannot create histogram ODB directory %s: %s\n",
                 kOdbPath, error.fErrorString.c_str());
    return false;
  }

  for (const auto& config : DefaultHistogramConfigs()) {
    OdbPtr group(directory->Chdir(config.group.c_str(), true, &error));
    if (!group || error.fError || !WriteConfig(group.get(), config)) {
      std::fprintf(stderr,
                   "WARNING: cannot create default histogram ODB subkey "
                   "\"%s/%s/%s\"\n",
                   config.group.c_str(), config.slot.c_str(),
                   config.hist_name.c_str());
      return false;
    }
  }
  return true;
}

}  // namespace ana
