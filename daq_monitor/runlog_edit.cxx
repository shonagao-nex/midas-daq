#include "runlog_edit.h"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace daq_monitor {
namespace {

using Json = nlohmann::json;
constexpr std::size_t kMaximumRunlogBytes = 16 * 1024 * 1024;

struct Fd {
  explicit Fd(int value = -1) : value(value) {}
  ~Fd() { if (value >= 0) close(value); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  int value;
};

struct TemporaryFile {
  explicit TemporaryFile(std::string name) : name(std::move(name)) {}
  ~TemporaryFile() { if (!name.empty()) unlink(name.c_str()); }
  std::string name;
};

RunlogEditResult result(RunlogEditCode code, const std::string& message) {
  return {code, message};
}

std::string filename_for_run(std::int64_t run) {
  std::string number = std::to_string(run);
  if (number.size() < 6) number.insert(0, 6 - number.size(), '0');
  return "runlog_" + number + ".json";
}

// Match the DAQ GUI maxlength (UTF-16 units) and ODB STRING capacity (bytes).
bool valid_text(const std::string& value, std::size_t max_units,
                std::size_t max_bytes, bool multiline) {
  if (value.size() > max_bytes) return false;
  std::size_t units = 0;
  for (std::size_t i = 0; i < value.size();) {
    const unsigned char first = static_cast<unsigned char>(value[i]);
    std::uint32_t codepoint = 0;
    std::size_t count = 0;
    if (first < 0x80) { codepoint = first; count = 1; }
    else if (first >= 0xC2 && first <= 0xDF) {
      codepoint = first & 0x1F; count = 2;
    } else if (first >= 0xE0 && first <= 0xEF) {
      codepoint = first & 0x0F; count = 3;
    } else if (first >= 0xF0 && first <= 0xF4) {
      codepoint = first & 0x07; count = 4;
    } else return false;
    if (i + count > value.size()) return false;
    for (std::size_t j = 1; j < count; ++j) {
      const unsigned char next = static_cast<unsigned char>(value[i + j]);
      if ((next & 0xC0) != 0x80) return false;
      codepoint = (codepoint << 6) | (next & 0x3F);
    }
    if ((count == 2 && codepoint < 0x80) ||
        (count == 3 && codepoint < 0x800) ||
        (count == 4 && codepoint < 0x10000) ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
        codepoint > 0x10FFFF) return false;
    if ((codepoint < 0x20 &&
         !(multiline && (codepoint == '\n' || codepoint == '\t'))) ||
        (codepoint >= 0x7F && codepoint <= 0x9F)) return false;
    units += codepoint > 0xFFFF ? 2 : 1;
    if (units > max_units) return false;
    i += count;
  }
  return true;
}

std::string normalize_comment(const std::string& value) {
  std::string normalized;
  normalized.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\r') {
      normalized.push_back('\n');
      if (i + 1 < value.size() && value[i + 1] == '\n') ++i;
    } else normalized.push_back(value[i]);
  }
  return normalized;
}

bool parse_strict_json(const std::string& text, Json* output) {
  std::vector<std::set<std::string>> keys;
  try {
    auto callback = [&keys](int, Json::parse_event_t event, Json& value) {
      if (event == Json::parse_event_t::object_start) keys.emplace_back();
      else if (event == Json::parse_event_t::object_end) keys.pop_back();
      else if (event == Json::parse_event_t::key &&
               !keys.back().insert(value.get<std::string>()).second)
        throw std::runtime_error("duplicate JSON key");
      return true;
    };
    *output = Json::parse(text, callback);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

std::optional<std::string> string_field(const Json& parent, const char* key) {
  const auto found = parent.find(key);
  if (found == parent.end()) return std::nullopt;
  if (!found->is_string()) throw std::runtime_error("metadata field is not a string");
  return found->get<std::string>();
}

void erase_editable_fields(Json* record) {
  (*record)["BOR"].erase("experiment_label");
  (*record)["BOR"].erase("Type");
  (*record)["EOR"].erase("Comment");
}

bool same_file(const struct stat& first, const struct stat& second) {
  return first.st_dev == second.st_dev && first.st_ino == second.st_ino &&
         first.st_size == second.st_size &&
         first.st_mtim.tv_sec == second.st_mtim.tv_sec &&
         first.st_mtim.tv_nsec == second.st_mtim.tv_nsec &&
         first.st_ctim.tv_sec == second.st_ctim.tv_sec &&
         first.st_ctim.tv_nsec == second.st_ctim.tv_nsec;
}

bool write_all(int fd, const std::string& value) {
  std::size_t offset = 0;
  while (offset < value.size()) {
    const ssize_t count = write(fd, value.data() + offset, value.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    offset += static_cast<std::size_t>(count);
  }
  return true;
}

}  // namespace

RunlogEditor::RunlogEditor(std::filesystem::path trusted_runlog_directory,
                           std::function<bool(std::int64_t)> is_run_active)
    : directory_(std::move(trusted_runlog_directory)),
      is_run_active_(std::move(is_run_active)) {}

RunlogEditResult RunlogEditor::edit(const RunlogEditRequest& request) const {
  if (!is_run_active_ || request.run_number <= 0 ||
      request.run_number > std::numeric_limits<std::int32_t>::max() ||
      request.changes.empty() || request.changes.size() > 3)
    return result(RunlogEditCode::kInvalidRequest, "Invalid run or edit request");

  std::set<std::string> requested;
  std::vector<std::pair<std::string, std::string>> replacements;
  for (const auto& change : request.changes) {
    if (!requested.insert(change.field).second)
      return result(RunlogEditCode::kInvalidRequest, "Duplicate edit field");
    std::string value = change.value;
    if (change.field == "Experiment") {
      if (!valid_text(value, 255, 1023, false))
        return result(RunlogEditCode::kInvalidValue, "Invalid Experiment");
    } else if (change.field == "Type") {
      if (value != "Data" && value != "Clock" && value != "Cosmic" &&
          value != "Test")
        return result(RunlogEditCode::kInvalidValue, "Invalid Type");
    } else if (change.field == "Comment") {
      value = normalize_comment(value);
      if (!valid_text(value, 1024, 4096, true))
        return result(RunlogEditCode::kInvalidValue, "Invalid Comment");
    } else return result(RunlogEditCode::kInvalidRequest, "Edit field is not allowed");
    replacements.emplace_back(change.field, std::move(value));
  }

  try {
    if (is_run_active_(request.run_number))
      return result(RunlogEditCode::kActiveRun, "Run is still acquiring");
  } catch (const std::exception&) {
    return result(RunlogEditCode::kActiveRun, "Cannot verify run activity");
  }

  if (!directory_.is_absolute())
    return result(RunlogEditCode::kInvalidRequest,
                  "Trusted Runlog directory must be absolute");
  Fd directory(open(directory_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                         O_NOFOLLOW));
  if (directory.value < 0)
    return result(RunlogEditCode::kIoError, "Cannot open trusted Runlog directory");
  if (flock(directory.value, LOCK_EX) != 0)
    return result(RunlogEditCode::kIoError, "Cannot lock Runlog directory");
  const std::string filename = filename_for_run(request.run_number);
  Fd source(openat(directory.value, filename.c_str(),
                   O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  if (source.value < 0) {
    if (errno == ENOENT) return result(RunlogEditCode::kNotFound, "Runlog does not exist");
    if (errno == ELOOP) return result(RunlogEditCode::kUnsafeFile, "Symlink is not allowed");
    return result(RunlogEditCode::kIoError, "Cannot open Runlog");
  }
  struct stat original_stat {};
  if (fstat(source.value, &original_stat) != 0)
    return result(RunlogEditCode::kIoError, "Cannot inspect Runlog");
  if (!S_ISREG(original_stat.st_mode) || original_stat.st_nlink != 1 ||
      original_stat.st_uid != geteuid())
    return result(RunlogEditCode::kUnsafeFile, "Runlog is not an owned regular file");
  if (original_stat.st_size < 0 ||
      static_cast<std::uint64_t>(original_stat.st_size) > kMaximumRunlogBytes)
    return result(RunlogEditCode::kInvalidJson, "Runlog size is invalid");
  std::string contents;
  char buffer[8192];
  for (;;) {
    const ssize_t count = read(source.value, buffer, sizeof(buffer));
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) return result(RunlogEditCode::kIoError, "Cannot read Runlog");
    if (count == 0) break;
    contents.append(buffer, static_cast<std::size_t>(count));
    if (contents.size() > kMaximumRunlogBytes)
      return result(RunlogEditCode::kInvalidJson, "Runlog is too large");
  }
  Json original;
  if (!parse_strict_json(contents, &original) || !original.is_object())
    return result(RunlogEditCode::kInvalidJson, "Runlog JSON is invalid");
  if (!original.contains("BOR") || !original["BOR"].is_object() ||
      !original.contains("EOR") || !original["EOR"].is_object() ||
      !original["EOR"].contains("Stop time") ||
      !original["EOR"]["Stop time"].is_string() ||
      original["EOR"]["Stop time"].get<std::string>().empty())
    return result(RunlogEditCode::kIncomplete, "Runlog has no complete EOR");
  const auto number = original["BOR"].find("Run number");
  if (number == original["BOR"].end() || !number->is_number_integer() ||
      *number != request.run_number)
    return result(RunlogEditCode::kRunMismatch, "BOR run number does not match");

  Json updated = original;
  try {
    for (std::size_t i = 0; i < request.changes.size(); ++i) {
      const auto& change = request.changes[i];
      Json& parent = change.field == "Comment" ? updated["EOR"] : updated["BOR"];
      const char* key = change.field == "Experiment" ? "experiment_label" :
                        change.field == "Type" ? "Type" : "Comment";
      if (string_field(parent, key) != change.expected)
        return result(RunlogEditCode::kConflict, "Runlog metadata changed since it was read");
      parent[key] = replacements[i].second;
    }
  } catch (const std::exception&) {
    return result(RunlogEditCode::kInvalidJson, "Existing metadata is not a string");
  }
  Json original_rest = original, updated_rest = updated;
  erase_editable_fields(&original_rest);
  erase_editable_fields(&updated_rest);
  if (original_rest != updated_rest)
    return result(RunlogEditCode::kInvalidRequest, "Non-editable JSON data changed");

  std::string serialized;
  try { serialized = updated.dump(2, ' ', false, Json::error_handler_t::strict) + "\n"; }
  catch (const std::exception&) {
    return result(RunlogEditCode::kInvalidJson, "Cannot serialize Runlog JSON");
  }
  Json verified;
  if (!parse_strict_json(serialized, &verified) || verified != updated)
    return result(RunlogEditCode::kInvalidJson, "Serialized Runlog failed validation");

  std::string template_path = (directory_ / ".runlog_edit.XXXXXX").string();
  std::vector<char> template_buffer(template_path.begin(), template_path.end());
  template_buffer.push_back('\0');
  Fd temporary(mkstemp(template_buffer.data()));
  if (temporary.value < 0)
    return result(RunlogEditCode::kIoError, "Cannot create temporary Runlog");
  TemporaryFile cleanup(template_buffer.data());
  if (!write_all(temporary.value, serialized) ||
      fchmod(temporary.value, original_stat.st_mode & 0777) != 0 ||
      fsync(temporary.value) != 0)
    return result(RunlogEditCode::kIoError, "Cannot write and sync temporary Runlog");

  struct stat current_stat {};
  if (fstatat(directory.value, filename.c_str(), &current_stat,
              AT_SYMLINK_NOFOLLOW) != 0 || !same_file(original_stat, current_stat))
    return result(RunlogEditCode::kConflict, "Runlog changed before replacement");
  try {
    if (is_run_active_(request.run_number))
      return result(RunlogEditCode::kActiveRun, "Run began acquiring before replacement");
  } catch (const std::exception&) {
    return result(RunlogEditCode::kActiveRun, "Cannot verify run activity");
  }
  const std::string temporary_name =
      std::filesystem::path(cleanup.name).filename().string();
  if (renameat(directory.value, temporary_name.c_str(), directory.value,
               filename.c_str()) != 0)
    return result(RunlogEditCode::kIoError, "Cannot replace Runlog atomically");
  cleanup.name.clear();
  if (fsync(directory.value) != 0)
    return result(RunlogEditCode::kDurabilityUnknown,
                  "Runlog was replaced but directory sync failed; reread it");
  return result(RunlogEditCode::kOk, "Runlog metadata updated");
}

}  // namespace daq_monitor
