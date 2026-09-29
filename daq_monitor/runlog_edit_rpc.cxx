#include "runlog_edit_rpc.h"

#include "midas.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace daq_monitor {
namespace {

using Json = nlohmann::json;
constexpr std::size_t kMaximumRequestBytes = 16 * 1024;

std::string failure(const char* code, const char* message) {
  return Json{{"ok", false}, {"code", code}, {"message", message}}.dump();
}

bool has_exact_keys(const Json& value, const std::set<std::string>& expected) {
  if (!value.is_object() || value.size() != expected.size()) return false;
  for (const auto& key : expected)
    if (!value.contains(key)) return false;
  return true;
}

Json parse_without_duplicate_keys(const std::string& arguments) {
  std::vector<std::set<std::string>> keys;
  auto callback = [&keys](int, Json::parse_event_t event, Json& value) {
    if (event == Json::parse_event_t::object_start) keys.emplace_back();
    else if (event == Json::parse_event_t::object_end) keys.pop_back();
    else if (event == Json::parse_event_t::key &&
             !keys.back().insert(value.get<std::string>()).second)
      throw std::invalid_argument("duplicate request key");
    return true;
  };
  return Json::parse(arguments, callback);
}

std::optional<RunlogEditRequest> parse_request(const Json& body) {
  if (!has_exact_keys(body, {"run", "changes"}) ||
      !body["run"].is_number_integer() || !body["changes"].is_object() ||
      body["changes"].empty() || body["changes"].size() > 3)
    return std::nullopt;
  std::int64_t run = 0;
  if (body["run"].is_number_unsigned()) {
    const auto candidate = body["run"].get<std::uint64_t>();
    if (candidate == 0 || candidate > std::numeric_limits<std::int32_t>::max())
      return std::nullopt;
    run = static_cast<std::int64_t>(candidate);
  } else {
    run = body["run"].get<std::int64_t>();
    if (run <= 0 || run > std::numeric_limits<std::int32_t>::max())
      return std::nullopt;
  }
  RunlogEditRequest request;
  request.run_number = run;
  for (auto it = body["changes"].begin(); it != body["changes"].end(); ++it) {
    const std::string field = it.key();
    if (field != "Experiment" && field != "Type" && field != "Comment")
      return std::nullopt;
    if (!has_exact_keys(it.value(), {"current", "value"}) ||
        (!it.value()["current"].is_null() &&
         !it.value()["current"].is_string()) ||
        !it.value()["value"].is_string())
      return std::nullopt;
    std::optional<std::string> current;
    if (!it.value()["current"].is_null())
      current = it.value()["current"].get<std::string>();
    request.changes.push_back(
        {field, std::move(current), it.value()["value"].get<std::string>()});
  }
  return request;
}

}  // namespace

std::string runlog_edit_rpc_response(const RunlogEditResult& result,
                                     std::int64_t run_number) {
  switch (result.code) {
    case RunlogEditCode::kOk:
      return Json{{"ok", true}, {"run", run_number}}.dump();
    case RunlogEditCode::kInvalidRequest:
      return failure("invalid_request", "Invalid edit request.");
    case RunlogEditCode::kInvalidValue:
      return failure("invalid_value", "An edit value is invalid.");
    case RunlogEditCode::kActiveRun:
      return failure("run_active", "This run may still be acquiring data.");
    case RunlogEditCode::kNotFound:
      return failure("run_not_found", "Runlog was not found.");
    case RunlogEditCode::kUnsafeFile:
    case RunlogEditCode::kInvalidJson:
    case RunlogEditCode::kRunMismatch:
      return failure("invalid_runlog", "Runlog cannot be edited safely.");
    case RunlogEditCode::kIncomplete:
      return failure("incomplete_run", "Runlog has no completed EOR.");
    case RunlogEditCode::kConflict:
      return failure("stale_value", "Run metadata changed since it was loaded.");
    case RunlogEditCode::kIoError:
      return failure("write_failed", "Runlog update failed.");
    case RunlogEditCode::kDurabilityUnknown:
      return failure("saved_but_sync_uncertain",
                     "Runlog may have been saved; reload it before retrying.");
  }
  return failure("write_failed", "Runlog update failed.");
}

bool runlog_edit_target_active(std::int64_t target_run,
                               std::int64_t current_run, int run_state,
                               int transition_in_progress, bool state_valid) {
  if (!state_valid || current_run <= 0) return true;
  return target_run == current_run &&
         (run_state != STATE_STOPPED || transition_in_progress != 0);
}

std::string handle_runlog_edit_rpc(const std::string& command,
                                   const std::string& arguments,
                                   const RunlogEditor& editor) {
  if (command != kEditRunlogMetadataCommand || arguments.empty() ||
      arguments.size() > kMaximumRequestBytes)
    return failure("invalid_request", "Invalid edit request.");
  std::optional<RunlogEditRequest> request;
  try {
    request = parse_request(parse_without_duplicate_keys(arguments));
  } catch (const Json::exception&) {
    return failure("invalid_request", "Invalid edit request.");
  } catch (const std::invalid_argument&) {
    return failure("invalid_request", "Invalid edit request.");
  }
  if (!request) return failure("invalid_request", "Invalid edit request.");
  try {
    return runlog_edit_rpc_response(editor.edit(*request), request->run_number);
  } catch (const std::exception&) {
    return failure("write_failed", "Runlog update failed.");
  }
}

}  // namespace daq_monitor
