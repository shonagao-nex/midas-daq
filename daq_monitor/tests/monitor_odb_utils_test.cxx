#include "monitor_odb_utils.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct MockRead {
  INT status = DB_SUCCESS;
  INT size = 0;
  std::vector<char> bytes;
};

MockRead gRead;
HNDLE gLastDatabase = 0;
HNDLE gLastRoot = 0;
std::string gLastPath;
DWORD gLastType = 0;
BOOL gLastCreate = TRUE;
int gChecks = 0;
int gFailures = 0;

void expect(bool condition, const char* expression, int line) {
  ++gChecks;
  if (!condition) {
    ++gFailures;
    std::fprintf(stderr, "line %d: expectation failed: %s\n", line,
                 expression);
  }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

template <typename T>
void reply_with(const T& value, INT size = sizeof(T)) {
  gRead = {};
  gRead.size = size;
  const char* begin = reinterpret_cast<const char*>(&value);
  gRead.bytes.assign(begin, begin + sizeof(value));
}

}  // namespace

//************************************//
// Simulate one MIDAS ODB read without connecting to an experiment
//************************************//
INT db_get_value(HNDLE database, HNDLE root, const char* path, void* data,
                 INT* size, DWORD type, BOOL create) {
  gLastDatabase = database;
  gLastRoot = root;
  gLastPath = path;
  gLastType = type;
  gLastCreate = create;
  if (gRead.status != DB_SUCCESS) return gRead.status;
  const std::size_t count =
      std::min(static_cast<std::size_t>(*size), gRead.bytes.size());
  if (count != 0) std::memcpy(data, gRead.bytes.data(), count);
  *size = gRead.size;
  return DB_SUCCESS;
}

//************************************//
// Check shared read semantics and MIDAS run-state conversion
//************************************//
int main() {
  const INT candidate = 42;
  reply_with(candidate);
  INT scalar = 7;
  EXPECT(daq_monitor::read_value(123, "/Test/Scalar", TID_INT32,
                                     &scalar));
  EXPECT(scalar == candidate);
  EXPECT(gLastDatabase == 123 && gLastRoot == 0);
  EXPECT(gLastPath == "/Test/Scalar" && gLastType == TID_INT32);
  EXPECT(gLastCreate == FALSE);

  reply_with(candidate, sizeof(candidate) - 1);
  scalar = 7;
  EXPECT(!daq_monitor::read_value(123, "/Test/Scalar", TID_INT32,
                                      &scalar));
  EXPECT(scalar == 7);
  gRead.status = DB_NO_KEY;
  EXPECT(!daq_monitor::read_value(123, "/Test/Scalar", TID_INT32,
                                      &scalar));
  EXPECT(scalar == 7);

  gRead = {};
  gRead.bytes = {'m', 'o', 'n', 'i', 't', 'o', 'r', '\0'};
  gRead.size = static_cast<INT>(gRead.bytes.size());
  std::string value = "unchanged";
  EXPECT(daq_monitor::read_string(123, "/Test/String", &value));
  EXPECT(value == "monitor");
  EXPECT(gLastPath == "/Test/String" && gLastType == TID_STRING);
  EXPECT(gLastCreate == FALSE);

  gRead.bytes.assign(512, 'x');
  gRead.size = 512;
  EXPECT(daq_monitor::read_string(123, "/Test/String", &value));
  EXPECT(value.size() == 511 && value == std::string(511, 'x'));
  gRead.size = 0;
  EXPECT(!daq_monitor::read_string(123, "/Test/String", &value));
  EXPECT(value.size() == 511);
  gRead.status = DB_NO_KEY;
  EXPECT(!daq_monitor::read_string(123, "/Test/String", &value));
  EXPECT(value.size() == 511);

  EXPECT(daq_monitor::policy_run_state(STATE_RUNNING) ==
         daq_monitor::RunState::kRunning);
  EXPECT(daq_monitor::policy_run_state(STATE_STOPPED) ==
         daq_monitor::RunState::kStopped);
  EXPECT(daq_monitor::policy_run_state(STATE_PAUSED) ==
         daq_monitor::RunState::kPausedOrTransition);
  EXPECT(daq_monitor::policy_run_state(-1) ==
         daq_monitor::RunState::kPausedOrTransition);

  std::printf("monitor_odb_utils_test: %d checks, %d failures\n", gChecks,
              gFailures);
  return gFailures == 0 ? 0 : 1;
}
