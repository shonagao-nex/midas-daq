#include "runlog_edit.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using Json = nlohmann::json;
using daq_monitor::RunlogEditCode;
using daq_monitor::RunlogEditRequest;
using daq_monitor::RunlogEditor;
using daq_monitor::RunlogFieldEdit;
namespace fs = std::filesystem;

int checks = 0, failures = 0;
void check(bool condition, const char* expression, int line) {
  ++checks;
  if (!condition) {
    ++failures;
    std::fprintf(stderr, "line %d: %s failed\n", line, expression);
  }
}
#define CHECK(expression) check((expression), #expression, __LINE__)

struct Directory {
  Directory() {
    char name[] = "/tmp/runlog-edit-test.XXXXXX";
    char* created = mkdtemp(name);
    if (!created) std::abort();
    path = created;
  }
  ~Directory() { fs::remove_all(path); }
  fs::path path;
};

Json sample() {
  return {
    {"BOR", {{"Run number", 64}, {"Start time", "Tue Sep 29 21:38:49 2026"},
             {"Type", "Test"}, {"experiment_label", "Development"},
             {"unknown", {{"value", 42}, {"enabled", true}}}}},
    {"EOR", {{"Stop time", "Tue Sep 29 21:39:05 2026"},
             {"Comment", "test"}, {"Duration", "0x0000000000000010"},
             {"VME events", "102"}, {"EASIROC events", "-1"},
             {"HUL events", "-1"},
             {"EventSlipCount", "0x0000000000000000"},
             {"DAQ Status", "OK"}, {"DAQ Summary", "DAQ OK"},
             {"Scaler 64ch", {{"ch00", "0x0000000000000000"},
                              {"ch05", "0x000000000000002A"}}}}},
    {"future_section", {{"valid", true}, {"count", 123}}}
  };
}

fs::path runlog(const Directory& directory, int run = 64) {
  char name[40];
  std::snprintf(name, sizeof(name), "runlog_%06d.json", run);
  return directory.path / name;
}

void write_text(const fs::path& path, const std::string& value) {
  std::ofstream output(path, std::ios::binary);
  output << value;
  if (!output) std::abort();
}

std::string read_text(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_sample(const Directory& directory, const Json& value = sample()) {
  write_text(runlog(directory), value.dump(2) + "\n");
}

RunlogEditor editor(const Directory& directory, int acquiring_run = 0) {
  return RunlogEditor(directory.path,
                      [acquiring_run](std::int64_t run) {
                        return run == acquiring_run;
                      });
}

RunlogEditRequest request(std::vector<RunlogFieldEdit> changes, int run = 64) {
  return {run, std::move(changes)};
}

void check_unchanged_record(const Json& before, const Json& after) {
  Json a = before, b = after;
  for (Json* value : {&a, &b}) {
    (*value)["BOR"].erase("experiment_label");
    (*value)["BOR"].erase("Type");
    (*value)["EOR"].erase("Comment");
  }
  CHECK(a == b);
  CHECK(after["BOR"]["Run number"] == 64);
  CHECK(after["BOR"]["Start time"] == before["BOR"]["Start time"]);
  CHECK(after["EOR"]["Stop time"] == before["EOR"]["Stop time"]);
  CHECK(after["EOR"]["Duration"] == before["EOR"]["Duration"]);
  CHECK(after["EOR"]["VME events"] == before["EOR"]["VME events"]);
  CHECK(after["EOR"]["EASIROC events"] == before["EOR"]["EASIROC events"]);
  CHECK(after["EOR"]["HUL events"] == before["EOR"]["HUL events"]);
  CHECK(after["EOR"]["DAQ Status"] == before["EOR"]["DAQ Status"]);
  CHECK(after["EOR"]["EventSlipCount"] == before["EOR"]["EventSlipCount"]);
  CHECK(after["EOR"]["Scaler 64ch"] == before["EOR"]["Scaler 64ch"]);
}

void test_successes() {
  Directory directory;
  const Json before = sample();
  for (const auto& edit : std::vector<RunlogFieldEdit>{
           {"Experiment", "Development", "KEK beamtime 2026"},
           {"Type", "Test", "Cosmic"},
           {"Comment", "test", "new comment"},
           {"Experiment", "Development", ""},
           {"Comment", "test", ""},
           {"Comment", "test", "first\r\nsecond\rthird"},
           {"Experiment", "Development", "日本語 🧪"}}) {
    write_sample(directory);
    CHECK(editor(directory).edit(request({edit})).ok());
    const Json after = Json::parse(read_text(runlog(directory)));
    check_unchanged_record(before, after);
    if (edit.field == "Experiment")
      CHECK(after["BOR"]["experiment_label"] == edit.value);
    else if (edit.field == "Type") CHECK(after["BOR"]["Type"] == edit.value);
    else CHECK(after["EOR"]["Comment"] ==
               (edit.value == "first\r\nsecond\rthird" ? "first\nsecond\nthird" : edit.value));
  }
  write_sample(directory);
  CHECK(editor(directory).edit(request({
      {"Experiment", "Development", "WC test 2026"},
      {"Type", "Test", "Data"},
      {"Comment", "test", "updated\ncomment"}})).ok());
  const Json all = Json::parse(read_text(runlog(directory)));
  CHECK(all["BOR"]["experiment_label"] == "WC test 2026");
  CHECK(all["BOR"]["Type"] == "Data");
  CHECK(all["EOR"]["Comment"] == "updated\ncomment");
  check_unchanged_record(before, all);
  // Existing Run Summary parser consumes these exact BOR/EOR keys and types.
  CHECK(all["BOR"]["Run number"].is_number_integer());
  CHECK(all["EOR"]["Duration"].is_string());
  CHECK(all["EOR"]["Scaler 64ch"].is_object());
  CHECK((fs::status(runlog(directory)).permissions() & fs::perms::owner_write) !=
        fs::perms::none);
  std::size_t files = 0;
  for (const auto& entry : fs::directory_iterator(directory.path)) {
    CHECK(entry.path().filename() == "runlog_000064.json");
    ++files;
  }
  CHECK(files == 1);
  Json old = sample();
  old["BOR"].erase("experiment_label");
  write_sample(directory, old);
  CHECK(editor(directory).edit(request({{"Experiment", std::nullopt, "added"}})).ok());
  CHECK(Json::parse(read_text(runlog(directory)))["BOR"]["experiment_label"] ==
        "added");
}

void expect_rejected(const Json& contents, const RunlogEditRequest& edit,
                     RunlogEditCode code, int acquiring_run = 0) {
  Directory directory;
  write_sample(directory, contents);
  const std::string before = read_text(runlog(directory));
  const auto actual = editor(directory, acquiring_run).edit(edit);
  CHECK(actual.code == code);
  CHECK(read_text(runlog(directory)) == before);
}

void test_rejections() {
  const auto experiment = request({{"Experiment", "Development", "new"}});
  expect_rejected(sample(), request({{"Type", "Test", "Other"}}),
                  RunlogEditCode::kInvalidValue);
  expect_rejected(sample(), request({{"Duration", "0x10", "15"}}),
                  RunlogEditCode::kInvalidRequest);
  expect_rejected(sample(), request({{"Experiment", "stale", "new"}}),
                  RunlogEditCode::kConflict);
  expect_rejected(sample(), experiment, RunlogEditCode::kActiveRun, 64);
  expect_rejected(sample(), request({{"Experiment", "Development", "x\ny"}}),
                  RunlogEditCode::kInvalidValue);
  expect_rejected(sample(), request({{"Comment", "test", std::string(1025, 'x')}}),
                  RunlogEditCode::kInvalidValue);
  expect_rejected(sample(), request({{"Experiment", "Development", std::string(256, 'x')}}),
                  RunlogEditCode::kInvalidValue);
  expect_rejected(sample(), request({{"Experiment", "Development", "x"},
                                     {"Experiment", "Development", "y"}}),
                  RunlogEditCode::kInvalidRequest);
  expect_rejected(sample(), request({}, 64), RunlogEditCode::kInvalidRequest);
  expect_rejected(sample(), request({{"Experiment", "Development", "new"}}, 0),
                  RunlogEditCode::kInvalidRequest);
  Json no_eor = sample(); no_eor.erase("EOR");
  expect_rejected(no_eor, experiment, RunlogEditCode::kIncomplete);
  Json no_stop = sample(); no_stop["EOR"].erase("Stop time");
  expect_rejected(no_stop, experiment, RunlogEditCode::kIncomplete);
  Json empty_stop = sample(); empty_stop["EOR"]["Stop time"] = "";
  expect_rejected(empty_stop, experiment, RunlogEditCode::kIncomplete);
  Json wrong_number = sample(); wrong_number["BOR"]["Run number"] = 63;
  expect_rejected(wrong_number, experiment, RunlogEditCode::kRunMismatch);
  Directory missing;
  CHECK(editor(missing).edit(experiment).code == RunlogEditCode::kNotFound);
  Directory invalid;
  write_text(runlog(invalid), "{\"BOR\":{},\"EOR\":");
  const auto broken = read_text(runlog(invalid));
  CHECK(editor(invalid).edit(experiment).code == RunlogEditCode::kInvalidJson);
  CHECK(read_text(runlog(invalid)) == broken);
  Directory duplicate;
  write_text(runlog(duplicate), "{\"BOR\":{},\"BOR\":{},\"EOR\":{}}");
  CHECK(editor(duplicate).edit(experiment).code == RunlogEditCode::kInvalidJson);
  Directory symlink;
  const fs::path target = symlink.path / "target.json";
  write_text(target, sample().dump());
  fs::create_symlink(target, runlog(symlink));
  CHECK(editor(symlink).edit(experiment).code == RunlogEditCode::kUnsafeFile);
  CHECK(read_text(target) == sample().dump());
  Directory non_file;
  fs::create_directory(runlog(non_file));
  CHECK(editor(non_file).edit(experiment).code == RunlogEditCode::kUnsafeFile);
}

void test_past_run_while_other_run_acquires() {
  Directory directory;
  write_sample(directory);
  CHECK(editor(directory, 65).edit(
      request({{"Comment", "test", "edited while 65 runs"}})).ok());
  CHECK(Json::parse(read_text(runlog(directory)))["EOR"]["Comment"] ==
        "edited while 65 runs");
}

void test_run_becomes_active_before_replace() {
  Directory directory;
  write_sample(directory);
  const std::string before = read_text(runlog(directory));
  int activity_checks = 0;
  RunlogEditor guarded(directory.path, [&activity_checks](std::int64_t) {
    return ++activity_checks >= 2;
  });
  CHECK(guarded.edit(request({{"Comment", "test", "new"}})).code ==
        RunlogEditCode::kActiveRun);
  CHECK(activity_checks == 2);
  CHECK(read_text(runlog(directory)) == before);
  std::size_t files = 0;
  for (const auto& entry : fs::directory_iterator(directory.path)) {
    CHECK(entry.path().filename() == "runlog_000064.json");
    ++files;
  }
  CHECK(files == 1);
}

}  // namespace

int main() {
  test_successes();
  test_rejections();
  test_past_run_while_other_run_acquires();
  test_run_becomes_active_before_replace();
  std::printf("runlog_edit_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
