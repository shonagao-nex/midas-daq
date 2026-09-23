#include "HistogramConfigLoader.h"
#include "HistogramOdbInitializer.h"

#include "mvodb.h"

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using Value = std::variant<bool, int, double, std::string>;

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
    const std::string slot = config.slot.empty() ? config.hist_name : config.slot;
    Node& histogram = Child(Child(histograms, group), slot);
    histogram.values["HistName"] = config.hist_name;
    histogram.values["Title"] = config.title;
    histogram.values["XTitle"] = config.x_title;
    histogram.values["YTitle"] = config.y_title;
    histogram.values["Type"] = config.type;
    histogram.values["Expression"] = config.expression;
    histogram.values["Bins"] = config.bins;
    histogram.values["Min"] = config.min;
    histogram.values["Max"] = config.max;
    histogram.values["Cut"] = config.cut;
    histogram.values["Enabled"] = config.enabled;
  }

  int CreateOperations() const { return state_->create_operations; }
  int WriteOperations() const { return state_->write_operations; }

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

#define UNUSED_ARRAY_READ(method, type)                                      \
  void method(const char*, std::vector<type>*, bool create, int,             \
              MVOdbError* error) override {                                  \
    CountCreate(create);                                                      \
    SetOk(error);                                                             \
  }
  UNUSED_ARRAY_READ(RBA, bool)
  UNUSED_ARRAY_READ(RIA, int)
  UNUSED_ARRAY_READ(RDA, double)
  UNUSED_ARRAY_READ(RFA, float)
  UNUSED_ARRAY_READ(RU16A, uint16_t)
  UNUSED_ARRAY_READ(RU32A, uint32_t)
  UNUSED_ARRAY_READ(RU64A, uint64_t)
#undef UNUSED_ARRAY_READ
  void RSA(const char*, std::vector<std::string>*, bool create, int, int,
           MVOdbError* error) override {
    CountCreate(create);
    SetOk(error);
  }

#define UNUSED_INDEX_READ(method, type)                                  \
  void method(const char*, int, type*, MVOdbError* error) override {      \
    SetOk(error);                                                         \
  }
  UNUSED_INDEX_READ(RBAI, bool)
  UNUSED_INDEX_READ(RIAI, int)
  UNUSED_INDEX_READ(RDAI, double)
  UNUSED_INDEX_READ(RFAI, float)
  UNUSED_INDEX_READ(RSAI, std::string)
  UNUSED_INDEX_READ(RU16AI, uint16_t)
  UNUSED_INDEX_READ(RU32AI, uint32_t)
  UNUSED_INDEX_READ(RU64AI, uint64_t)
#undef UNUSED_INDEX_READ

#define UNUSED_SCALAR_WRITE(method, type)                               \
  void method(const char*, type, MVOdbError* error) override {           \
    ++state_->write_operations;                                          \
    SetOk(error);                                                        \
  }
  UNUSED_SCALAR_WRITE(WB, bool)
  UNUSED_SCALAR_WRITE(WI, int)
  UNUSED_SCALAR_WRITE(WD, double)
  UNUSED_SCALAR_WRITE(WF, float)
  UNUSED_SCALAR_WRITE(WU16, uint16_t)
  UNUSED_SCALAR_WRITE(WU32, uint32_t)
  UNUSED_SCALAR_WRITE(WU64, uint64_t)
#undef UNUSED_SCALAR_WRITE
  void WS(const char*, const char*, int, MVOdbError* error) override {
    ++state_->write_operations;
    SetOk(error);
  }

#define UNUSED_ARRAY_WRITE(method, type)                                    \
  void method(const char*, const std::vector<type>&, MVOdbError* error)      \
      override {                                                             \
    ++state_->write_operations;                                              \
    SetOk(error);                                                            \
  }
  UNUSED_ARRAY_WRITE(WBA, bool)
  UNUSED_ARRAY_WRITE(WIA, int)
  UNUSED_ARRAY_WRITE(WDA, double)
  UNUSED_ARRAY_WRITE(WFA, float)
  UNUSED_ARRAY_WRITE(WU16A, uint16_t)
  UNUSED_ARRAY_WRITE(WU32A, uint32_t)
  UNUSED_ARRAY_WRITE(WU64A, uint64_t)
#undef UNUSED_ARRAY_WRITE
  void WSA(const char*, const std::vector<std::string>&, int,
           MVOdbError* error) override {
    ++state_->write_operations;
    SetOk(error);
  }

#define UNUSED_INDEX_WRITE(method, type)                                  \
  void method(const char*, int, type, MVOdbError* error) override {        \
    ++state_->write_operations;                                            \
    SetOk(error);                                                          \
  }
  UNUSED_INDEX_WRITE(WBAI, bool)
  UNUSED_INDEX_WRITE(WIAI, int)
  UNUSED_INDEX_WRITE(WDAI, double)
  UNUSED_INDEX_WRITE(WFAI, float)
  UNUSED_INDEX_WRITE(WU16AI, uint16_t)
  UNUSED_INDEX_WRITE(WU32AI, uint32_t)
  UNUSED_INDEX_WRITE(WU64AI, uint64_t)
#undef UNUSED_INDEX_WRITE
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
      {"TTR0", 128}, {"EADC0", 64}, {"ETLE0", 64}, {"ETTR0", 64},
      {"FADC0", 8}};
  std::map<std::string, std::size_t> actual_slots;
  for (const auto& config : defaults) {
    ++actual_slots[config.group];
    okay &= Check(!config.title.empty() && !config.x_title.empty() &&
                      config.y_title == "Counts",
                  "every default slot should have display metadata");
    okay &= Check(config.group == "Event" ? config.enabled : !config.enabled,
                  "only Event should be enabled by default");
  }
  okay &= Check(actual_slots == expected_slots && defaults.size() == 521,
                "default groups should contain all 521 channel slots");
  okay &= Check(initialized.okay,
                "explicit initialization should succeed on an empty ODB");
  okay &= Check(initialized.created == defaults.size() &&
                    initialized.loaded == defaults.size() &&
                    initialized.valid == defaults.size(),
                "explicit initialization should create, load, and validate "
                "every default");
  okay &= Check(empty_for_initialization.CreateOperations() > 0 &&
                    empty_for_initialization.WriteOperations() ==
                        static_cast<int>(defaults.size() * 11),
                "explicit initialization should create all slot fields for "
                "every default");

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

  if (!okay) return 1;
  std::printf("HistogramConfigLoader tests passed\n");
  return 0;
}
