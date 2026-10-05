#ifndef V775_CONFIG_H
#define V775_CONFIG_H

#include "vme_odb.h"
#include "mvmestd.h"
#include "v775.h"
#include "v7xx_config.h"

namespace v775_config {

struct Access {
  MVME_INTERFACE *vme;
  DWORD base;
  const char *log_source;
  bool (*read16)(MVME_INTERFACE *, DWORD, WORD &, const char *);
  bool (*write16)(MVME_INTERFACE *, DWORD, WORD, const char *);
  int (*read_thresholds)(MVME_INTERFACE *, DWORD, WORD *);
};

struct Readback {
  WORD full_scale = 0;
  WORD bits = 0;
  WORD firmware = 0;
  WORD fast_clear = 0;
  WORD thresholds[V775_MAX_CHANNELS] = {};
};

enum class VerifyStatus { ReadFailure, Matched, Mismatch };

// Apply the V775 run settings in their established register order.
bool configure_for_run(const Access &access, const V775Settings &settings);
// Read and verify V775 settings without publishing frontend state.
VerifyStatus verify_configuration(const Access &access, const V775Settings &settings, Readback &result);
// Clear the V775 data buffer using the established set/clear sequence.
bool clear_data(const Access &access, bool manual);

struct DiagnosticAccess {
  MVME_INTERFACE *vme;
  DWORD base;
  bool (*read16)(MVME_INTERFACE *, DWORD, WORD &, const char *);
  bool (*write16)(MVME_INTERFACE *, DWORD, WORD, const char *);
  void (*event_counter)(MVME_INTERFACE *, DWORD, DWORD *);
  int (*sleep_ms)(int);
};

enum class RestoreResult { NoSavedState, NoClearNeeded, Restored, AccessFailure, VerifyFailure };
enum class EnableResult { AlreadyEnabled, Enabled, AccessFailure, VerifyFailure };
enum class PollResult { Ready, Timeout, ReadFailure };
constexpr unsigned kDiagnosticMaxPolls = 100;

bool save_diagnostic_settings(const DiagnosticAccess &access, v7xx_config::V775DiagnosticState &state);
EnableResult enable_empty_program(const DiagnosticAccess &access, v7xx_config::V775DiagnosticState &state, WORD &readback);
RestoreResult restore_diagnostic_settings(const DiagnosticAccess &access, v7xx_config::V775DiagnosticState &state, WORD &readback);
bool read_before_sw_comm(const DiagnosticAccess &access, WORD &status1, WORD &status2, DWORD &counter);
bool issue_sw_comm(const DiagnosticAccess &access);
PollResult poll_after_sw_comm(const DiagnosticAccess &access, WORD &status1, unsigned &polls_done);
bool read_after_sw_comm(const DiagnosticAccess &access, WORD &status2, DWORD &counter);

}

#endif
