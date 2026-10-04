#include "HistogramConfigLoader.h"
#include "HistogramOdbInitializer.h"
#include "OnlineHistogramPrescale.h"
#include "PageConfigLoader.h"

#include "mvodb.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using Value = std::variant<bool, int, double, std::string,
                           std::vector<bool>, std::vector<int>,
                           std::vector<double>, std::vector<std::string>>;

struct Node {
  std::map<std::string, std::unique_ptr<Node>> children;
  std::map<std::string, Value> values;
};

struct State {
  Node root;
  int create_operations = 0;
  int write_operations = 0;
};

class TrackingOdb final : public MVOdb {
 public:
  TrackingOdb() : state_(std::make_shared<State>()), node_(&state_->root) {}

  void AddHistogram(const ana::HistogramConfig& config) {
    Node& analyzer = Child(state_->root, "Analyzer");
    Node& histograms = Child(analyzer, "Histograms");
    const std::string group = config.group.empty() ? "Test" : config.group;
    Node& histogram = Child(histograms, group);
    AppendValue(histogram, "HistName", config.hist_name);
    AppendValue(histogram, "Title", config.title);
    AppendValue(histogram, "XTitle", config.x_title);
    AppendValue(histogram, "YTitle", config.y_title);
    AppendValue(histogram, "Type", config.type);
    AppendValue(histogram, "Expression", config.expression);
    AppendValue(histogram, "Bins", config.bins);
    AppendValue(histogram, "Min", config.min);
    AppendValue(histogram, "Max", config.max);
    AppendValue(histogram, "Cut", config.cut);
    AppendValue(histogram, "Enabled", config.enabled);
  }

  int CreateOperations() const { return state_->create_operations; }
  int WriteOperations() const { return state_->write_operations; }
  std::size_t FieldCount() const {
    const auto analyzer = state_->root.children.find("Analyzer");
    if (analyzer == state_->root.children.end()) return 0;
    const auto histograms = analyzer->second->children.find("Histograms");
    if (histograms == analyzer->second->children.end()) return 0;
    std::size_t count = 0;
    for (const auto& [_, group] : histograms->second->children)
      count += group->values.size();
    return count;
  }
  std::size_t GroupCount() const {
    const auto analyzer = state_->root.children.find("Analyzer");
    if (analyzer == state_->root.children.end()) return 0;
    const auto histograms = analyzer->second->children.find("Histograms");
    return histograms == analyzer->second->children.end()
               ? 0 : histograms->second->children.size();
  }
  std::size_t ChannelDirectoryCount() const {
    const auto analyzer = state_->root.children.find("Analyzer");
    if (analyzer == state_->root.children.end()) return 0;
    const auto histograms = analyzer->second->children.find("Histograms");
    if (histograms == analyzer->second->children.end()) return 0;
    std::size_t count = 0;
    for (const auto& [_, group] : histograms->second->children)
      count += group->children.size();
    return count;
  }
  void TruncateField(const std::string& group, const std::string& field) {
    auto& values = Child(Child(Child(state_->root, "Analyzer"),
                               "Histograms"), group).values;
    std::get<std::vector<int>>(values.at(field)).pop_back();
  }
  void SetInteger(const std::string& directory, const std::string& key,
                  int value) {
    Node* node = &state_->root;
    std::size_t begin = 0;
    while (begin < directory.size()) {
      const std::size_t end = directory.find('/', begin);
      node = &Child(*node, directory.substr(begin, end - begin));
      if (end == std::string::npos) break;
      begin = end + 1;
    }
    node->values[key] = value;
    ++state_->write_operations;
  }

  bool IsReadOnly() const override { return false; }

  MVOdb* Chdir(const char* path, bool create, MVOdbError* error) override {
    SetOk(error);
    if (create) ++state_->create_operations;
    Node* current = node_;
    std::string remaining = path ? path : "";
    std::size_t begin = 0;
    while (begin < remaining.size()) {
      const std::size_t end = remaining.find('/', begin);
      const std::string part = remaining.substr(begin, end - begin);
      if (!part.empty()) {
        auto found = current->children.find(part);
        if (found == current->children.end()) {
          if (!create) return nullptr;
          found = current->children
                      .emplace(part, std::make_unique<Node>())
                      .first;
        }
        current = found->second.get();
      }
      if (end == std::string::npos) break;
      begin = end + 1;
    }
    return new TrackingOdb(state_, current);
  }

  void ReadKey(const char*, int*, int*, int*, int*, MVOdbError* error) override {
    SetOk(error);
  }
  void ReadKeyLastWritten(const char*, int*, MVOdbError* error) override {
    SetOk(error);
  }
  void ReadDir(std::vector<std::string>* names, std::vector<int>*,
               std::vector<int>*, std::vector<int>*, std::vector<int>*,
               MVOdbError* error) override {
    SetOk(error);
    if (!names) return;
    names->clear();
    for (const auto& entry : node_->children) names->push_back(entry.first);
  }

  void RB(const char* name, bool* value, bool create,
          MVOdbError* error) override {
    ReadValue(name, value, create, error);
  }
  void RI(const char* name, int* value, bool create,
          MVOdbError* error) override {
    ReadValue(name, value, create, error);
  }
  void RD(const char* name, double* value, bool create,
          MVOdbError* error) override {
    ReadValue(name, value, create, error);
  }
  void RS(const char* name, std::string* value, bool create, int,
          MVOdbError* error) override {
    ReadValue(name, value, create, error);
  }

#define UNUSED_SCALAR_READ(method, type)                                      \
  void method(const char*, type*, bool create, MVOdbError* error) override {  \
    CountCreate(create);                                                       \
    SetOk(error);                                                              \
  }
  UNUSED_SCALAR_READ(RF, float)
  UNUSED_SCALAR_READ(RU16, uint16_t)
  UNUSED_SCALAR_READ(RU32, uint32_t)
  UNUSED_SCALAR_READ(RU64, uint64_t)
#undef UNUSED_SCALAR_READ

#define STORED_ARRAY_READ(method, type)                                      \
  void method(const char* name, std::vector<type>* value, bool create, int,  \
              MVOdbError* error) override {                                  \
    ReadValue(name, value, create, error);                                    \
  }
  STORED_ARRAY_READ(RBA, bool)
  STORED_ARRAY_READ(RIA, int)
  STORED_ARRAY_READ(RDA, double)
#undef STORED_ARRAY_READ
#define UNUSED_ARRAY_READ(method, type)                                      \
  void method(const char*, std::vector<type>*, bool create, int,             \
              MVOdbError* error) override {                                  \
    CountCreate(create);                                                      \
    SetOk(error);                                                             \
  }
  UNUSED_ARRAY_READ(RFA, float)
  UNUSED_ARRAY_READ(RU16A, uint16_t)
  UNUSED_ARRAY_READ(RU32A, uint32_t)
  UNUSED_ARRAY_READ(RU64A, uint64_t)
#undef UNUSED_ARRAY_READ
  void RSA(const char* name, std::vector<std::string>* value, bool create, int, int,
           MVOdbError* error) override {
    ReadValue(name, value, create, error);
  }

#define UNUSED_INDEX_READ(method, type)                                  \
  void method(const char*, int, type*, MVOdbError* error) override {      \
    SetOk(error);                                                         \
  }
  UNUSED_INDEX_READ(RIAI, int)
  UNUSED_INDEX_READ(RDAI, double)
  UNUSED_INDEX_READ(RFAI, float)
  UNUSED_INDEX_READ(RSAI, std::string)
  UNUSED_INDEX_READ(RU16AI, uint16_t)
  UNUSED_INDEX_READ(RU32AI, uint32_t)
  UNUSED_INDEX_READ(RU64AI, uint64_t)
#undef UNUSED_INDEX_READ
  void RBAI(const char* name, int index, bool* value,
            MVOdbError* error) override {
    SetOk(error);
    auto found = node_->values.find(name);
    if (found == node_->values.end() || index < 0) return;
    const auto* values = std::get_if<std::vector<bool>>(&found->second);
    if (values && static_cast<std::size_t>(index) < values->size() && value)
      *value = (*values)[index];
  }

#define UNUSED_SCALAR_WRITE(method, type)                               \
  void method(const char*, type, MVOdbError* error) override {           \
    ++state_->write_operations;                                          \
    SetOk(error);                                                        \
  }
  UNUSED_SCALAR_WRITE(WB, bool)
  UNUSED_SCALAR_WRITE(WD, double)
  UNUSED_SCALAR_WRITE(WF, float)
  UNUSED_SCALAR_WRITE(WU16, uint16_t)
  UNUSED_SCALAR_WRITE(WU32, uint32_t)
  UNUSED_SCALAR_WRITE(WU64, uint64_t)
#undef UNUSED_SCALAR_WRITE
  void WI(const char* name, int value, MVOdbError* error) override {
    node_->values[name] = value;
    ++state_->write_operations;
    SetOk(error);
  }
  void WS(const char* name, const char* value, int,
          MVOdbError* error) override {
    node_->values[name] = std::string(value);
    ++state_->write_operations;
    SetOk(error);
  }

#define STORED_ARRAY_WRITE(method, type)                                     \
  void method(const char* name, const std::vector<type>& value,              \
              MVOdbError* error) override {                                  \
    WriteValue(name, value, error);                                          \
  }
  STORED_ARRAY_WRITE(WBA, bool)
  STORED_ARRAY_WRITE(WIA, int)
  STORED_ARRAY_WRITE(WDA, double)
#undef STORED_ARRAY_WRITE
#define UNUSED_ARRAY_WRITE(method, type)                                    \
  void method(const char*, const std::vector<type>&, MVOdbError* error)      \
      override {                                                             \
    ++state_->write_operations;                                              \
    SetOk(error);                                                            \
  }
  UNUSED_ARRAY_WRITE(WFA, float)
  UNUSED_ARRAY_WRITE(WU16A, uint16_t)
  UNUSED_ARRAY_WRITE(WU32A, uint32_t)
  UNUSED_ARRAY_WRITE(WU64A, uint64_t)
#undef UNUSED_ARRAY_WRITE
  void WSA(const char* name, const std::vector<std::string>& value, int,
           MVOdbError* error) override {
    WriteValue(name, value, error);
  }

#define UNUSED_INDEX_WRITE(method, type)                                  \
  void method(const char*, int, type, MVOdbError* error) override {        \
    ++state_->write_operations;                                            \
    SetOk(error);                                                          \
  }
  UNUSED_INDEX_WRITE(WIAI, int)
  UNUSED_INDEX_WRITE(WDAI, double)
  UNUSED_INDEX_WRITE(WFAI, float)
  UNUSED_INDEX_WRITE(WU16AI, uint16_t)
  UNUSED_INDEX_WRITE(WU32AI, uint32_t)
  UNUSED_INDEX_WRITE(WU64AI, uint64_t)
#undef UNUSED_INDEX_WRITE
  void WBAI(const char* name, int index, bool value,
             MVOdbError* error) override {
    SetOk(error);
    auto found = node_->values.find(name);
    if (found == node_->values.end() || index < 0) return;
    auto* values = std::get_if<std::vector<bool>>(&found->second);
    if (!values || static_cast<std::size_t>(index) >= values->size()) return;
    (*values)[index] = value;
    ++state_->write_operations;
  }
  void WSAI(const char*, int, const char*, MVOdbError* error) override {
    ++state_->write_operations;
    SetOk(error);
  }

  void Delete(const char*, MVOdbError* error) override {
    ++state_->write_operations;
    SetOk(error);
  }
  void SetPrintError(bool value) override { print_errors_ = value; }
  bool GetPrintError() const override { return print_errors_; }

 private:
  TrackingOdb(std::shared_ptr<State> state, Node* node)
      : state_(std::move(state)), node_(node) {}

  static Node& Child(Node& parent, const std::string& name) {
    auto& child = parent.children[name];
    if (!child) child = std::make_unique<Node>();
    return *child;
  }

  template <typename T>
  static void AppendValue(Node& node, const std::string& name, T value) {
    auto found = node.values.find(name);
    if (found == node.values.end())
      found = node.values.emplace(name, std::vector<T>{}).first;
    std::get<std::vector<T>>(found->second).push_back(value);
  }

  template <typename T>
  void WriteValue(const char* name, const std::vector<T>& value,
                  MVOdbError* error) {
    node_->values[name] = value;
    ++state_->write_operations;
    SetOk(error);
  }

  void CountCreate(bool create) {
    if (create) ++state_->create_operations;
  }

  template <typename T>
  void ReadValue(const char* name, T* value, bool create,
                 MVOdbError* error) {
    CountCreate(create);
    SetOk(error);
    const std::string key = name ? name : "";
    auto found = node_->values.find(key);
    if (found == node_->values.end() && create && value) {
      found = node_->values.emplace(key, *value).first;
      ++state_->write_operations;
    }
    if (found == node_->values.end() || !value) return;
    if (const T* stored = std::get_if<T>(&found->second)) *value = *stored;
  }

  std::shared_ptr<State> state_;
  Node* node_ = nullptr;
  bool print_errors_ = true;
};

bool Check(bool condition, const char* message) {
  if (condition) return true;
  std::fprintf(stderr, "FAIL: %s\n", message);
  return false;
}

}  // namespace

int main() {
  bool okay = true;
  ana::HistogramConfigLoader loader;

  TrackingOdb missing;
  const auto missing_result = loader.Load(&missing);
  okay &= Check(!missing_result.odb_path_found,
                "missing path should be reported as absent");
  okay &= Check(!missing_result.loaded_from_odb,
                "missing path should not report an ODB load");
  okay &= Check(missing_result.configs == ana::DefaultHistogramConfigs(),
                "missing path should use in-memory defaults");
  okay &= Check(missing.CreateOperations() == 0,
                "normal load must perform zero create operations");
  okay &= Check(missing.WriteOperations() == 0,
                "normal load must perform zero write operations");
  TrackingOdb empty_for_initialization;
  const auto initialized =
      ana::InitializeHistogramOdb(&empty_for_initialization);
  const auto defaults = ana::DefaultHistogramConfigs();
  const std::map<std::string, std::size_t> expected_slots{
      {"Event", 1},  {"QDC0", 32},  {"TDC0", 32},  {"TLE0", 128},
      {"TTR0", 128}, {"EADC0", 64}, {"ETLE0", 64}, {"ETTR0", 64}};
  std::map<std::string, std::size_t> actual_slots;
  for (const auto& config : defaults) {
    ++actual_slots[config.group];
    okay &= Check(!config.title.empty() && !config.x_title.empty() &&
                      config.y_title == "Counts",
                  "every default slot should have display metadata");
    okay &= Check(config.enabled,
                  "every histogram should be enabled by default");
  }
  okay &= Check(actual_slots == expected_slots && defaults.size() == 513,
                "default groups should contain all 513 channel slots");
  for (const auto& config : defaults) {
    const bool event = config.group == "Event";
    const bool time = config.group == "TLE0" || config.group == "TTR0" ||
                      config.group == "ETLE0" || config.group == "ETTR0";
    okay &= Check(config.bins == (event ? 400 : 512) && config.min == 0.0 &&
                      config.max == (event ? 400.0
                                         : time ? 1048576.0 : 4096.0),
                  "default bin count and axis range must match the group");
  }
  okay &= Check(initialized.okay,
                "explicit initialization should succeed on an empty ODB");
  okay &= Check(initialized.created == defaults.size() &&
                    initialized.loaded == defaults.size() &&
                    initialized.valid == defaults.size(),
                "explicit initialization should create, load, and validate "
                "every default");
  okay &= Check(empty_for_initialization.CreateOperations() > 0 &&
                    empty_for_initialization.WriteOperations() ==
                        static_cast<int>(expected_slots.size() * 11),
                "explicit initialization should write 11 arrays per group");
  okay &= Check(empty_for_initialization.GroupCount() == 8 &&
                    empty_for_initialization.FieldCount() == 88 &&
                    empty_for_initialization.ChannelDirectoryCount() == 0 &&
                    empty_for_initialization.FieldCount() <
                        defaults.size() * 11 / 10,
                "compact schema should have 8 groups and only 88 fields");
  const auto round_trip = loader.Load(&empty_for_initialization);
  auto actual_configs = round_trip.configs;
  auto expected_configs = defaults;
  const auto by_name = [](const ana::HistogramConfig& a,
                          const ana::HistogramConfig& b) {
    return a.hist_name < b.hist_name;
  };
  std::sort(actual_configs.begin(), actual_configs.end(), by_name);
  std::sort(expected_configs.begin(), expected_configs.end(), by_name);
  okay &= Check(actual_configs == expected_configs,
                "create then load should preserve all 513 configs");

  std::unique_ptr<MVOdb> analyzer_odb(
      empty_for_initialization.Chdir("Analyzer/Histograms/QDC0", false, nullptr));
  okay &= Check(static_cast<bool>(analyzer_odb),
                "initialized QDC0 group should exist");
  if (analyzer_odb) {
    MVOdbError error;
    analyzer_odb->WBAI("Enabled", 5, false, &error);
    const auto disabled_result = loader.Load(&empty_for_initialization);
    const auto changed = std::find_if(
        disabled_result.configs.begin(), disabled_result.configs.end(),
        [](const ana::HistogramConfig& config) {
          return config.group == "QDC0" && config.slot == "Ch05";
        });
    okay &= Check(changed != disabled_result.configs.end() &&
                      !changed->enabled && changed->title == "QDC0 Ch.5",
                  "ODB Enabled false should load without changing metadata");
    analyzer_odb->WBAI("Enabled", 5, true, &error);
    auto restored = loader.Load(&empty_for_initialization).configs;
    std::sort(restored.begin(), restored.end(), by_name);
    okay &= Check(restored == expected_configs,
                  "ODB Enabled true should restore the default configuration");
  }

  empty_for_initialization.TruncateField("QDC0", "Bins");
  const auto malformed_result = loader.Load(&empty_for_initialization);
  okay &= Check(!malformed_result.loaded_from_odb &&
                    malformed_result.configs == defaults,
                "a mismatched group array should use defaults without writes");
  okay &= Check(empty_for_initialization.WriteOperations() == 90,
                "malformed group load must remain read-only");

  TrackingOdb existing;
  const ana::HistogramConfig first{"from_odb_a", "TH1D", "event", 10,
                                   0.0, 10.0, "", true, "First", "raw",
                                   "Counts", "Test", "Ch00"};
  const ana::HistogramConfig second{"from_odb_b", "TH1D", "qdc0[0]", 20,
                                    1.0, 21.0, "", false, "Second", "raw",
                                    "Counts", "Test", "Ch01"};
  existing.AddHistogram(first);
  existing.AddHistogram(second);
  const auto existing_result = loader.Load(&existing);
  okay &= Check(existing_result.odb_path_found,
                "existing path should be reported as present");
  okay &= Check(existing_result.loaded_from_odb,
                "existing path should load ODB configuration");
  okay &= Check(existing_result.configs.size() == 2,
                "both ODB definitions should load");
  okay &= Check(existing_result.configs.size() == 2 &&
                    existing_result.configs[0] == first &&
                    existing_result.configs[1] == second,
                "loaded ODB definitions should retain their values");
  okay &= Check(existing.CreateOperations() == 0,
                "existing-path load must perform zero create operations");
  okay &= Check(existing.WriteOperations() == 0,
                "existing-path load must perform zero write operations");

  const auto before_rejected_initialization = existing_result.configs;
  const auto rejected_initialization = ana::InitializeHistogramOdb(&existing);
  const auto after_rejected_initialization = loader.Load(&existing);
  okay &= Check(!rejected_initialization.okay,
                "initialization should fail when the tree already exists");
  okay &= Check(existing.CreateOperations() == 0 &&
                    existing.WriteOperations() == 0,
                "existing tree must reject initialization with zero writes");
  okay &= Check(after_rejected_initialization.configs ==
                    before_rejected_initialization,
                "rejected initialization must preserve existing values");

  TrackingOdb prescale_odb;
  okay &= Check(ana::OnlineHistogramPrescale::EnsureOdb(&prescale_odb),
                "online startup should create the integer prescale key");
  std::unique_ptr<MVOdb> prescale_directory(
      prescale_odb.Chdir("Analyzer/OnlineHistogram", false, nullptr));
  int stored_prescale = 0;
  MVOdbError prescale_error;
  if (prescale_directory)
    prescale_directory->RI("FillPrescale", &stored_prescale, false,
                           &prescale_error);
  okay &= Check(prescale_directory && !prescale_error.fError &&
                    stored_prescale == 1 && prescale_odb.WriteOperations() == 1,
                "new FillPrescale should be an integer with default 1");
  okay &= Check(ana::OnlineHistogramPrescale::EnsureOdb(&prescale_odb) &&
                    prescale_odb.WriteOperations() == 1,
                "online startup should preserve an existing prescale value");

  ana::OnlineHistogramPrescale prescale;
  prescale.BeginRun(&prescale_odb);
  const auto selected = [&prescale](ana::AnalyzerMode mode) {
    std::size_t count = 0;
    for (std::size_t event = 1; event <= 17426; ++event)
      if (prescale.ShouldFill(mode, event)) ++count;
    return count;
  };
  okay &= Check(selected(ana::AnalyzerMode::kOnline) == 17426,
                "prescale 1 should fill every online decoded event");
  prescale_odb.SetInteger("Analyzer/OnlineHistogram", "FillPrescale", 2);
  prescale.Poll(&prescale_odb);
  okay &= Check(prescale.EffectiveValue() == 2 &&
                    selected(ana::AnalyzerMode::kOnline) == 8713,
                "live prescale 2 should select half the decoded events");
  prescale_odb.SetInteger("Analyzer/OnlineHistogram", "FillPrescale", 10);
  prescale.Poll(&prescale_odb);
  okay &= Check(prescale.EffectiveValue() == 10 &&
                    selected(ana::AnalyzerMode::kOnline) == 1743 &&
                    selected(ana::AnalyzerMode::kOffline) == 17426,
                "live prescale 10 should select 1743 online events but all offline events");
  prescale_odb.SetInteger("Analyzer/OnlineHistogram", "FillPrescale", 0);
  prescale.Poll(&prescale_odb);
  okay &= Check(prescale.EffectiveValue() == 1 &&
                    selected(ana::AnalyzerMode::kOnline) == 17426,
                "invalid prescale should fall back to 1");
  prescale_odb.SetInteger("Analyzer/OnlineHistogram", "FillPrescale", 10);
  prescale.Poll(&prescale_odb);
  prescale_odb.SetInteger("Analyzer/OnlineHistogram", "FillPrescale", 1);
  prescale.Poll(&prescale_odb);
  okay &= Check(prescale.EffectiveValue() == 1 &&
                    selected(ana::AnalyzerMode::kOnline) == 17426,
                "live 10-to-1 update should restore full histogram fill");

  ana::PageConfigLoader page_loader;
  TrackingOdb page_odb;
  const auto page_fallback = page_loader.Load(&page_odb);
  okay &= Check(!page_fallback.odb_path_found &&
                    page_fallback.pages == ana::DefaultPageConfigs() &&
                    page_odb.CreateOperations() == 0 &&
                    page_odb.WriteOperations() == 0,
                "missing page ODB path must use read-only defaults");
  okay &= Check(page_loader.CreateDefaults(&page_odb),
                "explicit page initialization must create missing tree");
  const auto initialized_pages = page_loader.Load(&page_odb);
  okay &= Check(initialized_pages.odb_path_found &&
                    initialized_pages.loaded_from_odb &&
                    initialized_pages.pages.size() == 4 &&
                    page_odb.WriteOperations() == 4 * 34,
                "four pages must be readable with Rows, Columns, and 32 pads");
  const auto defaults_pages = ana::DefaultPageConfigs();
  for (const auto& expected : defaults_pages) {
    const auto found = std::find_if(
        initialized_pages.pages.begin(), initialized_pages.pages.end(),
        [&](const ana::PageConfig& page) { return page.name == expected.name; });
    okay &= Check(found != initialized_pages.pages.end() && *found == expected,
                  "page ODB round trip must preserve all channel pads");
  }
  const int existing_writes = page_odb.WriteOperations();
  okay &= Check(!page_loader.CreateDefaults(&page_odb) &&
                    page_odb.WriteOperations() == existing_writes &&
                    page_loader.Load(&page_odb).pages ==
                        initialized_pages.pages,
                "existing page tree must reject initialization without writes");

  if (!okay) return 1;
  std::printf("HistogramConfigLoader tests passed\n");
  return 0;
}
