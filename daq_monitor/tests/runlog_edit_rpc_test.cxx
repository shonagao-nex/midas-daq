#include "runlog_edit_rpc.h"

#include "midas.h"
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
using daq_monitor::RunlogEditor;
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
    char name[] = "/tmp/runlog-edit-rpc-test.XXXXXX";
    char* created = mkdtemp(name);
    if (!created) std::abort();
    path = created;
  }
  ~Directory() { fs::remove_all(path); }
  fs::path path;
  fs::path runlog() const { return path / "runlog_000064.json"; }
  void write(const Json& value) const {
    std::ofstream output(runlog(), std::ios::binary);
    output << value.dump(2) << '\n';
    if (!output) std::abort();
  }
  Json read() const {
    std::ifstream input(runlog(), std::ios::binary);
    return Json::parse(input);
  }
};

Json sample() {
  return {{"BOR", {{"Run number", 64}, {"Start time", "Tue Sep 29 21:38:49 2026"},
                   {"experiment_label", "Development"}, {"Type", "Test"}}},
          {"EOR", {{"Stop time", "Tue Sep 29 21:39:05 2026"},
                   {"Comment", "test"}, {"Duration", "0x0000000000000010"},
                   {"VME events", "102"}, {"EASIROC events", "-1"},
                   {"HUL events", "-1"}, {"EventSlipCount", "0x0000000000000000"},
                   {"DAQ Status", "OK"}, {"Scaler 64ch", Json::object()}}}};
}

Json request(const Json& changes) {
  return {{"run", 64}, {"changes", changes}};
}

Json field(const char* current, const char* value) {
  return {{"current", current}, {"value", value}};
}

Json call(const RunlogEditor& editor, const Json& body,
          const std::string& command = daq_monitor::kEditRunlogMetadataCommand) {
  return Json::parse(daq_monitor::handle_runlog_edit_rpc(
      command, body.dump(), editor));
}

RunlogEditor editor(const Directory& directory, int active_run = 0) {
  return RunlogEditor(directory.path, [active_run](std::int64_t run) {
    return active_run == run;
  });
}

void expect_error(const RunlogEditor& target, const Json& body,
                  const char* code) {
  const Json reply = call(target, body);
  CHECK(reply["ok"] == false);
  CHECK(reply["code"] == code);
  CHECK(reply["message"].is_string());
  CHECK(reply.dump().find("/home/") == std::string::npos);
  CHECK(reply.dump().find("runlog_000064.json") == std::string::npos);
}

void test_successes() {
  Directory directory;
  const Json original = sample();
  const std::vector<std::pair<Json, std::string>> cases = {
      {request({{"Experiment", field("Development", "WC test 2026")}}),
       "WC test 2026"},
      {request({{"Type", field("Test", "Data")}}), "Data"},
      {request({{"Comment", field("test", "new comment")}}), "new comment"}};
  for (std::size_t index = 0; index < cases.size(); ++index) {
    directory.write(original);
    const Json reply = call(editor(directory), cases[index].first);
    CHECK(reply == Json({{"ok", true}, {"run", 64}}));
    const Json updated = directory.read();
    const char* field_name = index == 0 ? "experiment_label" :
                             index == 1 ? "Type" : "Comment";
    const char* phase = index == 2 ? "EOR" : "BOR";
    CHECK(updated[phase][field_name] == cases[index].second);
    CHECK(updated["EOR"]["VME events"] == original["EOR"]["VME events"]);
  }
  directory.write(original);
  const Json all = request({{"Experiment", field("Development", "KEK beamtime 2026")},
                            {"Type", field("Test", "Cosmic")},
                            {"Comment", field("test", "line 1\nline 2")}});
  CHECK(call(editor(directory), all) == Json({{"ok", true}, {"run", 64}}));
  const Json updated = directory.read();
  CHECK(updated["BOR"]["experiment_label"] == "KEK beamtime 2026");
  CHECK(updated["BOR"]["Type"] == "Cosmic");
  CHECK(updated["EOR"]["Comment"] == "line 1\nline 2");
  CHECK(updated["EOR"]["DAQ Status"] == "OK");
  Json missing_experiment = original;
  missing_experiment["BOR"].erase("experiment_label");
  directory.write(missing_experiment);
  CHECK(call(editor(directory), request({{"Experiment", {
      {"current", nullptr}, {"value", "new label"}}}}))["ok"] == true);
  CHECK(directory.read()["BOR"]["experiment_label"] == "new label");
}

void test_bad_requests() {
  Directory directory;
  directory.write(sample());
  const auto target = editor(directory);
  expect_error(target, Json::array(), "invalid_request");
  expect_error(target, request(Json::object()), "invalid_request");
  expect_error(target, {{"run", "64"}, {"changes", Json::object()}},
               "invalid_request");
  for (const Json& run : {Json(-1), Json(0), Json(1.5), Json(true),
                          Json(2147483648LL)}) {
    auto body = request({{"Type", field("Test", "Data")}});
    body["run"] = run;
    expect_error(target, body, "invalid_request");
  }
  auto unknown_top = request({{"Type", field("Test", "Data")}});
  unknown_top["path"] = "/tmp/other.json";
  expect_error(target, unknown_top, "invalid_request");
  auto unknown_field = request({{"Duration", field("0x10", "15")}});
  expect_error(target, unknown_field, "invalid_request");
  auto unknown_property = request({{"Type", field("Test", "Data")}});
  unknown_property["changes"]["Type"]["filename"] = "elsewhere.json";
  expect_error(target, unknown_property, "invalid_request");
  auto wrong_value_type = request({{"Comment", {{"current", "test"}, {"value", 4}}}});
  expect_error(target, wrong_value_type, "invalid_request");
  expect_error(target, request({{"Type", field("Test", "Other")}}),
               "invalid_value");
  expect_error(target, request({{"Experiment", field("stale", "new")}}),
               "stale_value");
  CHECK(Json::parse(daq_monitor::handle_runlog_edit_rpc(
      "unknown", request({{"Type", field("Test", "Data")}}).dump(), target))["code"] ==
        "invalid_request");
  CHECK(Json::parse(daq_monitor::handle_runlog_edit_rpc(
      daq_monitor::kEditRunlogMetadataCommand,
      "{\"run\":64,\"run\":64,\"changes\":{}}", target))["code"] ==
        "invalid_request");
  CHECK(directory.read() == sample());
}

void test_run_conditions() {
  Directory directory;
  const Json change = request({{"Comment", field("test", "edited")}});
  directory.write(sample());
  expect_error(editor(directory, 64), change, "run_active");
  CHECK(directory.read() == sample());
  CHECK(call(editor(directory, 65), change)["ok"] == true);
  fs::remove(directory.runlog());
  expect_error(editor(directory), change, "run_not_found");
  Json bor_only = sample(); bor_only.erase("EOR");
  directory.write(bor_only);
  expect_error(editor(directory), change, "incomplete_run");
}

void test_state_guard() {
  using daq_monitor::runlog_edit_target_active;
  CHECK(runlog_edit_target_active(64, 64, STATE_RUNNING, 0, true));
  CHECK(runlog_edit_target_active(64, 64, STATE_PAUSED, 0, true));
  CHECK(runlog_edit_target_active(64, 64, STATE_STOPPED, 1, true));
  CHECK(!runlog_edit_target_active(64, 64, STATE_STOPPED, 0, true));
  CHECK(!runlog_edit_target_active(63, 64, STATE_RUNNING, 0, true));
  CHECK(!runlog_edit_target_active(63, 64, STATE_STOPPED, 1, true));
  CHECK(runlog_edit_target_active(63, 64, STATE_RUNNING, 0, false));
}

void test_result_mapping() {
  const std::vector<std::pair<RunlogEditCode, const char*>> cases = {
      {RunlogEditCode::kInvalidRequest, "invalid_request"},
      {RunlogEditCode::kInvalidValue, "invalid_value"},
      {RunlogEditCode::kActiveRun, "run_active"},
      {RunlogEditCode::kNotFound, "run_not_found"},
      {RunlogEditCode::kUnsafeFile, "invalid_runlog"},
      {RunlogEditCode::kInvalidJson, "invalid_runlog"},
      {RunlogEditCode::kIncomplete, "incomplete_run"},
      {RunlogEditCode::kRunMismatch, "invalid_runlog"},
      {RunlogEditCode::kConflict, "stale_value"},
      {RunlogEditCode::kIoError, "write_failed"},
      {RunlogEditCode::kDurabilityUnknown, "saved_but_sync_uncertain"}};
  for (const auto& [internal, code] : cases) {
    const Json reply = Json::parse(daq_monitor::runlog_edit_rpc_response(
        {internal, "/home/nagao/private/runlog_000064.json"}, 64));
    CHECK(reply["ok"] == false);
    CHECK(reply["code"] == code);
    CHECK(reply.dump().find("/home/") == std::string::npos);
    CHECK(reply.dump().find("runlog_000064.json") == std::string::npos);
  }
  CHECK(Json::parse(daq_monitor::runlog_edit_rpc_response(
      {RunlogEditCode::kOk, "saved"}, 64)) ==
        Json({{"ok", true}, {"run", 64}}));
}

}  // namespace

int main() {
  test_successes();
  test_bad_requests();
  test_run_conditions();
  test_state_guard();
  test_result_mapping();
  std::printf("runlog_edit_rpc_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
