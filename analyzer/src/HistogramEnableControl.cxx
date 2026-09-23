#include "HistogramEnableControl.h"

#include "HistogramConfig.h"
#include "mvodb.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ana {
namespace {

const std::map<std::string, std::size_t>& Counts() {
  static const auto counts = [] {
    std::map<std::string, std::size_t> result;
    for (const auto& config : DefaultHistogramConfigs())
      if (config.group != "Event") ++result[config.group];
    return result;
  }();
  return counts;
}

bool Fail(std::string* error, const std::string& message) {
  if (error) *error = message;
  return false;
}

}  // namespace

const std::vector<std::string>& HistogramEnableControl::GroupNames() {
  static const std::vector<std::string> names{
      "QDC0", "TDC0", "TLE0", "TTR0",
      "EADC0", "ETLE0", "ETTR0", "FADC0"};
  return names;
}

std::size_t HistogramEnableControl::ChannelCount(const std::string& group) {
  auto found = Counts().find(group);
  return found == Counts().end() ? 0 : found->second;
}

bool HistogramEnableControl::Read(MVOdb* odb, const std::string& group,
                                  HistogramEnableGroup* result,
                                  std::string* error) {
  const auto count = ChannelCount(group);
  if (!count || !result) return Fail(error, "unknown histogram group");
  if (!odb) return Fail(error, "ODB is unavailable");

  const bool print_errors = odb->GetPrintError();
  odb->SetPrintError(false);
  MVOdbError odb_error;
  std::unique_ptr<MVOdb> directory(
      odb->Chdir(("Analyzer/Histograms/" + group).c_str(), false,
                 &odb_error));
  odb->SetPrintError(print_errors);
  if (!directory || odb_error.fError)
    return Fail(error, "compact histogram ODB group is absent");
  directory->SetPrintError(false);
  std::vector<bool> enabled;
  directory->RBA("Enabled", &enabled, false, 0, &odb_error);
  if (odb_error.fError || enabled.size() != count)
    return Fail(error, "Enabled[] is absent or has an unexpected length");
  result->name = group;
  result->enabled = std::move(enabled);
  if (error) error->clear();
  return true;
}

bool HistogramEnableControl::Set(MVOdb* odb, const std::string& group,
                                 std::size_t channel, bool enabled,
                                 std::string* error) {
  if (!odb || odb->IsReadOnly())
    return Fail(error, "ODB is not writable");
  if (!ChannelCount(group) || channel >= ChannelCount(group))
    return Fail(error, "unknown group or channel");
  HistogramEnableGroup current;
  if (!Read(odb, group, &current, error)) return false;
  if (current.enabled[channel] == enabled) return true;

  std::unique_ptr<MVOdb> directory(
      odb->Chdir(("Analyzer/Histograms/" + group).c_str(), false));
  if (!directory) return Fail(error, "compact histogram ODB group disappeared");
  directory->SetPrintError(false);
  MVOdbError odb_error;
  directory->WBAI("Enabled", static_cast<int>(channel), enabled, &odb_error);
  if (odb_error.fError) return Fail(error, odb_error.fErrorString);
  bool actual = !enabled;
  directory->RBAI("Enabled", static_cast<int>(channel), &actual, &odb_error);
  if (odb_error.fError || actual != enabled)
    return Fail(error, "Enabled[] write could not be verified");
  if (error) error->clear();
  return true;
}

}  // namespace ana
