#include "HistogramOdbInitializer.h"

#include "HistogramConfig.h"
#include "HistogramConfigLoader.h"
#include "PageConfigLoader.h"
#include "mvodb.h"
#include <memory>

#include <algorithm>
#include <vector>

namespace ana {
namespace {

bool SameConfigs(std::vector<HistogramConfig> left,
                 std::vector<HistogramConfig> right) {
  const auto by_name = [](const HistogramConfig& a, const HistogramConfig& b) {
    return a.hist_name < b.hist_name;
  };
  std::sort(left.begin(), left.end(), by_name);
  std::sort(right.begin(), right.end(), by_name);
  return left == right;
}

}  // namespace

HistogramOdbInitializationResult InitializeHistogramOdb(MVOdb* odb) {
  HistogramOdbInitializationResult result;
  const auto defaults = DefaultHistogramConfigs();
  HistogramConfigLoader loader;

  if (!loader.CreateDefaults(odb)) {
    result.error = "histogram ODB defaults were not created";
    return result;
  }
  result.created = defaults.size();

  const auto loaded = loader.Load(odb);
  if (!loaded.odb_path_found || !loaded.loaded_from_odb) {
    result.error = "created histogram ODB tree could not be read back";
    return result;
  }
  result.loaded = loaded.configs.size();

  const auto validation = ValidateHistogramConfigs(loaded.configs);
  result.valid = validation.configs.size();
  if (validation.skipped != 0 || result.loaded != defaults.size() ||
      result.valid != defaults.size() ||
      !SameConfigs(validation.configs, defaults)) {
    result.error = "read-back histogram configuration does not match defaults";
    return result;
  }

  for (const auto& config : defaults)
    result.histogram_names.push_back(config.hist_name);
  result.okay = true;
  return result;
}

bool InitializeWaveformOdb(MVOdb* odb, std::string* error) {
  const auto fail = [error](const char* message) {
    if (error) *error = message;
    return false;
  };
  if (!odb || odb->IsReadOnly()) return fail("writable ODB required");
  const auto directory = [odb](const char* path) {
    return std::unique_ptr<MVOdb>(odb->Chdir(path, false));
  };
  if (!directory("Analyzer/Histograms") || !directory("Analyzer/Pages"))
    return fail("initialize Histograms and Pages first; nothing was modified");
  if (directory("Analyzer/Histograms/FADC0") ||
      directory("Analyzer/Pages/V1720E_Waveforms"))
    return fail("FADC0 or V1720E_Waveforms already exists; nothing was modified");
  // Check all collisions before writing. Never replace existing user settings.
  const auto loaded = HistogramConfigLoader{}.Load(odb);
  if (!loaded.loaded_from_odb)
    return fail("existing histogram configuration is incomplete; nothing was modified");
  const auto defaults = DefaultWaveformConfigs();
  auto combined = loaded.configs;
  combined.insert(combined.end(), defaults.begin(), defaults.end());
  if (ValidateHistogramConfigs(combined).skipped != 0)
    return fail("histogram validation/collision failed; nothing was modified");
  if (!HistogramConfigLoader{}.CreateWaveformDefaults(odb))
    return fail("cannot write FADC0; inspect ODB for partial initialization");
  std::unique_ptr<MVOdb> page(odb->Chdir("Analyzer/Pages/V1720E_Waveforms", true));
  if (!page) return fail("cannot create waveform page; FADC0 was created");
  MVOdbError write_error;
  page->WI("Rows", 3, &write_error);
  if (write_error.fError) return fail("cannot write page Rows; inspect partial initialization");
  page->WI("Columns", 3, &write_error);
  if (write_error.fError) return fail("cannot write page Columns; inspect partial initialization");
  for (std::size_t i = 0; i < defaults.size(); ++i) {
    char pad[16];
    std::snprintf(pad, sizeof(pad), "Pad%02zu", i + 1);
    const auto path = "FADC0/" + defaults[i].hist_name;
    page->WS(pad, path.c_str(), 64, &write_error);
    if (write_error.fError) return fail("cannot write page pad; inspect partial initialization");
  }
  const auto round_trip = HistogramConfigLoader{}.Load(odb);
  for (const auto& config : defaults) {
    const auto found = std::find_if(round_trip.configs.begin(), round_trip.configs.end(),
        [&](const HistogramConfig& c) { return c.hist_name == config.hist_name; });
    if (found == round_trip.configs.end() || *found != config)
      return fail("FADC0 read-back failed; inspect ODB");
  }
  const auto pages = PageConfigLoader{}.Load(odb);
  const auto found = std::find_if(pages.pages.begin(), pages.pages.end(),
      [](const PageConfig& p) { return p.name == "V1720E_Waveforms"; });
  if (found == pages.pages.end() || found->rows != 3 || found->columns != 3)
    return fail("waveform page read-back failed; inspect ODB");
  for (std::size_t i = 0; i < defaults.size(); ++i)
    if (found->pads[i] != "FADC0/" + defaults[i].hist_name)
      return fail("waveform pad read-back failed; inspect ODB");
  return true;
}

}  // namespace ana
