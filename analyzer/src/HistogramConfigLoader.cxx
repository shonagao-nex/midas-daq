#include "HistogramConfigLoader.h"
#include "mvodb.h"

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ana {
namespace {
using OdbPtr = std::unique_ptr<MVOdb>;
constexpr const char* kPath = "Analyzer/Histograms";

OdbPtr FindDirectory(MVOdb* odb) {
  if (!odb) return {};
  const bool print = odb->GetPrintError();
  if (!odb->IsReadOnly()) odb->SetPrintError(false);
  MVOdbError error;
  OdbPtr result(odb->Chdir(kPath, false, &error));
  if (!odb->IsReadOnly()) odb->SetPrintError(print);
  return result;
}

struct Arrays {
  std::vector<std::string> hist_name, title, x_title, y_title, type;
  std::vector<std::string> expression, cut;
  std::vector<int> bins;
  std::vector<double> min, max;
  std::vector<bool> enabled;

  void Add(const HistogramConfig& c) {
    hist_name.push_back(c.hist_name);
    title.push_back(c.title);
    x_title.push_back(c.x_title);
    y_title.push_back(c.y_title);
    type.push_back(c.type);
    expression.push_back(c.expression);
    bins.push_back(c.bins);
    min.push_back(c.min);
    max.push_back(c.max);
    cut.push_back(c.cut);
    enabled.push_back(c.enabled);
  }
  bool SameSize() const {
    const auto n = hist_name.size();
    return n && title.size() == n && x_title.size() == n &&
           y_title.size() == n && type.size() == n &&
           expression.size() == n && bins.size() == n && min.size() == n &&
           max.size() == n && cut.size() == n && enabled.size() == n;
  }
};

bool ReadGroup(MVOdb* odb, Arrays* a) {
  MVOdbError error;
#define READ_STRING(key, field)                           \
  odb->RSA(key, &a->field, false, 0, 0, &error);            \
  if (error.fError) return false;
#define READ_ARRAY(method, key, field)                    \
  odb->method(key, &a->field, false, 0, &error);            \
  if (error.fError) return false;
  READ_STRING("HistName", hist_name)
  READ_STRING("Title", title)
  READ_STRING("XTitle", x_title)
  READ_STRING("YTitle", y_title)
  READ_STRING("Type", type)
  READ_STRING("Expression", expression)
  READ_ARRAY(RIA, "Bins", bins)
  READ_ARRAY(RDA, "Min", min)
  READ_ARRAY(RDA, "Max", max)
  READ_STRING("Cut", cut)
  READ_ARRAY(RBA, "Enabled", enabled)
#undef READ_STRING
#undef READ_ARRAY
  return a->SameSize();
}

bool WriteGroup(MVOdb* odb, const Arrays& a) {
  MVOdbError error;
#define WRITE_STRING(key, field, length)                  \
  odb->WSA(key, a.field, length, &error);                  \
  if (error.fError) return false;
#define WRITE_ARRAY(method, key, field)                   \
  odb->method(key, a.field, &error);                       \
  if (error.fError) return false;
  WRITE_STRING("HistName", hist_name, 64)
  WRITE_STRING("Title", title, 128)
  WRITE_STRING("XTitle", x_title, 128)
  WRITE_STRING("YTitle", y_title, 128)
  WRITE_STRING("Type", type, 32)
  WRITE_STRING("Expression", expression, 128)
  WRITE_ARRAY(WIA, "Bins", bins)
  WRITE_ARRAY(WDA, "Min", min)
  WRITE_ARRAY(WDA, "Max", max)
  WRITE_STRING("Cut", cut, 128)
  WRITE_ARRAY(WBA, "Enabled", enabled)
#undef WRITE_STRING
#undef WRITE_ARRAY
  return true;
}

HistogramConfig At(const Arrays& a, const std::string& group, size_t i) {
  char slot[32];
  std::snprintf(slot, sizeof(slot), "Ch%02zu", i);
  return {a.hist_name[i], a.type[i], a.expression[i], a.bins[i],
          a.min[i], a.max[i], a.cut[i], a.enabled[i], a.title[i],
          a.x_title[i], a.y_title[i], group, slot};
}
}  // namespace

HistogramConfigLoader::Result HistogramConfigLoader::Load(MVOdb* odb) const {
  Result result;
  OdbPtr directory = FindDirectory(odb);
  if (!directory) {
    result.configs = DefaultHistogramConfigs();
    return result;
  }
  result.odb_path_found = true;
  if (!directory->IsReadOnly()) directory->SetPrintError(false);
  std::vector<std::string> groups;
  MVOdbError error;
  directory->ReadDir(&groups, nullptr, nullptr, nullptr, nullptr, &error);
  if (error.fError || (groups.empty() && directory->IsReadOnly())) {
    result.configs = DefaultHistogramConfigs();
    return result;
  }
  for (const auto& group : groups) {
    OdbPtr group_directory(directory->Chdir(group.c_str(), false));
    if (!group_directory) continue;
    if (!group_directory->IsReadOnly()) group_directory->SetPrintError(false);
    Arrays arrays;
    if (!ReadGroup(group_directory.get(), &arrays)) {
      std::fprintf(stderr,
                   "WARNING: histogram ODB group %s/%s has missing or "
                   "mismatched arrays; using in-memory defaults\n",
                   kOdbPath, group.c_str());
      result.configs = DefaultHistogramConfigs();
      return result;
    }
    for (size_t i = 0; i < arrays.hist_name.size(); ++i)
      result.configs.push_back(At(arrays, group, i));
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
  if (FindDirectory(odb)) {
    std::fprintf(stderr, "ERROR: %s already exists. Nothing was modified.\n",
                 kOdbPath);
    return false;
  }
  MVOdbError error;
  OdbPtr directory(odb->Chdir(kPath, true, &error));
  if (!directory || error.fError) return false;
  std::map<std::string, Arrays> groups;
  for (const auto& config : DefaultHistogramConfigs())
    groups[config.group].Add(config);
  for (const auto& [name, arrays] : groups) {
    OdbPtr group(directory->Chdir(name.c_str(), true, &error));
    if (!group || error.fError || !WriteGroup(group.get(), arrays)) {
      std::fprintf(stderr, "WARNING: cannot create histogram ODB group %s/%s\n",
                   kOdbPath, name.c_str());
      return false;
    }
  }
  return true;
}
}  // namespace ana
