#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <atomic>
#include <string>

#include "midas.h"
#include "mfe.h"
#include "mvmestd.h"
#include "../common/manual_buffer_clear.h"
#include "vme/v792.h"
#include "v775.h"
#include "v1720e.h"
#include "v1720e_readout.h"
#include "v792_blt32.h"
#include "v1190_fifo_blt32.h"
#include "v1190_readout.h"
#include "module_status_snapshot.h"
#include "caenvme.h"
#include "rpv130.h"
#include "global_busy.h"
#include "out0_diagnostic.h"
#include "vme_odb.h"
#include "v1190_config.h"
#include "v1190.h"
#include "v1190.h"
#include "v1720e_config.h"
#include "v1720e_readout.h"
#include "v1720e_integrity.h"
#include "v792_config.h"
#include "v775_config.h"
#include "v775_readout.h"
#include "v792_readout.h"

using vme_odb::make_odb_path;
using vme_odb::set_absolute_odb_value;
using vme_odb::ensure_odb_value;
using vme_odb::get_absolute_odb_value;
using vme_odb::publish_configuration_status;
using vme_odb::set_module_output;

/* Module locations. */
#define V792_BASE 0x00600000
#define V1190_BASE 0x00C10000
#define V775_BASE 0x00000000
#define V1720E_BASE V1720E_BASE_ADDRESS

#ifndef ENABLE_V792_SW_TRIGGER_TEST
#define ENABLE_V792_SW_TRIGGER_TEST 0
#endif
#ifndef ENABLE_V1190_SOFT_TRIGGER_TEST
#define ENABLE_V1190_SOFT_TRIGGER_TEST 0
#endif
#ifndef ENABLE_V775_SW_TRIGGER_TEST
#define ENABLE_V775_SW_TRIGGER_TEST 0
#endif

/* Bounded single-event readout limits and counter widths. */
static const size_t V792_MAX_EVENT_WORDS = v792_readout::kMaxEventWords;
enum V792ReadoutMode { V792_SINGLE_D32, V792_BLT32 };
/* Source selection for the V792 hardware comparison run. */
static const V792ReadoutMode V792_READOUT_MODE_SELECT = V792_BLT32;
static const size_t V1190_MAX_EVENT_WORDS = v1190_readout::kMaxEventWords;
enum V1190ReadoutMode { V1190_SINGLE_D32, V1190_EVENT_FIFO_BLT32 };
static const V1190ReadoutMode V1190_READOUT_MODE_SELECT = V1190_EVENT_FIFO_BLT32;
/* Set true for the original per-event Event FIFO Stored before/after checks. */
static const bool V1190_FIFO_STRICT_SYNC_CHECK = false;
static const size_t V775_MAX_EVENT_WORDS = v775_readout::kMaxEventWords;
static const DWORD V7XX_EVENT_COUNTER_MASK = 0x00FFFFFF;
static const DWORD V1190_EVENT_COUNTER_MASK = 0x003FFFFF;

/* Finite ready/handshake polling limits. */
static const unsigned V1190_READY_MAX_POLLS = 100;
static const unsigned V775_READY_MAX_POLLS = 100;
static const unsigned V1720E_READY_MAX_POLLS = 100;

/* V1190 regular registers and normal-run Control bits. */
static const DWORD V1190_STATUS = 0x1002;
static const DWORD V1190_EVENT_COUNTER = 0x101C;
static const DWORD V1190_EVENT_STORED = 0x1020;
static const DWORD V1190_EVENT_FIFO_STATUS = V1190_FIFO_STATUS_OFFSET;
static const DWORD V1190_EVENT_FIFO_STORED = V1190_FIFO_STORED_OFFSET;
static const WORD V1190_STATUS_DATA_READY = 0x0001;
static const WORD V1190_STATUS_ALMOST_FULL = 0x0002;
static const WORD V1190_STATUS_FULL = 0x0004;
static const WORD V1190_STATUS_TRIGGER_MATCH = 0x0008;
static const WORD V1190_FIFO_STATUS_DATA_READY = 0x0001;

/* V1190 microcontroller opcodes and operands from the V1190 manual. */
static const size_t V1190_CHANNEL_MASK_WORDS = 8;

const char *frontend_name = "fevme";            // MIDAS frontend/client name
const char *frontend_file_name = __FILE__;      // Frontend source file name

BOOL frontend_call_loop = TRUE;                 // Enable periodic read-only status monitoring
BOOL equipment_common_overwrite = TRUE;         // Apply polled/run-only settings to existing ODB equipment
INT display_period = 1000;                      // MIDAS status display update period [ms]
INT max_event_size = 1024 * 1024;               // Maximum event size [bytes]
INT max_event_size_frag = 5 * 1024 * 1024;      // Maximum fragmented event size [bytes]
INT event_buffer_size = 10 * 1024 * 1024;       // MIDAS event buffer size [bytes]

static MVME_INTERFACE *gVme = NULL;              // MIDAS VME interface handle
// Hold VME acquisition and readout state
struct VmeState {
  bool readout_failed = false;
  bool frontend_enabled_for_run = true;
  std::atomic<bool> blt_stop_requested{false};
  V1190_FIFO_BLT_STATE v1190_fifo_blt = {};
  DWORD rpv130_last_poll = 0;
  bool rpv130_enabled_for_run = true;
  bool single_event_busy_enabled_for_run = false;
  bool rpv130_busy_configured = false;
};
static VmeState gVmeState;
static const char *VME_RUN_SNAPSHOT_PATH = "/Equipment/VME/RunSnapshot";
static const DWORD RPV130_POLL_PERIOD_MS = 5000;
static const DWORD FRONTEND_IDLE_SLEEP_MS = 10;
static const char *RPV130_SETTINGS_PATH = "/Equipment/VME/Settings/RPV130";

static const char *BUFFER_CLEAR_COMMAND_PATH =
  "/Equipment/VME/Commands/BufferClearRequestId";
static const char *BUFFER_CLEAR_STATUS_PATH =
  "/Equipment/VME/Variables/BufferClear";

static const DWORD V1720E_ACQ_RUN = 0x00000004u;

struct V1720State {
  v1720e_config::RunConfig run;
  bool variables_enabled = true;
  struct {
    bool startup_enabled = true;
    INT startup_run_state = -1;
    bool started = false;
    bool start_attempted = false;
  } lifecycle;
  struct {
    V1720ERuntimeState value = {};
    DWORD last_variables_publish = 0;
  } runtime;
};
static V1720State gV1720State;
/* Change this source constant to BLT32 for the hardware comparison run. */
static const V1720E_READOUT_MODE V1720E_READOUT_MODE_SELECT = BLT32;
static const DWORD V1720E_VARIABLES_MIN_PUBLISH_INTERVAL_MS = 200;
static const DWORD V1720E_VARIABLES_HEARTBEAT_INTERVAL_MS = 1000;


static void mark_configuration_failed(INT run_number)
{
  if (!publish_configuration_status(false, run_number))
    cm_msg(MERROR, frontend_name,
         "Cannot publish failed VME configuration status for run %d",
         run_number);
}

static const char *V792_INFO_PATH = "/Equipment/VME/Info/V792";
static const char *V792_READBACK_PATH = "/Equipment/VME/Readback/V792";
static const char *V792_VARIABLES_PATH = "/Equipment/VME/Variables/V792";
static const char *V1190_INFO_PATH = "/Equipment/VME/Info/V1190";
static const char *V1190_READBACK_PATH = "/Equipment/VME/Readback/V1190";
static const char *V775_INFO_PATH = "/Equipment/VME/Info/V775";
static const char *V775_READBACK_PATH = "/Equipment/VME/Readback/V775";
static const char *V775_VARIABLES_PATH = "/Equipment/VME/Variables/V775";
static const DWORD MODULE_VARIABLES_MIN_PUBLISH_INTERVAL_MS = 200;
static const DWORD MODULE_VARIABLES_HEARTBEAT_INTERVAL_MS = 1000;

// Hold requested VME module settings for the run
struct VmeConfig {
  V792Settings v792 = v792_config::default_v792_settings();
  V775Settings v775 = v775_config::default_v775_settings();
};
static VmeConfig gVmeConfig;
static v1190_config::State gV1190Config = {
  v1190_config::default_settings(), {}};

// Hold VME module status and the run snapshot
struct VmeModuleState {
  VmeRunSnapshot snapshot = {};
  V7xxRuntimeState v792 = {};
  V1190RuntimeState v1190 = {};
  V7xxRuntimeState v775 = {};
  DWORD v792_last_publish = 0;
  DWORD v1190_last_publish = 0;
  DWORD v775_last_publish = 0;
};
static VmeModuleState gVmeModuleState;

static daq::BufferClearStatus gBufferClearStatus;
static VmeBufferClearResults gBufferClearResults;

// Initialize the VME buffer-clear request mailbox
static bool initialize_buffer_clear_mailbox()
{
  if (!vme_odb::ensure_buffer_clear_schema()) return false;
  char path[256];
  DWORD request_id = 0, handled = 0, successful = 0;
  if (!get_absolute_odb_value(BUFFER_CLEAR_COMMAND_PATH, &request_id,
                sizeof(request_id), TID_DWORD) ||
    !make_odb_path(path, sizeof(path), BUFFER_CLEAR_STATUS_PATH,
             "LastHandledRequestId") ||
    !get_absolute_odb_value(path, &handled, sizeof(handled), TID_DWORD) ||
    !make_odb_path(path, sizeof(path), BUFFER_CLEAR_STATUS_PATH,
             "LastSuccessfulRequestId") ||
    !get_absolute_odb_value(path, &successful, sizeof(successful), TID_DWORD))
    return false;
  gBufferClearStatus = {};
  gBufferClearStatus.last_handled_request_id = handled;
  gBufferClearStatus.last_successful_request_id = successful;
  if (request_id > handled) {
    const time_t now = time(NULL);
    gBufferClearStatus = daq::acknowledgeStaleBufferClearRequest(
      gBufferClearStatus, request_id,
      now < 0 ? 0 : static_cast<uint64_t>(now));
    gBufferClearResults = {};
    gBufferClearResults.v792 = gBufferClearResults.v1190 =
      gBufferClearResults.v775 = gBufferClearResults.v1720e =
        "Not executed: stale startup request";
    cm_msg(MINFO, frontend_name, "WARNING: %s (request %u)",
         gBufferClearStatus.last_error.c_str(), request_id);
  }
  return vme_odb::publish_buffer_clear_status(gBufferClearStatus, gBufferClearResults);
}

// Initialize RPV130 ODB mapping and startup flags
static bool initialize_rpv130_odb()
{
  return vme_odb::initialize_rpv130_odb(
    gVmeState.rpv130_enabled_for_run,
    gVmeState.single_event_busy_enabled_for_run);
}

static void fail_single_event_busy(const char *reason);

// Poll RPV130 status and pass acquired values to the ODB mapping
static void publish_rpv130_status(bool force)
{
  if ((!gVmeState.rpv130_enabled_for_run && !gVmeState.single_event_busy_enabled_for_run) || !gVme)
    return;
  const DWORD now = ss_millitime();
  if (!force &&
    static_cast<DWORD>(now - gVmeState.rpv130_last_poll) < RPV130_POLL_PERIOD_MS)
    return;
  gVmeState.rpv130_last_poll = now;

  RPV130_STATUS status = {};
  const INT read_result =
    rpv130_read_status(gVme, RPV130_BASE_ADDRESS, &status);
  const BOOL communication_ok = read_result == MVME_SUCCESS;
  if (!communication_ok) {
    status = {};
    cm_msg(MERROR, frontend_name,
         "RPV130 read-only status poll failed at base 0x%04X: status %d",
         RPV130_BASE_ADDRESS, read_result);
    if (gVmeState.single_event_busy_enabled_for_run &&
      global_busy::readout_allowed())
      fail_single_event_busy("RPV130 status/CSR1 poll failed");
  }

  const BOOL busy1 = (status.csr1 & RPV130_CSR1_BUSY1) ? TRUE : FALSE;
  const BOOL armed = communication_ok && gVmeState.rpv130_busy_configured &&
    (status.csr1 & RPV130_CSR1_CHANNEL1_ARMED) ==
      RPV130_CSR1_CHANNEL1_ARMED ? TRUE : FALSE;
  vme_odb::publish_rpv130_status(status, communication_ok, busy1, armed);
}

// Forward RPV130 BUSY status updates to the ODB mapping
static bool publish_rpv130_busy_state(bool busy, bool armed)
{
  return vme_odb::publish_rpv130_busy_state(busy, armed);
}

// Hold single-event BUSY after a readout or RPV130 failure
static void fail_single_event_busy(const char *reason)
{
  if (!gVmeState.single_event_busy_enabled_for_run) return;
  gVmeState.readout_failed = true;
  global_busy::disable_readout();
  cm_msg(MERROR, frontend_name, "RPV130 Single Event BUSY held: %s", reason);
  if (!global_busy::set_global_busy(true))
    cm_msg(MERROR, frontend_name,
         "Cannot assert V3718 Global BUSY after RPV130/readout failure");
  vme_odb::publish_rpv130_armed(false);
}

// Arm RPV130 single-event BUSY under asserted Global BUSY
static bool arm_rpv130_single_event_busy()
{
  if (!gVmeState.single_event_busy_enabled_for_run) return true;
  // A failed first write still needs a later cleanup attempt under Global BUSY.
  gVmeState.rpv130_busy_configured = true;
  if (!global_busy::set_global_busy(true)) {
    cm_msg(MERROR, frontend_name,
         "Cannot verify Global BUSY before arming RPV130 FIN1");
    return false;
  }
  uint8_t csr1 = 0;
  const int rc = rpv130_clear_busy1_and_rearm(
    gVme, RPV130_BASE_ADDRESS, &csr1);
  if (rc != MVME_SUCCESS || (csr1 & RPV130_CSR1_BUSY1)) {
    cm_msg(MERROR, frontend_name,
         "RPV130 FIN1 arm/CSR1 readback failed: status %d CSR1=0x%02X",
         rc, csr1);
    vme_odb::publish_rpv130_armed(false);
    return false;
  }
  if (!publish_rpv130_busy_state(false, true)) return false;
  cm_msg(MINFO, frontend_name,
       "RPV130 Single Event BUSY armed: FIN1->BOUT1, CSR1=0x%02X",
       csr1);
  return true;
}

// Disable RPV130 single-event BUSY under asserted Global BUSY
static bool quiesce_rpv130_single_event_busy(const char *context)
{
  if (!gVmeState.single_event_busy_enabled_for_run || !gVme) return true;
  // Never release BUSY1 before OUT0 has been asserted and verified.
  if (!global_busy::set_global_busy(true)) {
    cm_msg(MERROR, frontend_name,
         "RPV130 %s: Global BUSY assertion failed; BUSY1 left untouched",
         context);
    return false;
  }
  uint8_t csr1 = 0;
  const int rc = rpv130_clear_busy1_and_disable(
    gVme, RPV130_BASE_ADDRESS, &csr1);
  if (rc != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name,
         "RPV130 %s: CLR1/disable/readback failed: status %d CSR1=0x%02X",
         context, rc, csr1);
    vme_odb::publish_rpv130_armed(false);
    return false;
  }
  gVmeState.rpv130_busy_configured = false;
  return publish_rpv130_busy_state(false, false);
}

static bool ensure_v792_settings_schema()
{
  return vme_odb::ensure_v792_settings_schema(v792_config::default_v792_settings());
}

static bool ensure_v1190_settings_schema()
{
  return vme_odb::ensure_v1190_settings_schema(v1190_config::default_settings());
}

static bool ensure_v775_settings_schema()
{
  return vme_odb::ensure_v775_settings_schema(v775_config::default_v775_settings());
}

using vme_odb::publish_module_info;
using vme_odb::read_v792_settings;
using vme_odb::read_v1190_settings;
using vme_odb::read_v775_settings;

static void decode_v7xx_runtime(V7xxRuntimeState &r, WORD s1, WORD s2,
                                DWORD counter)
{
  r.status1 = s1;
  r.status2 = s2;
  r.data_ready = !!(s1 & 0x1);
  r.busy = !!(s1 & 0x4);
  r.buffer_empty = !!(s2 & 0x2);
  r.buffer_full = !!(s2 & 0x4);
  r.event_counter = counter;
  r.dirty = true;
}

static void decode_v1190_runtime(WORD status, DWORD stored, DWORD counter)
{
  gVmeModuleState.v1190.status = status;
  gVmeModuleState.v1190.data_ready = !!(status & V1190_STATUS_DATA_READY);
  gVmeModuleState.v1190.almost_full = !!(status & V1190_STATUS_ALMOST_FULL);
  gVmeModuleState.v1190.full = !!(status & V1190_STATUS_FULL);
  gVmeModuleState.v1190.trigger_matching = !!(status & V1190_STATUS_TRIGGER_MATCH);
  gVmeModuleState.v1190.event_stored = stored;
  gVmeModuleState.v1190.event_counter = counter;
  gVmeModuleState.v1190.dirty = true;
}

static bool publish_v7xx_variables(const char *path, V7xxRuntimeState &r,
                   DWORD &last)
{
  return vme_odb::publish_v7xx_variables(path, r, last);
}

static bool publish_v1190_variables()
{
  return vme_odb::publish_v1190_variables(
    gVmeModuleState.v1190, gVmeModuleState.v1190_last_publish);
}

static void set_module_readback_valid(const char *path, bool valid)
{
  vme_odb::set_module_readback_valid(path, valid);
}

// Initialize VME module output records in ODB
static void initialize_module_output_schema()
{
  vme_odb::initialize_module_output_schema(
    gVmeModuleState.v792, gVmeModuleState.v1190, gVmeModuleState.v775,
    gVmeModuleState.v792_last_publish,
    gVmeModuleState.v1190_last_publish,
    gVmeModuleState.v775_last_publish);
}

// Initialize enabled VME module records in ODB
static bool initialize_other_module_odb()
{
  if (!ensure_v792_settings_schema() || !ensure_v1190_settings_schema() ||
    !ensure_v775_settings_schema()) return false;
  if (!publish_module_info(V792_INFO_PATH,V792_BASE,"A24_ND","D16","D32") ||
    !publish_module_info(V1190_INFO_PATH,V1190_BASE,"A24_ND","D16","D32") ||
    !publish_module_info(V775_INFO_PATH,V775_BASE,"A24_ND","D16","D32")) return false;
  initialize_module_output_schema();
  V792Settings a = {};
  V1190Settings b = {};
  V775Settings c = {};
  if (!read_v792_settings(a) || !read_v1190_settings(b) || !read_v775_settings(c)) return false;
  gVmeConfig.v792 = a;
  gV1190Config.run_settings = b;
  gVmeConfig.v775 = c;
  return true;
}

static bool ensure_v1720e_settings_schema()
{
  return vme_odb::ensure_v1720e_settings_schema(v1720e_config::default_settings());
}

using vme_odb::read_v1720e_settings;
using vme_odb::publish_v1720e_info;

// Forward V1720E readback validity updates to the ODB mapping
static void set_v1720e_readback_valid(bool valid)
{
  vme_odb::set_v1720e_readback_valid(valid);
}

static void update_v1720e_acquisition_status(DWORD status)
{
  const V1720E_ACQUISITION_STATE decoded = v1720e_decode_acquisition_status(status);
  gV1720State.runtime.value.acquisition_status = status;
  gV1720State.runtime.value.running = decoded.running;
  gV1720State.runtime.value.event_ready = decoded.event_ready;
  gV1720State.runtime.value.external_clock = decoded.external_clock;
  gV1720State.runtime.value.pll_locked = decoded.pll_locked;
  gV1720State.runtime.value.board_ready = decoded.board_ready;
  gV1720State.runtime.value.dirty = true;
}

static void update_v1720e_board_state(const V1720E_BOARD_INFO &info)
{
  gV1720State.runtime.value.acquisition_control = info.acquisition_control;
  update_v1720e_acquisition_status(info.acquisition_status);
  gV1720State.runtime.value.event_stored = info.event_stored;
  gV1720State.runtime.value.dirty = true;
}

static bool publish_v1720e_variables()
{
  return vme_odb::publish_v1720e_variables(
    gV1720State.runtime.value, gV1720State.runtime.last_variables_publish);
}

static void set_v1720e_communication_ok(bool ok)
{
  gV1720State.runtime.value.communication_ok = ok ? TRUE : FALSE;
  gV1720State.runtime.value.dirty = true;
  publish_v1720e_variables();
}

static void publish_v1720e_board_state(const V1720E_BOARD_INFO &info)
{
  update_v1720e_board_state(info);
  publish_v1720e_variables();
}

// Initialize V1720E output records in ODB
static void initialize_v1720e_output_schema()
{
  const V1720E_CONFIG_READBACK empty_readback = {};
  vme_odb::publish_v1720e_readback(empty_readback, false);
  gV1720State.runtime.value = {};
  gV1720State.runtime.value.dirty = true;
  publish_v1720e_variables();
}

// Initialize V1720E settings and status in ODB
static bool initialize_v1720e_odb()
{
  if (!ensure_v1720e_settings_schema() || !publish_v1720e_info())
    return false;
  initialize_v1720e_output_schema();
  V1720ESettings startup_settings = {};
  if (!read_v1720e_settings(startup_settings))
    return false;
  gV1720State.lifecycle.startup_enabled = startup_settings.enabled != FALSE;
  gV1720State.variables_enabled = gV1720State.lifecycle.startup_enabled;
  return true;
}

static bool snapshot_v1720e_run_settings()
{
  V1720ESettings settings = {};
  if (!read_v1720e_settings(settings) ||
      !v1720e_config::snapshot_run_settings(gV1720State.run, settings)) return false;
  gV1720State.variables_enabled = gV1720State.run.settings.enabled != FALSE;
  return true;
}

// Hold per-run VME statistics and diagnostic counters
struct VmeStatistics {
  RunStatistics run = {};
  bool run_counters_dirty = true;
  DWORD run_counters_last_publish = 0;
  std::atomic<unsigned> v1190_blt_diagnostics{0};
};
static VmeStatistics gVmeStatistics;

static bool publish_run_counters()
{
  return vme_odb::publish_run_counters(
    gVmeStatistics.run, gVmeStatistics.run_counters_dirty,
    gVmeStatistics.run_counters_last_publish);
}

static void mark_run_counters_dirty()
{
  gVmeStatistics.run_counters_dirty = true;
}

// Initialize per-run VME counters in ODB
static bool initialize_run_counters_odb()
{
  return publish_run_counters();
}

#if ENABLE_V1190_SOFT_TRIGGER_TEST
#endif

#if ENABLE_V775_SW_TRIGGER_TEST
static v775_config::V775DiagnosticState gV775Diagnostic;
#endif

using V792EventInfo = v792_readout::EventInfo;

using V1190EventInfo = v1190_readout::EventInfo;

using V775EventInfo = v775_readout::EventInfo;

enum class V1720StopOutcome { Disabled, AlreadyStopped, StopVerified };

static const char *v1720_stop_outcome_name(V1720StopOutcome outcome)
{
  switch (outcome) {
  case V1720StopOutcome::Disabled: return "disabled (not checked)";
  case V1720StopOutcome::AlreadyStopped: return "already stopped (no write)";
  case V1720StopOutcome::StopVerified: return "stop write and readback verified";
  }
  return "unknown";
}

static void invalidate_v1720e_current_state()
{
  gV1720State.runtime.value.communication_ok = FALSE;
  gV1720State.runtime.value.acquisition_control = 0;
  gV1720State.runtime.value.acquisition_status = 0;
  gV1720State.runtime.value.running = FALSE;
  gV1720State.runtime.value.event_ready = FALSE;
  gV1720State.runtime.value.external_clock = FALSE;
  gV1720State.runtime.value.pll_locked = FALSE;
  gV1720State.runtime.value.board_ready = FALSE;
  gV1720State.runtime.value.event_stored = 0;
  gV1720State.runtime.value.dirty = true;
}

static bool stop_v1720e_and_publish_state(
  const char *context, V1720StopOutcome *outcome = nullptr)
{
  if (!gV1720State.lifecycle.startup_enabled && !gV1720State.lifecycle.start_attempted && !gV1720State.lifecycle.started) {
    if (outcome) *outcome = V1720StopOutcome::Disabled;
    return true;
  }

  if (!gVme) {
    cm_msg(MERROR, frontend_name,
         "Cannot stop V1720E during %s: VME interface is unavailable",
         context);
    invalidate_v1720e_current_state();
    publish_v1720e_variables();
    return false;
  }

  DWORD control = 0, acquisition_status = 0;
  int stop_attempted = 0;
  const int stop_status = v1720e_stop_if_running(
    gVme, V1720E_BASE, &control, &acquisition_status, &stop_attempted);
  if (stop_status != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name,
         "V1720E stop/verification failed during %s: status %d, "
         "stop_attempted=%d, AcqControl=0x%08X AcqStatus=0x%08X",
         context, stop_status, stop_attempted, control,
         acquisition_status);
    invalidate_v1720e_current_state();
    publish_v1720e_variables();
    return false;
  }

  V1720E_BOARD_INFO state = {};
  const int read_status = v1720e_probe(gVme, V1720E_BASE, &state);
  if (read_status != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name,
         "V1720E post-stop probe failed during %s: status %d",
         context, read_status);
    invalidate_v1720e_current_state();
    publish_v1720e_variables();
    return false;
  }
  if ((state.acquisition_control & V1720E_ACQ_RUN) ||
    (state.acquisition_status & V1720E_ACQ_RUN)) {
    cm_msg(MERROR, frontend_name,
         "V1720E still RUN during %s: AcqControl=0x%08X AcqStatus=0x%08X",
         context, state.acquisition_control, state.acquisition_status);
    invalidate_v1720e_current_state();
    publish_v1720e_variables();
    return false;
  }

  gV1720State.lifecycle.start_attempted = false;
  gV1720State.lifecycle.started = false;
  if (outcome) *outcome = stop_attempted
    ? V1720StopOutcome::StopVerified : V1720StopOutcome::AlreadyStopped;
  gV1720State.runtime.value.communication_ok = TRUE;
  update_v1720e_board_state(state);
  publish_v1720e_variables();
  cm_msg(MINFO, frontend_name,
       "V1720E %s during %s: AcqControl=0x%08X AcqStatus=0x%08X",
       stop_attempted ? "stop verified" : "already stopped; no write",
       context, state.acquisition_control, state.acquisition_status);
  return true;
}

/* Low-level VME access helpers. */
static bool vme_read16_at(MVME_INTERFACE *vme, DWORD address, WORD &value, const char *description)
{
  int saved_mode;
  if (mvme_get_dmode(vme, &saved_mode) != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name, "Cannot get VME data mode for %s", description);
    return false;
  }
  bool ok = false;
  if (mvme_set_dmode(vme, MVME_DMODE_D16) != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name, "Cannot select D16 for %s", description);
  } else {
    const int status = mvme_read(vme, &value, address, sizeof(value));
    if (status == MVME_SUCCESS)
      ok = true;
    else
      cm_msg(MERROR, frontend_name, "%s read failed at 0x%08X: status %d",
           description, address, status);
  }
  if (mvme_set_dmode(vme, saved_mode) != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name, "Cannot restore VME data mode after %s", description);
    ok = false;
  }
  return ok;
}

static bool vme_read16(DWORD address, WORD &value, const char *description)
{
  return vme_read16_at(gVme, address, value, description);
}

static bool vme_read32_at(MVME_INTERFACE *vme, DWORD address, DWORD &value, const char *description)
{
  int saved_mode;
  if (mvme_get_dmode(vme, &saved_mode) != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name, "Cannot get VME data mode for %s", description);
    return false;
  }
  bool ok = false;
  if (mvme_set_dmode(vme, MVME_DMODE_D32) != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name, "Cannot select D32 for %s", description);
  } else {
    const int status = mvme_read(vme, &value, address, sizeof(value));
    if (status == MVME_SUCCESS)
      ok = true;
    else
      cm_msg(MERROR, frontend_name, "%s read failed at 0x%08X: status %d",
           description, address, status);
  }
  if (mvme_set_dmode(vme, saved_mode) != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name, "Cannot restore VME data mode after %s", description);
    ok = false;
  }
  return ok;
}

static bool vme_read32(DWORD address, DWORD &value, const char *description)
{
  return vme_read32_at(gVme, address, value, description);
}

static bool vme_write16_at(MVME_INTERFACE *vme, DWORD address, WORD value, const char *description)
{
  int saved_mode;
  if (mvme_get_dmode(vme, &saved_mode) != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name, "Cannot get VME data mode for %s", description);
    return false;
  }
  bool ok = false;
  if (mvme_set_dmode(vme, MVME_DMODE_D16) != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name, "Cannot select D16 for %s", description);
  } else {
    const int status = mvme_write(vme, address, &value, sizeof(value));
    if (status == MVME_SUCCESS)
      ok = true;
    else
      cm_msg(MERROR, frontend_name, "%s write failed at 0x%08X: status %d",
           description, address, status);
  }
  if (mvme_set_dmode(vme, saved_mode) != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name, "Cannot restore VME data mode after %s", description);
    ok = false;
  }
  return ok;
}

static bool vme_write16(DWORD address, WORD value, const char *description)
{
  return vme_write16_at(gVme, address, value, description);
}

// Bind V792 configuration to the existing checked VME access functions.
static v792_config::Access v792_configuration_access()
{
  return {gVme, V792_BASE, frontend_name, vme_read16_at, vme_write16_at, v792_ThresholdRead};
}

// Bind V775 configuration to the existing checked VME access functions.
static v775_config::Access v775_configuration_access()
{
  return {gVme, V775_BASE, frontend_name, vme_read16_at, vme_write16_at, v775_ThresholdRead};
}

// Bind V1190 clear to the existing checked VME access functions.
static v1190_clear::Access v1190_clear_access()
{
  return {gVme, V1190_BASE, vme_read16_at, vme_write16_at};
}

// Bind V1190 configuration to the existing VME access functions.
static v1190_config::Access v1190_configuration_access()
{
  const v1190_micro::Access micro = {gVme, V1190_BASE, frontend_name, vme_read16_at, vme_write16_at};
  return {vme_read16, vme_write16,
      [micro](WORD opcode) { return v1190_micro::write_opcode(micro, opcode); },
      [micro](WORD opcode, const WORD *words, size_t count) {
        return v1190_micro::write_command(micro, opcode, words, count);
      },
      [micro](WORD opcode, WORD *words, size_t count) {
        return v1190_micro::read_command(micro, opcode, words, count);
      },
      [micro](WORD &mode) { return v1190_config::read_acquisition_mode(micro, mode); }};
}

// Connect the existing VME BLT transfer to V792 and V1190 readers.
static int v792_blt_transfer(void *context, uint32_t address, void *destination,
               int requested_bytes, int *actual_bytes)
{
  const MVME_INTERFACE *vme = static_cast<MVME_INTERFACE *>(context);
  return caenvme_a24_blt_read32(vme->handle, address, destination,
                  requested_bytes, actual_bytes);
}

// Keep V792 run state and BLT failure handling in the frontend.
static V792EventInfo read_v792_event(DWORD (&data)[V792_MAX_EVENT_WORDS])
{
  const v792_readout::Access access = {gVme, V792_BASE, frontend_name,
                      mvme_get_dmode, mvme_set_dmode, mvme_read,
                      v792_blt_transfer, gVme};
  if (V792_READOUT_MODE_SELECT == V792_BLT32) {
    const auto result = v792_readout::read_blt32_event(access, data);
    static std::atomic<unsigned> diagnostic_count{0};
    if (result.details.requested_bytes != 0 &&
      diagnostic_count.fetch_add(1, std::memory_order_relaxed) < 10) {
      cm_msg(MINFO, frontend_name,
           "V792 BLT32: requested=%d actual=%d bytes CAEN status=%d validation=%d",
           result.details.requested_bytes, result.details.actual_bytes,
           result.details.caen_status, static_cast<int>(result.status));
    }
    if (result.stop_required) {
      gVmeState.blt_stop_requested.store(true, std::memory_order_relaxed);
      cm_msg(MERROR, frontend_name,
           "V792 BLT32 readout failed: validation=%d CAEN status=%d requested=%d actual=%d restore=%d",
           static_cast<int>(result.status), result.details.caen_status,
           result.details.requested_bytes, result.details.actual_bytes, result.restore_status);
      return {};
    }
    if (result.event.valid) {
      gVmeModuleState.v792.event_counter = result.event.event_counter;
      gVmeModuleState.v792.dirty = true;
    }
    return result.event;
  }

  const auto event = v792_readout::read_single_event(access, data);
  if (event.footer_consumed) {
    gVmeModuleState.v792.event_counter = event.event_counter;
    gVmeModuleState.v792.dirty = true;
  }
  return event;
}

// Keep V1190 run state and FIFO/BLT failure handling in the frontend.
static V1190EventInfo read_v1190_event(DWORD (&data)[V1190_MAX_EVENT_WORDS])
{
  const v1190_readout::Access access = {gVme, V1190_BASE, frontend_name,
                      mvme_get_dmode, mvme_set_dmode, mvme_read,
                      vme_read16_at, vme_read32_at, v792_blt_transfer, gVme};
  if (V1190_READOUT_MODE_SELECT != V1190_EVENT_FIFO_BLT32) {
    const auto event = v1190_readout::read_single_event(access, data);
    if (event.trailer_consumed) {
      gVmeModuleState.v1190.event_counter = event.event_counter;
      gVmeModuleState.v1190.dirty = true;
    }
    return event;
  }

  const auto readout = v1190_readout::read_fifo_blt32_event(access, data, V1190_FIFO_STRICT_SYNC_CHECK,
                                 gVmeState.v1190_fifo_blt);
  const auto &result = readout.details;
  const auto status = readout.status;
  const bool diagnostic = gVmeStatistics.v1190_blt_diagnostics.fetch_add(1, std::memory_order_relaxed) < 10;
  if (diagnostic && status != V1190_FIFO_BLT_OK) {
    cm_msg(MINFO, frontend_name,
         "V1190 BLT FIFO=%u stored=%d->%d req=%d got=%d CAEN=%d",
         static_cast<unsigned>(result.fifo_word_count),
         result.timing.stored_before_checked ? static_cast<int>(result.stored_before) : -1,
         result.timing.stored_after_checked ? static_cast<int>(result.stored_after) : -1,
         result.requested_bytes, result.actual_bytes, result.caen_status);
    cm_msg(MINFO, frontend_name,
         "V1190 BLT trailer=%u ctr=%04X/%06X match=%d status=%d",
         static_cast<unsigned>(result.trailer_word_count),
         static_cast<unsigned>(result.fifo_event_counter),
         static_cast<unsigned>(result.event_counter),
         result.counter_consistent, static_cast<int>(status));
  }
  if (readout.stop_required) {
    /* The FIFO entry and/or Output Buffer may have advanced. Never retry. */
    gVmeState.blt_stop_requested.store(true, std::memory_order_relaxed);
    if (!V1190_FIFO_STRICT_SYNC_CHECK && status == V1190_FIFO_BLT_STORED_MISMATCH)
      cm_msg(MERROR, frontend_name, "V1190 FIFO periodic sync failed: stored_after=%u expected=0",
           static_cast<unsigned>(result.stored_after));
    cm_msg(MERROR, frontend_name,
         "V1190 BLT error s=%d c=%d n=%u req=%d got=%d",
         static_cast<int>(status), result.caen_status,
         static_cast<unsigned>(result.fifo_word_count),
         result.requested_bytes, result.actual_bytes);
    return {};
  }
  if (readout.event.valid) {
    gVmeModuleState.v1190.event_counter = readout.event.event_counter;
    gVmeModuleState.v1190.dirty = true;
  }
  return readout.event;
}

static bool wait_for_v1190_data_ready()
{
  WORD status = 0;
  for (unsigned poll = 0; poll < V1190_READY_MAX_POLLS; ++poll) {
    if (!vme_read16(V1190_BASE + V1190_STATUS, status, "V1190 Status"))
      return false;
    decode_v1190_runtime(status,gVmeModuleState.v1190.event_stored,gVmeModuleState.v1190.event_counter);
    if (status & V1190_STATUS_DATA_READY)
      return true;
    if (poll + 1 < V1190_READY_MAX_POLLS)
      ss_sleep(1);
  }
  cm_msg(MERROR, frontend_name,
       "V1190 DataReady timeout after %u polls; V792 FIFO was not consumed",
       V1190_READY_MAX_POLLS);
  return false;
}

static bool wait_for_v775_data_ready()
{
  for (unsigned poll = 0; poll < V775_READY_MAX_POLLS; ++poll) {
    if (v775_DataReady(gVme, V775_BASE))
      return true;
    if (poll + 1 < V775_READY_MAX_POLLS)
      ss_sleep(1);
  }
  cm_msg(MERROR, frontend_name,
       "V775 DataReady timeout after %u polls; V792 FIFO was not consumed",
       V775_READY_MAX_POLLS);
  return false;
}
static bool wait_for_v1720e_data_ready()
{
  DWORD event_stored = 0;
  DWORD acquisition_status = 0;
  for (unsigned poll = 0; poll < V1720E_READY_MAX_POLLS; ++poll) {
    int ready = 0;
    const int status = v1720e_data_ready(gVme, V1720E_BASE, &ready,
                      &event_stored,
                      &acquisition_status);
    if (status != MVME_SUCCESS) {
      gV1720State.runtime.value.communication_ok = FALSE;
      gV1720State.runtime.value.dirty = true;
      ++gVmeStatistics.run.v1720_read_timeout_count;
      mark_run_counters_dirty();
      cm_msg(MERROR, frontend_name,
           "V1720E ready read failed: status %d", status);
      return false;
    }
    update_v1720e_acquisition_status(acquisition_status);
    gV1720State.runtime.value.event_stored = event_stored;
    if (ready)
      return true;
    if (poll + 1 < V1720E_READY_MAX_POLLS)
      ss_sleep(1);
  }
  ++gVmeStatistics.run.v1720_read_timeout_count;
  mark_run_counters_dirty();
  cm_msg(MERROR, frontend_name,
       "V1720E DataReady timeout after %u polls (Event Stored %u); "
       "V792 FIFO was not consumed",
       V1720E_READY_MAX_POLLS, event_stored);
  return false;
}

/* Diagnostic-only configuration and restoration. */
#if ENABLE_V1190_SOFT_TRIGGER_TEST
// Restore saved V1190 diagnostic configuration
static bool restore_v1190_diagnostic_settings()
{
  const auto access = v1190_configuration_access();
  return v1190_config::restore_diagnostic_settings(access, gV1190Config.diagnostic);
}

// Set up the V1190 soft trigger diagnostic
static bool setup_v1190_soft_trigger_test()
{
  const auto access = v1190_configuration_access();
  return v1190_config::setup_soft_trigger_test(access, gV1190Config.diagnostic);
}

#endif

#if ENABLE_V775_SW_TRIGGER_TEST
static v775_config::DiagnosticAccess v775_diagnostic_access()
{
  return {gVme, V775_BASE, vme_read16_at, vme_write16_at, v775_EvtCntRead, ss_sleep};
}

static bool restore_v775_diagnostic_settings()
{
  WORD readback = 0;
  const auto result = v775_config::restore_diagnostic_settings(
    v775_diagnostic_access(), gV775Diagnostic, readback);
  if (result == v775_config::RestoreResult::AccessFailure) return false;
  if (result == v775_config::RestoreResult::VerifyFailure) {
    cm_msg(MERROR, frontend_name,
         "V775 Empty Program restoration verify failed: Bit Set 2=0x%04X",
         readback);
    return false;
  }
  if (result == v775_config::RestoreResult::Restored)
    cm_msg(MINFO, frontend_name,
         "V775 Empty Program restored to disabled (Bit Set 2=0x%04X)",
         readback);
  else if (result == v775_config::RestoreResult::NoClearNeeded)
    cm_msg(MINFO, frontend_name,
         "V775 Empty Program restoration needs no clear; original state was %s",
         (gV775Diagnostic.saved_bit_set2 & V775_BIT2_EMPTY_PROGRAM) ? "enabled" : "disabled");
  return true;
}

static bool setup_v775_sw_trigger_test()
{
  const auto access = v775_diagnostic_access();
  WORD readback = 0;
  WORD status1 = 0;
  WORD status2 = 0;
  DWORD counter = 0;

  if (!v775_config::save_diagnostic_settings(access, gV775Diagnostic)) return false;
  const WORD bitset2 = gV775Diagnostic.saved_bit_set2;
  cm_msg(MINFO, frontend_name,
       "V775 diagnostic saved Bit Set 2=0x%04X; Empty Program=%s",
       bitset2,
       (bitset2 & V775_BIT2_EMPTY_PROGRAM) ? "enabled" : "disabled");

  const auto enabled = v775_config::enable_empty_program(access, gV775Diagnostic, readback);
  if (enabled == v775_config::EnableResult::AccessFailure ||
    enabled == v775_config::EnableResult::VerifyFailure) {
    cm_msg(MERROR, frontend_name, "V775 Empty Program enable/readback failed");
    restore_v775_diagnostic_settings();
    return false;
  }
  if (enabled == v775_config::EnableResult::Enabled)
    cm_msg(MINFO, frontend_name,
         "V775 Empty Program temporarily enabled (Bit Set 2=0x%04X)",
         readback);

  if (!v775_config::read_before_sw_comm(access, status1, status2, counter)) {
    restore_v775_diagnostic_settings();
    return false;
  }
  printf("V775 SW trigger test enabled: issuing one SW Comm at begin of run.\n");
  printf("V775 before SW Comm: Status1=0x%04X Status2=0x%04X Event Counter=0x%06X\n",
       status1, status2, counter);

  if (!v775_config::issue_sw_comm(access)) {
    restore_v775_diagnostic_settings();
    return false;
  }

  unsigned polls_done = 0;
  const auto poll = v775_config::poll_after_sw_comm(access, status1, polls_done);
  if (poll == v775_config::PollResult::ReadFailure) {
    restore_v775_diagnostic_settings();
    return false;
  }
  if (!v775_config::read_after_sw_comm(access, status2, counter)) {
    restore_v775_diagnostic_settings();
    return false;
  }
  const bool ready = poll == v775_config::PollResult::Ready;
  printf("V775 after SW Comm poll %u/%u: DataReady=%s Status1=0x%04X Status2=0x%04X Event Counter=0x%06X\n",
       polls_done, v775_config::kDiagnosticMaxPolls, ready ? "Y" : "N",
       status1, status2, counter);
  if (!ready) {
    cm_msg(MERROR, frontend_name,
         "V775 SW trigger test DataReady timeout after %u polls",
         v775_config::kDiagnosticMaxPolls);
    restore_v775_diagnostic_settings();
    return false;
  }
  return true;
}

#endif

/* Frontend initialization checks. Keep the established read-only access order. */
// Check enabled VME modules in their established startup order
static bool check_module_communication(bool check_v1720e)
{
  if (gVmeConfig.v792.enabled) {
    printf("Checking V792 at 0x%08X...\n", V792_BASE);
    if (!v792_isPresent(gVme, V792_BASE)) {
      gVmeModuleState.v792.communication_ok = FALSE;
      publish_v7xx_variables(V792_VARIABLES_PATH, gVmeModuleState.v792, gVmeModuleState.v792_last_publish);
      cm_msg(MERROR, frontend_name, "V792 not found at 0x%08X", V792_BASE);
      return false;
    }
    gVmeModuleState.v792.communication_ok = TRUE;
    gVmeModuleState.v792.dirty = true;
    printf("V792 detected.\n");
  } else {
    gVmeModuleState.v792 = {};
    gVmeModuleState.v792.dirty = true;
    set_module_readback_valid(V792_READBACK_PATH, false);
    publish_v7xx_variables(V792_VARIABLES_PATH, gVmeModuleState.v792, gVmeModuleState.v792_last_publish);
    printf("V792 disabled in ODB; communication check skipped.\n");
  }

  WORD v1190_status = 0;
  WORD v1190_events = 0;
  if (gV1190Config.run_settings.enabled) {
    printf("Checking V1190A at 0x%08X...\n", V1190_BASE);
    if (!vme_read16(V1190_BASE + V1190_STATUS, v1190_status, "V1190 Status") ||
        !vme_read16(V1190_BASE + V1190_EVENT_STORED, v1190_events, "V1190 Event Stored")) {
      gVmeModuleState.v1190.communication_ok = FALSE;
      publish_v1190_variables();
      cm_msg(MERROR, frontend_name, "V1190A communication check failed at 0x%08X", V1190_BASE);
      return false;
    }
    gVmeModuleState.v1190.communication_ok = TRUE;
    decode_v1190_runtime(v1190_status, v1190_events, gVmeModuleState.v1190.event_counter);
    printf("V1190A detected: Status=0x%04X Event Stored=%u.\n",
           v1190_status, v1190_events);
  } else {
    gVmeModuleState.v1190 = {};
    gVmeModuleState.v1190.dirty = true;
    set_module_readback_valid(V1190_READBACK_PATH, false);
    publish_v1190_variables();
    printf("V1190 disabled in ODB; communication check skipped.\n");
  }

  if (gVmeConfig.v775.enabled) {
    printf("Checking V775 at 0x%08X...\n", V775_BASE);
    if (!v775_isPresent(gVme, V775_BASE)) {
      gVmeModuleState.v775.communication_ok = FALSE;
      publish_v7xx_variables(V775_VARIABLES_PATH, gVmeModuleState.v775, gVmeModuleState.v775_last_publish);
      cm_msg(MERROR, frontend_name, "V775 not found at 0x%08X", V775_BASE);
      return false;
    }
    gVmeModuleState.v775.communication_ok = TRUE;
    gVmeModuleState.v775.dirty = true;
    printf("V775 detected.\n");
  } else {
    gVmeModuleState.v775 = {};
    gVmeModuleState.v775.dirty = true;
    set_module_readback_valid(V775_READBACK_PATH, false);
    publish_v7xx_variables(V775_VARIABLES_PATH, gVmeModuleState.v775, gVmeModuleState.v775_last_publish);
    printf("V775 disabled in ODB; communication check skipped.\n");
  }

  if (!check_v1720e) {
    set_v1720e_communication_ok(false);
    set_v1720e_readback_valid(false);
    printf("V1720E disabled in ODB; communication check skipped.\n");
    return true;
  }

  V1720E_BOARD_INFO v1720 = {};
  printf("Checking V1720E at 0x%08X...\n", V1720E_BASE);
  const int v1720_status = v1720e_probe(gVme, V1720E_BASE, &v1720);
  if (v1720_status != MVME_SUCCESS ||
    !v1720e_decode_board_info(v1720.board_info).supported) {
    cm_msg(MERROR, frontend_name,
         "V1720E identification failed at 0x%08X: status %d "
         "BoardInfo 0x%08X",
         V1720E_BASE, v1720_status, v1720.board_info);
    set_v1720e_communication_ok(false);
    set_v1720e_readback_valid(false);
    return false;
  }
  printf("V1720E detected: BoardInfo=0x%08X ROC-FW=0x%08X "
       "AcqControl=0x%08X AcqStatus=0x%08X EventStored=%u.\n",
       v1720.board_info, v1720.roc_firmware,
       v1720.acquisition_control, v1720.acquisition_status,
       v1720.event_stored);
  set_v1720e_communication_ok(true);
  publish_v1720e_board_state(v1720);
  return true;
}

/* POUT is a frontend hardware setting, independent of BOR run settings. */

// Configure V1190 startup POUT and record its readback
static bool configure_v1190_pout_startup()
{
  const auto access = v1190_configuration_access();
  return v1190_config::configure_pout_startup(access,
                         gV1190Config.run_settings.enabled != FALSE);
}

// Validate module settings before capturing the run configuration.
static bool validate_and_snapshot_module_settings()
{
  V792Settings a = {};
  V1190Settings b = {};
  V775Settings c = {};
  if (!read_v792_settings(a) || !read_v1190_settings(b) || !read_v775_settings(c)) return false;
  if (a.enabled && a.iped > 0xFF) {
    cm_msg(MERROR, frontend_name, "V792 Iped exceeds 8-bit range");
    return false;
  }
  WORD r = 0, d = 0, h = 0, m[V1190_CHANNEL_MASK_WORDS] = {};
  if (b.enabled && !v1190_config::encode_semantics(b, r, d, h, m)) return false;
  if (c.enabled && c.full_scale_range > 0xFF) {
    cm_msg(MERROR, frontend_name, "V775 FullScaleRange exceeds 8-bit range");
    return false;
  }
  gVmeConfig.v792 = a;
  gV1190Config.run_settings = b;
  gVmeConfig.v775 = c;
  return true;
}

static bool snapshot_rpv130_enabled_for_run()
{
  char path[256];
  BOOL enabled = FALSE, single_event_busy = FALSE;
  if (!make_odb_path(path, sizeof(path), RPV130_SETTINGS_PATH, "Enabled") ||
    !get_absolute_odb_value(path, &enabled, sizeof(enabled), TID_BOOL))
    return false;
  gVmeState.rpv130_enabled_for_run = enabled != FALSE;
  if (!make_odb_path(path, sizeof(path), RPV130_SETTINGS_PATH,
             "SingleEventBusyEnabled") ||
    !get_absolute_odb_value(path, &single_event_busy,
                sizeof(single_event_busy), TID_BOOL))
    return false;
  gVmeState.single_event_busy_enabled_for_run = single_event_busy != FALSE;
  return true;
}

static void capture_vme_requested_snapshot()
{
  gVmeModuleState.snapshot.v792_requested = gVmeConfig.v792;
  gVmeModuleState.snapshot.v1190_requested = gV1190Config.run_settings;
  gVmeModuleState.snapshot.v775_requested = gVmeConfig.v775;
  gVmeModuleState.snapshot.v1720e_requested = gV1720State.run.settings;
  gVmeModuleState.snapshot.rpv130_enabled =
    gVmeState.rpv130_enabled_for_run ? TRUE : FALSE;
  gVmeModuleState.snapshot.rpv130_single_event_busy_enabled =
    gVmeState.single_event_busy_enabled_for_run ? TRUE : FALSE;
  gVmeModuleState.snapshot.enabled_for_run =
    gVmeState.frontend_enabled_for_run ? TRUE : FALSE;
}

// Publish the enabled modules captured for this run
static void publish_vme_enabled_for_run()
{
  gVmeModuleState.v792.enabled_for_run = gVmeConfig.v792.enabled;
  gVmeModuleState.v1190.enabled_for_run = gV1190Config.run_settings.enabled;
  gVmeModuleState.v775.enabled_for_run = gVmeConfig.v775.enabled;
  gV1720State.runtime.value.enabled_for_run = gV1720State.run.settings.enabled;
  gVmeModuleState.v792.dirty = true;
  gVmeModuleState.v1190.dirty = true;
  gVmeModuleState.v775.dirty = true;
  gV1720State.runtime.value.dirty = true;
  publish_v7xx_variables(V792_VARIABLES_PATH, gVmeModuleState.v792,
               gVmeModuleState.v792_last_publish);
  publish_v1190_variables();
  publish_v7xx_variables(V775_VARIABLES_PATH, gVmeModuleState.v775,
               gVmeModuleState.v775_last_publish);
  publish_v1720e_variables();
  if (gVmeState.rpv130_enabled_for_run) {
    vme_odb::publish_rpv130_enabled_for_run(true);
  } else {
    vme_odb::publish_rpv130_disabled_state();
  }
}

static bool validate_v792_event_source_dependency()
{
  if (!gVmeConfig.v792.enabled &&
    (gV1190Config.run_settings.enabled || gVmeConfig.v775.enabled ||
     gV1720State.run.settings.enabled)) {
    cm_msg(MERROR, frontend_name,
         "V792 must be enabled when any VME physics readout module "
         "is enabled");
    return false;
  }
  return true;
}

static bool configure_v792_for_run()
{
  if (!gVmeConfig.v792.enabled) return true;
  return v792_config::configure_for_run(v792_configuration_access(), gVmeConfig.v792);
}

// Apply V1190 settings for the run
static bool configure_v1190_for_run()
{
  const auto access = v1190_configuration_access();
  return v1190_config::configure_for_run(
    access, gV1190Config.run_settings,
    V1190_READOUT_MODE_SELECT == V1190_EVENT_FIFO_BLT32);
}

static bool configure_v775_for_run()
{
  if (!gVmeConfig.v775.enabled) return true;
  return v775_config::configure_for_run(v775_configuration_access(), gVmeConfig.v775);
}
// Configure and verify V1720E using the captured run settings
static bool configure_v1720e_for_run()
{
  if (!gV1720State.run.settings.enabled) {
    set_v1720e_communication_ok(false);
    set_v1720e_readback_valid(false);
    printf("  V1720E: DISABLED by BOR Settings snapshot; configuration skipped.\n");
    return true;
  }
  V1720E_BOARD_INFO before = {};
  const int probe_status = v1720e_probe(gVme, V1720E_BASE, &before);
  if (probe_status != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name,
         "V1720E pre-configuration read failed: status %d",
         probe_status);
    set_v1720e_communication_ok(false);
    set_v1720e_readback_valid(false);
    return false;
  }
  printf("  V1720E Event Stored before BOR configuration: %u\n",
       before.event_stored);
  const int status = v1720e_configure(gVme, V1720E_BASE,
                    &gV1720State.run.hardware);
  if (status != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name,
         "V1720E configuration/readback failed: status %d", status);
    set_v1720e_communication_ok(false);
    set_v1720e_readback_valid(false);
    return false;
  }
  V1720E_CONFIG_READBACK readback = {};
  const int readback_status =
    v1720e_read_configuration(gVme, V1720E_BASE, &readback);
  if (readback_status != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name,
         "V1720E configuration readback failed: status %d",
         readback_status);
    set_v1720e_communication_ok(false);
    set_v1720e_readback_valid(false);
    return false;
  }
  const bool verified = v1720e_config::verify_readback(gV1720State.run.hardware, readback);
  v1720e_config::capture_readback(gVmeModuleState.snapshot.v1720e_readback, readback, verified);
  vme_odb::publish_v1720e_readback(readback, verified);
  if (!verified) {
    set_v1720e_communication_ok(false);
    return false;
  }
  printf("  V1720E: BufferOrg=0x%X CustomSize=0x%X PostTrigger=0x%X "
       "TriggerSource=0x%08X ChannelMask=0x%02X\n",
       gV1720State.run.hardware.buffer_organization,
       gV1720State.run.hardware.custom_size, gV1720State.run.hardware.post_trigger,
       gV1720State.run.hardware.trigger_source, gV1720State.run.hardware.channel_enable);
  return true;
}

// Verify V792 registers and publish their readback
static bool verify_v792_configuration()
{
  if(!gVmeConfig.v792.enabled) { set_module_readback_valid(V792_READBACK_PATH,false); return true; }
  v792_config::Readback result;
  const auto status = v792_config::verify_configuration(v792_configuration_access(), gVmeConfig.v792, result);
  if (status == v792_config::VerifyStatus::ReadFailure) return false;
  const bool ok = status == v792_config::VerifyStatus::Matched;
  v792_config::capture_v792_readback(
    gVmeModuleState.snapshot.v792_readback, result.firmware, result.iped, result.bits,
    result.zero_suppression, result.all_trigger, result.thresholds, ok);
  vme_odb::publish_v792_readback(result.firmware, result.iped, result.zero_suppression,
                   result.all_trigger, result.bits, result.thresholds, ok);
  return ok;
}

// Verify V1190 settings and publish the BOR readback
static bool verify_v1190_configuration()
{
  const auto access = v1190_configuration_access();
  return v1190_config::verify_configuration(
    access, gV1190Config.run_settings,
    V1190_READOUT_MODE_SELECT == V1190_EVENT_FIFO_BLT32,
    gVmeModuleState.snapshot.v1190_readback);
}

// Verify V775 registers and publish their readback
static bool verify_v775_configuration()
{
  if(!gVmeConfig.v775.enabled) { set_module_readback_valid(V775_READBACK_PATH,false); return true; }
  v775_config::Readback result;
  const auto status = v775_config::verify_configuration(v775_configuration_access(), gVmeConfig.v775, result);
  if (status == v775_config::VerifyStatus::ReadFailure) return false;
  const bool ok = status == v775_config::VerifyStatus::Matched;
  v775_config::capture_v775_readback(
    gVmeModuleState.snapshot.v775_readback, result.firmware, result.full_scale, result.fast_clear,
    result.bits, result.thresholds, ok);
  vme_odb::publish_v775_readback(result.firmware, result.full_scale, result.fast_clear,
                   result.bits, result.thresholds, ok);
  return ok;
}
// Clear participating module event buffers
static bool clear_module_buffers()
{
  if (gVmeConfig.v792.enabled && !v792_config::clear_data(v792_configuration_access(), false)) return false;
  if (gV1190Config.run_settings.enabled && !v1190_clear::software_clear(v1190_clear_access(), "V1190 Software Clear")) return false;
  if (gVmeConfig.v775.enabled && !v775_config::clear_data(v775_configuration_access(), false)) return false;
  v1190_clear::CheckResult v1190_check = v1190_clear::CheckResult::Empty;
  if (gV1190Config.run_settings.enabled) {
    v1190_check = v1190_clear::verify_clear(v1190_clear_access(), "V1190 Status after clear");
    if (v1190_check == v1190_clear::CheckResult::ReadFailed) return false;
  }
  if ((gVmeConfig.v792.enabled && v792_DataReady(gVme,V792_BASE)) ||
    (gV1190Config.run_settings.enabled && v1190_check == v1190_clear::CheckResult::DataReady) ||
    (gVmeConfig.v775.enabled && v775_DataReady(gVme,V775_BASE))) {
    cm_msg(MERROR, frontend_name, "Buffer clear verify failed: DataReady remains asserted");
    return false;
  }
  return true;
}

static void set_vme_clear_results(const std::string &value)
{
  gBufferClearResults.v792 = value;
  gBufferClearResults.v1190 = value;
  gBufferClearResults.v775 = value;
  gBufferClearResults.v1720e = value;
}

struct ManualBufferClearRequest {
  DWORD request_id = 0;
  uint64_t unix_time = 0;
  V792Settings v792 = {};
  V1190Settings v1190 = {};
  V775Settings v775 = {};
  V1720ESettings v1720 = {};
};

struct ManualBufferClearExecution {
  bool all_ok = true;
  std::string errors;
};

// Validate a stopped-state manual buffer clear request
static bool validate_manual_buffer_clear_request(ManualBufferClearRequest &request)
{
  DWORD &request_id = request.request_id;
  const uint64_t &unix_time = request.unix_time;
  if (!get_absolute_odb_value(BUFFER_CLEAR_COMMAND_PATH, &request_id,
                sizeof(request_id), TID_DWORD) ||
    request_id <= gBufferClearStatus.last_handled_request_id)
    return false;

  const daq::BufferClearTransition transition =
    daq::beginBufferClearRequest(gBufferClearStatus, request_id);
  if (!transition.handled)
    return false;
  gBufferClearStatus = transition.pending;
  set_vme_clear_results("Not attempted");
  vme_odb::publish_buffer_clear_status(gBufferClearStatus, gBufferClearResults);

  const time_t now = time(NULL);
  request.unix_time = now < 0 ? 0 : static_cast<uint64_t>(now);
  INT current_run_state = 0;
  if (!get_absolute_odb_value("/Runinfo/State", &current_run_state,
                sizeof(current_run_state), TID_INT)) {
    gBufferClearStatus = daq::rejectBufferClearRequest(
      gBufferClearStatus, "Cannot verify MIDAS Run state", unix_time);
    set_vme_clear_results("Not attempted: Run state unavailable");
    vme_odb::publish_buffer_clear_status(gBufferClearStatus, gBufferClearResults);
    return false;
  }
  if (current_run_state != STATE_STOPPED) {
    const daq::BufferClearRunState state =
      current_run_state == STATE_RUNNING
        ? daq::BufferClearRunState::kRunning
        : (current_run_state == STATE_PAUSED
             ? daq::BufferClearRunState::kPaused
             : daq::BufferClearRunState::kUnknown);
    const std::string error =
      daq::bufferClearRunStateRejection(state, "VME");
    gBufferClearStatus = daq::rejectBufferClearRequest(
      gBufferClearStatus, error, unix_time);
    set_vme_clear_results("Not attempted: " + error);
    vme_odb::publish_buffer_clear_status(gBufferClearStatus, gBufferClearResults);
    cm_msg(MINFO, frontend_name, "Request %u rejected: %s", request_id,
         error.c_str());
    return false;
  }
  if (!gVme) {
    gBufferClearStatus = daq::rejectBufferClearRequest(
      gBufferClearStatus, "VME interface is not open", unix_time);
    set_vme_clear_results("Not attempted: VME interface unavailable");
    vme_odb::publish_buffer_clear_status(gBufferClearStatus, gBufferClearResults);
    return false;
  }

  V792Settings &v792 = request.v792;
  V1190Settings &v1190 = request.v1190;
  V775Settings &v775 = request.v775;
  V1720ESettings &v1720 = request.v1720;
  if (!read_v792_settings(v792) || !read_v1190_settings(v1190) ||
    !read_v775_settings(v775) || !read_v1720e_settings(v1720)) {
    gBufferClearStatus = daq::rejectBufferClearRequest(
      gBufferClearStatus, "Cannot snapshot VME module Enabled settings",
      unix_time);
    set_vme_clear_results("Not attempted: ODB Settings unavailable");
    vme_odb::publish_buffer_clear_status(gBufferClearStatus, gBufferClearResults);
    return false;
  }

  return true;
}

// Clear enabled VME modules in the established sequence
static void execute_manual_buffer_clear(const ManualBufferClearRequest &request,
                    ManualBufferClearExecution &result)
{
  bool &all_ok = result.all_ok;
  std::string &errors = result.errors;
  const auto &v792 = request.v792;
  const auto &v1190 = request.v1190;
  const auto &v775 = request.v775;
  const auto &v1720 = request.v1720;
  const auto fail = [&](const char *module, std::string &result,
              const std::string &detail) {
    result = "Failed: " + detail;
    if (!errors.empty()) errors += "; ";
    errors += module;
    errors += ": ";
    errors += detail;
    all_ok = false;
  };

  if (!v792.enabled) {
    gBufferClearResults.v792 = "Skipped: disabled in ODB";
  } else if (!v792_config::clear_data(v792_configuration_access(), true)) {
    fail("V792", gBufferClearResults.v792, "Data Clear write failed");
  } else if (v792_DataReady(gVme, V792_BASE)) {
    fail("V792", gBufferClearResults.v792,
       "DataReady remains asserted after Data Clear");
  } else {
    gBufferClearResults.v792 = "Succeeded: Data Clear";
  }

  if (!v1190.enabled) {
    gBufferClearResults.v1190 = "Skipped: disabled in ODB";
  } else if (!v1190_clear::software_clear(v1190_clear_access(), "V1190 manual Software Clear")) {
    fail("V1190", gBufferClearResults.v1190, "Software Clear write failed");
  } else {
    const auto check = v1190_clear::verify_clear(v1190_clear_access(), "V1190 Status after manual Software Clear");
    if (check == v1190_clear::CheckResult::ReadFailed)
      fail("V1190", gBufferClearResults.v1190, "status verification read failed");
    else if (check == v1190_clear::CheckResult::DataReady)
      fail("V1190", gBufferClearResults.v1190, "DataReady remains asserted after Software Clear");
    else
      gBufferClearResults.v1190 = "Succeeded: Software Clear";
  }

  if (!v775.enabled) {
    gBufferClearResults.v775 = "Skipped: disabled in ODB";
  } else if (!v775_config::clear_data(v775_configuration_access(), true)) {
    fail("V775", gBufferClearResults.v775, "Data Clear write failed");
  } else if (v775_DataReady(gVme, V775_BASE)) {
    fail("V775", gBufferClearResults.v775,
       "DataReady remains asserted after Data Clear");
  } else {
    gBufferClearResults.v775 = "Succeeded: Data Clear";
  }

  if (!v1720.enabled) {
    gBufferClearResults.v1720e = "Skipped: disabled in ODB";
  } else {
    DWORD stored = 0;
    int stored_valid = 0;
    const int status =
      v1720e_software_clear(gVme, V1720E_BASE, &stored, &stored_valid);
    if (status != MVME_SUCCESS) {
      const std::string stored_text = stored_valid
        ? std::to_string(stored)
        : "unavailable";
      fail("V1720E", gBufferClearResults.v1720e,
         std::string("Software Clear failed (status ") +
           std::to_string(status) + ", EventStored=" +
           stored_text + ")");
    } else {
      gBufferClearResults.v1720e =
        "Succeeded: Software Clear; EventStored=0";
    }
  }

}

// Publish the final result of a manual buffer clear request
static void finish_manual_buffer_clear_request(
  const ManualBufferClearRequest &request,
  const ManualBufferClearExecution &result)
{
  gBufferClearStatus = daq::finishBufferClearRequest(
    gBufferClearStatus, result.all_ok, result.errors, request.unix_time);
  vme_odb::publish_buffer_clear_status(gBufferClearStatus, gBufferClearResults);
  if (result.all_ok) {
    cm_msg(MINFO, frontend_name,
         "Request %u VME buffer clear succeeded: V792=%s; V1190=%s; "
         "V775=%s; V1720E=%s; RPV130 untouched",
         request.request_id, gBufferClearResults.v792.c_str(),
         gBufferClearResults.v1190.c_str(),
         gBufferClearResults.v775.c_str(),
         gBufferClearResults.v1720e.c_str());
  } else {
    cm_msg(MERROR, frontend_name,
         "Request %u VME buffer clear failed: %s; RPV130 untouched",
         request.request_id, result.errors.c_str());
  }
}


// Handle a stopped-state manual buffer clear request
static void process_manual_buffer_clear_request()
{
  ManualBufferClearRequest request;
  if (!validate_manual_buffer_clear_request(request)) return;
  gBufferClearStatus =
    daq::markBufferClearExecuting(gBufferClearStatus);
  vme_odb::publish_buffer_clear_status(gBufferClearStatus, gBufferClearResults);
  ManualBufferClearExecution result;
  execute_manual_buffer_clear(request, result);
  finish_manual_buffer_clear_request(request, result);
}

// Confirm that participating buffers and event counters are ready for BOR.
static bool verify_run_start_state()
{
  WORD status = 0;
  DWORD v792_counter = 0, v1190_counter = 0;
  if (gVmeConfig.v792.enabled) v792_EvtCntRead(gVme, V792_BASE, &v792_counter);
  DWORD v775_counter = 0;
  if (gVmeConfig.v775.enabled) v775_EvtCntRead(gVme, V775_BASE, &v775_counter);
  if (gV1190Config.run_settings.enabled &&
      (!vme_read32(V1190_BASE + V1190_EVENT_COUNTER, v1190_counter, "V1190 Event Counter") ||
       !vme_read16(V1190_BASE + V1190_STATUS, status, "V1190 run-start Status"))) return false;
  printf("BOR event counters: V792=%s0x%06X V1190=%s0x%08X",
         gVmeConfig.v792.enabled ? "" : "DISABLED/", v792_counter & V7XX_EVENT_COUNTER_MASK,
         gV1190Config.run_settings.enabled ? "" : "DISABLED/", v1190_counter);
  printf(" V775=%s0x%06X", gVmeConfig.v775.enabled ? "" : "DISABLED/",
         v775_counter & V7XX_EVENT_COUNTER_MASK);
  printf("\n");
  if ((gVmeConfig.v792.enabled&&v792_DataReady(gVme,V792_BASE)) || (gV1190Config.run_settings.enabled&&(status&V1190_STATUS_DATA_READY))
    || (gVmeConfig.v775.enabled&&v775_DataReady(gVme,V775_BASE))
    ) {
    cm_msg(MERROR, frontend_name, "BOR run-start verify failed: module buffer is not empty");
    return false;
  }
  if (gV1190Config.run_settings.enabled &&
    V1190_READOUT_MODE_SELECT == V1190_EVENT_FIFO_BLT32) {
    WORD fifo_status = 0, fifo_stored = 0;
    if (!vme_read16(V1190_BASE + V1190_EVENT_FIFO_STATUS,
            fifo_status, "V1190 run-start Event FIFO Status") ||
      !vme_read16(V1190_BASE + V1190_EVENT_FIFO_STORED,
            fifo_stored, "V1190 run-start Event FIFO Stored"))
      return false;
    if ((fifo_status & V1190_FIFO_STATUS_DATA_READY) || fifo_stored) {
      cm_msg(MERROR, frontend_name,
           "BOR run-start verify failed: Event FIFO not empty (status=0x%04X stored=%u)",
           fifo_status, fifo_stored);
      return false;
    }
  }
  printf("BOR configuration complete:\n  V792  : %s\n  V1190 : %s\n"
       "  V775  : %s\n"
       "  V1720E: %s\n  Buffers empty\n"
       "  Event counters reset\n  Run may start\n",
       gVmeConfig.v792.enabled?"READY":"DISABLED",gV1190Config.run_settings.enabled?"READY":"DISABLED",
       gVmeConfig.v775.enabled?"READY":"DISABLED",
       gV1720State.run.settings.enabled ? "READY" : "DISABLED");
  return true;
}

// Configure, clear, and start modules in the established BOR order.
static bool prepare_modules_for_run()
{
  if (!check_module_communication(gV1720State.run.settings.enabled != FALSE)) return false;
  if (!configure_v792_for_run() || !configure_v1190_for_run()
    || !configure_v775_for_run()
    || !configure_v1720e_for_run()) return false;
  if (!verify_v792_configuration() || !verify_v1190_configuration()
    || !verify_v775_configuration()
    ) return false;
  if (!clear_module_buffers() || !verify_run_start_state())
    return false;
  if (!gV1720State.run.settings.enabled) {
    gV1720State.lifecycle.start_attempted = false;
    gV1720State.lifecycle.started = false;
    return true;
  }
  gV1720State.lifecycle.start_attempted = true;
  const int start_status = v1720e_start(gVme, V1720E_BASE);
  if (start_status != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name,
         "V1720E Acquisition Start failed: status %d", start_status);
    return false;
  }
  gV1720State.lifecycle.started = true;
  DWORD v1720_events = 0;
  int v1720_ready = 0;
  DWORD v1720_acquisition_status = 0;
  const int ready_status = v1720e_data_ready(gVme, V1720E_BASE,
                         &v1720_ready, &v1720_events,
                         &v1720_acquisition_status);
  if (ready_status != MVME_SUCCESS || v1720_events > 1) {
    cm_msg(MERROR, frontend_name,
         "V1720E post-start buffer verification failed: status %d "
         "ready=%d Event Stored=%u",
         ready_status, v1720_ready, v1720_events);
    stop_v1720e_and_publish_state("BOR post-start verification failure");
    return false;
  }
  printf("  V1720E Acquisition STARTED; Event Stored after RUN memory reset=%u\n",
       v1720_events);
  V1720E_BOARD_INFO state = {};
  state.acquisition_status = v1720_acquisition_status;
  state.event_stored = v1720_events;
  if (v1720e_probe(gVme, V1720E_BASE, &state) == MVME_SUCCESS)
    publish_v1720e_board_state(state);
  return true;
}

static bool vme_configuration_ready()
{
  return gVmeModuleState.snapshot.frontend_bor_complete == TRUE &&
       (!gVmeConfig.v792.enabled ||
      gVmeModuleState.snapshot.v792_readback.valid == TRUE) &&
       (!gV1190Config.run_settings.enabled ||
      gVmeModuleState.snapshot.v1190_readback.valid == TRUE) &&
       (!gVmeConfig.v775.enabled ||
      gVmeModuleState.snapshot.v775_readback.valid == TRUE) &&
       (!gV1720State.run.settings.enabled ||
      (gVmeModuleState.snapshot.v1720e_readback.valid == TRUE &&
       gV1720State.lifecycle.started));
}

#if ENABLE_V792_SW_TRIGGER_TEST
static void setup_v792_sw_trigger_test()
{
  WORD cnt_low = v792_Read16(gVme, V792_BASE, V792_EVT_CNT_L_RO);
  WORD cnt_high = v792_Read16(gVme, V792_BASE, V792_EVT_CNT_H_RO);

  printf("Event counter before reset: high=0x%04X low=0x%04X -> 0x%06X\n",
       cnt_high, cnt_low, ((cnt_high & 0xFF) << 16) | cnt_low);

  v792_SingleShotReset(gVme, V792_BASE);

  cnt_low = v792_Read16(gVme, V792_BASE, V792_EVT_CNT_L_RO);
  cnt_high = v792_Read16(gVme, V792_BASE, V792_EVT_CNT_H_RO);

  printf("Event counter after single-shot reset: high=0x%04X low=0x%04X -> 0x%06X\n",
       cnt_high, cnt_low, ((cnt_high & 0xFF) << 16) | cnt_low);

  v792_Status(gVme, V792_BASE);
  printf("V792 SW trigger test enabled: issuing one SW comm at begin of run.\n");
  v792_Trigger(gVme, V792_BASE);
}
#endif

static void reset_run_statistics()
{
  gVmeState.readout_failed = false;
  gVmeState.blt_stop_requested.store(false, std::memory_order_relaxed);
  gVmeStatistics.run = {};
  gVmeStatistics.run.v1720_min_ttt_delta = 0x7FFFFFFFu;
}

static void log_run_statistics()
{
  printf("Event counter mismatches during run: %llu\n",
       static_cast<unsigned long long>(gVmeStatistics.run.counter_mismatch_count));
  if (gVmeStatistics.run.counter_mismatch_count != 0) {
    printf("First mismatch serial: %u\n", gVmeStatistics.run.first_mismatch_serial);
    printf("Last mismatch serial : %u\n", gVmeStatistics.run.last_mismatch_serial);
  }
  printf("V1720E integrity: malformed=%llu size-errors=%llu mask-errors=%llu "
       "read-timeouts=%llu counter-discontinuities=%llu\n",
       static_cast<unsigned long long>(gVmeStatistics.run.v1720_malformed_count),
       static_cast<unsigned long long>(gVmeStatistics.run.v1720_size_error_count),
       static_cast<unsigned long long>(gVmeStatistics.run.v1720_mask_error_count),
       static_cast<unsigned long long>(gVmeStatistics.run.v1720_read_timeout_count),
       static_cast<unsigned long long>(
         gVmeStatistics.run.v1720_counter_discontinuity_count));
  if (gVmeStatistics.run.v1720_have_previous) {
    printf("V1720E counters: first=%u last=%u; TTT delta count=%llu",
         gVmeStatistics.run.v1720_first_counter,
         gVmeStatistics.run.v1720_last_counter,
         static_cast<unsigned long long>(gVmeStatistics.run.v1720_ttt_count));
    if (gVmeStatistics.run.v1720_ttt_count != 0)
      printf(" min=%u max=%u", gVmeStatistics.run.v1720_min_ttt_delta,
           gVmeStatistics.run.v1720_max_ttt_delta);
    printf("\n");
  }
}

static void refresh_enabled_module_variables()
{
  if(gVmeConfig.v792.enabled) {
    module_status_snapshot::V7xxSnapshot snapshot = {0, 0, gVmeModuleState.v792.event_counter};
    if (module_status_snapshot::read_v7xx(gVme, V792_BASE, V792_CSR1_RO, V792_CSR2_RO,
                         "V792 final Status1", "V792 final Status2", vme_read16_at,
                         v792_EvtCntRead, snapshot)) {
      gVmeModuleState.v792.communication_ok=TRUE;
      decode_v7xx_runtime(gVmeModuleState.v792,snapshot.status1,snapshot.status2,snapshot.counter);
    } else gVmeModuleState.v792.communication_ok=FALSE;
    publish_v7xx_variables(V792_VARIABLES_PATH,gVmeModuleState.v792,gVmeModuleState.v792_last_publish);
  }
  if(gV1190Config.run_settings.enabled) {
    module_status_snapshot::V1190Snapshot snapshot = {0, 0, gVmeModuleState.v1190.event_counter};
    if (module_status_snapshot::read_v1190(gVme, V1190_BASE, V1190_STATUS, V1190_EVENT_STORED, V1190_EVENT_COUNTER,
                        "V1190 final Status", "V1190 final Event Stored", "V1190 final Event Counter",
                        vme_read16_at, vme_read32_at, snapshot)) {
      gVmeModuleState.v1190.communication_ok=TRUE;
      decode_v1190_runtime(snapshot.status,snapshot.stored,snapshot.counter);
    } else gVmeModuleState.v1190.communication_ok=FALSE;
    publish_v1190_variables();
  }
  if(gVmeConfig.v775.enabled) {
    module_status_snapshot::V7xxSnapshot snapshot = {0, 0, gVmeModuleState.v775.event_counter};
    if (module_status_snapshot::read_v7xx(gVme, V775_BASE, V775_STATUS1, V775_STATUS2,
                         "V775 final Status1", "V775 final Status2", vme_read16_at,
                         v775_EvtCntRead, snapshot)) {
      gVmeModuleState.v775.communication_ok=TRUE;
      decode_v7xx_runtime(gVmeModuleState.v775,snapshot.status1,snapshot.status2,snapshot.counter);
    } else gVmeModuleState.v775.communication_ok=FALSE;
    publish_v7xx_variables(V775_VARIABLES_PATH,gVmeModuleState.v775,gVmeModuleState.v775_last_publish);
  }
}


static INT start_abort(INT run_number, char *error);

// Verify the MIDAS run state before VME startup recovery
static INT verify_startup_run_state(INT *current_run_state)
{
  if (!get_absolute_odb_value("/Runinfo/State", current_run_state,
                sizeof(*current_run_state), TID_INT)) {
    cm_msg(MERROR, frontend_name,
         "Cannot verify MIDAS Run state for V1720E startup recovery");
    return FE_ERR_ODB;
  }
  if (*current_run_state != STATE_STOPPED &&
    *current_run_state != STATE_RUNNING &&
    *current_run_state != STATE_PAUSED) {
    cm_msg(MERROR, frontend_name,
         "Unknown MIDAS Run state %d; refusing V1720E startup recovery",
         *current_run_state);
    return FE_ERR_ODB;
  }
  gV1720State.lifecycle.startup_run_state = *current_run_state;
  return SUCCESS;
}

// Initialize VME ODB schemas and startup status
static INT initialize_frontend_odb_schema()
{
  if (!initialize_rpv130_odb() || !initialize_other_module_odb() ||
    !initialize_v1720e_odb() || !initialize_run_counters_odb() ||
    !initialize_buffer_clear_mailbox()) {
    cm_msg(MERROR, frontend_name,
         "Cannot initialize VME module ODB schema/settings");
    return FE_ERR_ODB;
  }
  if (!publish_configuration_status(false, 0)) {
    cm_msg(MERROR, frontend_name,
         "Cannot initialize VME frontend configuration status");
    return FE_ERR_ODB;
  }
  vme_odb::reset_run_snapshot(gVmeModuleState.snapshot, 0, frontend_name);
  if (!vme_odb::publish_run_snapshot(gVmeModuleState.snapshot)) {
    cm_msg(MERROR, frontend_name,
         "Cannot initialize VME RunSnapshot ODB schema");
    return FE_ERR_ODB;
  }
  return SUCCESS;
}

// Register VME startup and BUSY transitions
static INT register_frontend_transitions()
{
  const INT transition_status =
    cm_register_transition(TR_STARTABORT, start_abort, 500);
  if (transition_status != CM_SUCCESS) {
    cm_msg(MERROR, frontend_name,
         "Cannot register TR_STARTABORT callback: status %d",
         transition_status);
    return transition_status;
  }
  if (cm_register_transition(TR_START, global_busy::before_start, 400) != CM_SUCCESS ||
    cm_register_transition(TR_START, global_busy::after_start, 600) != CM_SUCCESS ||
    cm_register_transition(TR_STOP, global_busy::before_stop, 400) != CM_SUCCESS ||
    cm_register_transition(TR_STARTABORT, global_busy::start_abort, 400) != CM_SUCCESS)
    return FE_ERR_ODB;
  return SUCCESS;
}

// Initialize the VME frontend and hardware interface
INT frontend_init()
{
  global_busy::disable_readout();
  INT current_run_state = 0;
  const INT startup_state_status = verify_startup_run_state(&current_run_state);
  if (startup_state_status != SUCCESS) return startup_state_status;
#if ENABLE_V792_SW_TRIGGER_TEST || ENABLE_V1190_SOFT_TRIGGER_TEST || ENABLE_V775_SW_TRIGGER_TEST
  printf("============================================================\n"
       " WARNING: SOFTWARE-TRIGGER DIAGNOSTIC BUILD\n"
       " V792 SW trigger    : ENABLED\n"
       " V1190 Soft trigger : ENABLED\n"
       " V775 SW trigger    : ENABLED\n"
       "============================================================\n");
#else
  printf("Software-trigger diagnostics: disabled\n");
#endif

  const INT schema_status = initialize_frontend_odb_schema();
  if (schema_status != SUCCESS) return schema_status;
  if (!global_busy::initialize()) return FE_ERR_ODB;
  const INT transition_status = register_frontend_transitions();
  if (transition_status != SUCCESS) return transition_status;

  printf("Opening VME interface...\n");

  INT status = mvme_open(&gVme, 0);

  if (status != MVME_SUCCESS) {
    cm_msg(MERROR, frontend_name, "mvme_open() failed: %d", status);
    return FE_ERR_HW;
  }
  log_v3718_out0_diagnostic(gVme);
  global_busy::attach(gVme);
  // OUT0 may be unconfigured or temporarily unavailable. Keep VME diagnostics
  // usable; the START transition will require a successful BUSY assertion.
  global_busy::set_global_busy(true);

  mvme_set_am(gVme, MVME_AM_A24_ND);
  mvme_set_dmode(gVme, MVME_DMODE_D16);

  printf("VME interface opened.\n");
  publish_rpv130_status(true);
  if (!check_module_communication(gV1720State.lifecycle.startup_enabled)) {
    mvme_close(gVme);
    gVme = NULL;
    global_busy::attach(NULL);
    return FE_ERR_HW;
  }
  // Recheck just before recovery so a run-state change during initialization
  // cannot use the earlier STOPPED snapshot to authorize a V1720E stop.
  if (!get_absolute_odb_value("/Runinfo/State", &current_run_state,
                sizeof(current_run_state), TID_INT)) {
    cm_msg(MERROR, frontend_name,
         "Cannot recheck MIDAS Run state before V1720E startup recovery");
    mvme_close(gVme);
    gVme = NULL;
    global_busy::attach(NULL);
    return FE_ERR_ODB;
  }
  gV1720State.lifecycle.startup_run_state = current_run_state;
  if (current_run_state != STATE_STOPPED &&
    current_run_state != STATE_RUNNING &&
    current_run_state != STATE_PAUSED) {
    cm_msg(MERROR, frontend_name,
         "Unknown MIDAS Run state %d before V1720E startup recovery",
         current_run_state);
    mvme_close(gVme);
    gVme = NULL;
    global_busy::attach(NULL);
    return FE_ERR_ODB;
  }
  if (current_run_state == STATE_STOPPED &&
    !quiesce_rpv130_single_event_busy("STOPPED frontend startup")) {
    mvme_close(gVme);
    gVme = NULL;
    global_busy::attach(NULL);
    return FE_ERR_HW;
  }
  if (gV1720State.lifecycle.startup_enabled && current_run_state == STATE_STOPPED) {
    DWORD control = 0, acquisition_status = 0;
    const int read_status = v1720e_read_run_state(
      gVme, V1720E_BASE, &control, &acquisition_status);
    if (read_status != MVME_SUCCESS) {
      cm_msg(MERROR, frontend_name,
           "Cannot read V1720E RUN state at STOPPED startup: status %d",
           read_status);
      mvme_close(gVme);
      gVme = NULL;
      global_busy::attach(NULL);
      return FE_ERR_HW;
    }
    if ((control & V1720E_ACQ_RUN) ||
      (acquisition_status & V1720E_ACQ_RUN)) {
      cm_msg(MERROR, frontend_name,
           "V1720E RUN persisted while MIDAS STOPPED: "
           "AcqControl=0x%08X AcqStatus=0x%08X; attempting stop",
           control, acquisition_status);
      if (!stop_v1720e_and_publish_state("STOPPED frontend startup recovery")) {
        cm_msg(MERROR, frontend_name,
             "V1720E startup recovery failed; fevme startup refused");
        mvme_close(gVme);
        gVme = NULL;
        global_busy::attach(NULL);
        return FE_ERR_HW;
      }
    }
  } else if (current_run_state == STATE_RUNNING ||
         current_run_state == STATE_PAUSED) {
    cm_msg(MINFO, frontend_name,
         "V1720E startup auto-stop skipped: MIDAS state %d",
         current_run_state);
  }
  if (gV1190Config.run_settings.enabled) {
    INT state_before_pout = 0;
    INT transition_in_progress = 0;
    if (!get_absolute_odb_value("/Runinfo/State", &state_before_pout,
                  sizeof(state_before_pout), TID_INT) ||
      !get_absolute_odb_value("/Runinfo/Transition in progress",
                  &transition_in_progress,
                  sizeof(transition_in_progress), TID_INT)) {
      cm_msg(MERROR, frontend_name,
           "Cannot verify MIDAS transition state before V1190 startup POUT setting");
      mvme_close(gVme);
      gVme = NULL;
      global_busy::attach(NULL);
      return FE_ERR_ODB;
    }
    if (state_before_pout != STATE_STOPPED || transition_in_progress) {
      cm_msg(MERROR, frontend_name,
           "V1190 startup POUT setting requires stable STOPPED state; MIDAS state %d transition %d",
           state_before_pout, transition_in_progress);
      mvme_close(gVme);
      gVme = NULL;
      global_busy::attach(NULL);
      return FE_ERR_HW;
    }
    if (!configure_v1190_pout_startup()) {
      cm_msg(MERROR, frontend_name,
           "V1190 startup POUT configuration failed; fevme startup refused");
      mvme_close(gVme);
      gVme = NULL;
      global_busy::attach(NULL);
      return FE_ERR_HW;
    }
  }
  return SUCCESS;
}


// Shut down the VME frontend safely
INT frontend_exit()
{
  global_busy::publish_ready(true, false, 0);
  if (gVme) global_busy::set_global_busy(true);
  const bool rpv130_stopped =
    quiesce_rpv130_single_event_busy("frontend exit");
  if (gVme) {
    const bool owns_v1720_run = gV1720State.lifecycle.start_attempted || gV1720State.lifecycle.started;
    const bool midas_active = run_state == STATE_RUNNING ||
                  run_state == STATE_PAUSED;
    if (!owns_v1720_run &&
      (gV1720State.lifecycle.startup_run_state != STATE_STOPPED || midas_active)) {
      cm_msg(MINFO, frontend_name,
           "V1720E auto-stop skipped on frontend exit: "
           "startup MIDAS state %d, current state %d, no frontend start",
           gV1720State.lifecycle.startup_run_state, run_state);
    } else if (!stop_v1720e_and_publish_state("frontend exit")) {
      cm_msg(MERROR, frontend_name,
           "V1720E hardware stop failed during frontend exit");
    }
    refresh_enabled_module_variables();
#if ENABLE_V1190_SOFT_TRIGGER_TEST
    if (gV1190Config.run_settings.enabled && !restore_v1190_diagnostic_settings())
      cm_msg(MERROR, frontend_name, "V1190 diagnostic restoration failed during frontend exit");
#endif
#if ENABLE_V775_SW_TRIGGER_TEST
    if (gVmeConfig.v775.enabled && !restore_v775_diagnostic_settings())
      cm_msg(MERROR, frontend_name, "V775 diagnostic restoration failed during frontend exit");
#endif
    mvme_close(gVme);
    gVme = NULL;
    global_busy::attach(NULL);
  }

  printf("VME interface closed.\n");

  return rpv130_stopped ? SUCCESS : FE_ERR_HW;
}

// Prepare VME modules for a new run
INT begin_of_run(INT run_number, char *error)
{
  const auto finish = [run_number](INT result) {
    cm_msg(MINFO, frontend_name, "START 500 begin_of_run exit run %d status %d",
         run_number, result);
    return result;
  };
  cm_msg(MINFO, frontend_name, "START 500 begin_of_run enter run %d", run_number);
  printf("Begin run %d\n", run_number);
  if (!global_busy::publish_ready(true, false, 0)) return finish(FE_ERR_ODB);
  if (!publish_configuration_status(false, run_number)) {
    snprintf(error, 256,
         "Cannot reset VME configuration status at BOR");
    return finish(FE_ERR_ODB);
  }
  reset_run_statistics();
  gVmeStatistics.v1190_blt_diagnostics.store(0, std::memory_order_relaxed);
  v1190_fifo_blt_state_reset(&gVmeState.v1190_fifo_blt);
  mark_run_counters_dirty();
  if (!publish_run_counters()) {
    cm_msg(MERROR, frontend_name,
         "Cannot reset VME RunCounters ODB values at BOR");
    snprintf(error, 256, "Cannot reset VME RunCounters");
    mark_configuration_failed(run_number);
    return finish(FE_ERR_ODB);
  }
  vme_odb::reset_run_snapshot(gVmeModuleState.snapshot, run_number, frontend_name);
  BOOL participation_valid = FALSE, frontend_enabled = FALSE;
  INT participation_run = 0;
  if (!get_absolute_odb_value("/DAQ/Status/Run/ParticipationValid",
                &participation_valid, sizeof(participation_valid), TID_BOOL) ||
    !get_absolute_odb_value("/DAQ/Status/Run/ParticipationRunNumber",
                &participation_run, sizeof(participation_run), TID_INT) ||
    !get_absolute_odb_value("/DAQ/Status/Run/VMEParticipating",
                &frontend_enabled, sizeof(frontend_enabled), TID_BOOL) ||
    !participation_valid || participation_run != run_number) {
    snprintf(error, 256, "Cannot read VME run participation");
    return finish(FE_ERR_ODB);
  }
  gVmeState.frontend_enabled_for_run = frontend_enabled != FALSE;
  if (!gVmeState.frontend_enabled_for_run) {
    gVmeModuleState.snapshot.enabled_for_run = FALSE;
    gVmeModuleState.snapshot.frontend_bor_complete = TRUE;
    if (!vme_odb::publish_run_snapshot(gVmeModuleState.snapshot) ||
      !publish_configuration_status(true, run_number) ||
      !global_busy::publish_ready(false, false, 0)) {
      snprintf(error, 256, "Cannot publish disabled VME frontend state");
      return finish(FE_ERR_ODB);
    }
    return finish(SUCCESS);
  }

  if (!validate_and_snapshot_module_settings() ||
    !snapshot_v1720e_run_settings() ||
    !snapshot_rpv130_enabled_for_run()) {
    cm_msg(MERROR, frontend_name,
         "Cannot snapshot/validate VME module Settings at BOR");
    snprintf(error, 256, "Invalid VME module ODB Settings");
    mark_configuration_failed(run_number);
    return finish(FE_ERR_ODB);
  }
  if (!validate_v792_event_source_dependency()) {
    snprintf(error, 256,
         "V792 must be enabled when any VME physics readout module "
         "is enabled");
    mark_configuration_failed(run_number);
    return finish(FE_ERR_ODB);
  }
  capture_vme_requested_snapshot();
  publish_vme_enabled_for_run();
  set_module_readback_valid(V792_READBACK_PATH,false);
  set_module_readback_valid(V1190_READBACK_PATH,false);
  set_module_readback_valid(V775_READBACK_PATH,false);
  set_v1720e_readback_valid(false);

  if (!prepare_modules_for_run()) {
    stop_v1720e_and_publish_state("BOR failure");
    cm_msg(MERROR, frontend_name,
         "BOR configuration failed; refusing to start run %d", run_number);
    snprintf(error, 256, "Normal BOR module preparation failed");
    mark_configuration_failed(run_number);
    return finish(FE_ERR_HW);
  }

#if ENABLE_V792_SW_TRIGGER_TEST
  if(gVmeConfig.v792.enabled) setup_v792_sw_trigger_test();
#endif
#if ENABLE_V1190_SOFT_TRIGGER_TEST
  if (gV1190Config.run_settings.enabled && (!restore_v1190_diagnostic_settings() ||
    !setup_v1190_soft_trigger_test())) {
    snprintf(error, 256, "V1190 soft-trigger diagnostic setup failed");
    restore_v1190_diagnostic_settings();
    stop_v1720e_and_publish_state("BOR diagnostic setup failure");
    mark_configuration_failed(run_number);
    return finish(FE_ERR_HW);
  }
#endif
#if ENABLE_V775_SW_TRIGGER_TEST
  if (gVmeConfig.v775.enabled && (!restore_v775_diagnostic_settings() ||
    !setup_v775_sw_trigger_test())) {
    snprintf(error, 256, "V775 SW trigger diagnostic setup failed");
    restore_v775_diagnostic_settings();
    stop_v1720e_and_publish_state("BOR diagnostic setup failure");
    mark_configuration_failed(run_number);
    return finish(FE_ERR_HW);
  }
#endif
  if (!arm_rpv130_single_event_busy()) {
    snprintf(error, 256, "RPV130 Single Event BUSY arm failed");
    quiesce_rpv130_single_event_busy("BOR arm failure");
    stop_v1720e_and_publish_state("BOR RPV130 arm failure");
    mark_configuration_failed(run_number);
    return finish(FE_ERR_HW);
  }
  gVmeModuleState.snapshot.frontend_bor_complete = TRUE;
  if (!vme_configuration_ready()) {
    gVmeModuleState.snapshot.frontend_bor_complete = FALSE;
    quiesce_rpv130_single_event_busy("BOR readiness failure");
    stop_v1720e_and_publish_state("BOR readiness failure");
    cm_msg(MERROR, frontend_name,
         "VME BOR completed without all enabled modules ready");
    snprintf(error, 256, "VME configuration readiness check failed");
    mark_configuration_failed(run_number);
    return finish(FE_ERR_HW);
  }
  if (!vme_odb::publish_run_snapshot(gVmeModuleState.snapshot)) {
    gVmeModuleState.snapshot.frontend_bor_complete = FALSE;
    quiesce_rpv130_single_event_busy("BOR RunSnapshot publish failure");
    stop_v1720e_and_publish_state("BOR RunSnapshot publish failure");
    cm_msg(MERROR, frontend_name,
         "Cannot publish completed VME RunSnapshot for run %d",
         run_number);
    snprintf(error, 256, "Cannot publish completed VME RunSnapshot");
    mark_configuration_failed(run_number);
    return finish(FE_ERR_ODB);
  }
  if (!publish_configuration_status(true, run_number)) {
    gVmeModuleState.snapshot.frontend_bor_complete = FALSE;
    vme_odb::publish_run_snapshot(gVmeModuleState.snapshot);
    quiesce_rpv130_single_event_busy("BOR status publish failure");
    stop_v1720e_and_publish_state("BOR status publish failure");
    cm_msg(MERROR, frontend_name,
         "Cannot publish successful VME configuration status for run %d",
         run_number);
    snprintf(error, 256, "Cannot publish VME configuration status");
    mark_configuration_failed(run_number);
    return finish(FE_ERR_ODB);
  }
  if (!global_busy::publish_ready(true, true, run_number)) {
    snprintf(error, 256, "Cannot publish VME DAQReady");
    quiesce_rpv130_single_event_busy("BOR DAQReady publish failure");
    return finish(FE_ERR_ODB);
  }
  return finish(SUCCESS);
}

// Stop VME acquisition and publish final status
INT end_of_run(INT run_number, char *error)
{
  global_busy::disable_readout();
  if (!gVmeState.frontend_enabled_for_run) {
    global_busy::publish_ready(false, false, 0);
    return SUCCESS;
  }
  global_busy::publish_ready(true, false, 0);
  bool restore_failed = false;
  printf("End run %d\n", run_number);
  const bool rpv130_stopped = quiesce_rpv130_single_event_busy("EOR");
  const bool v1720_stopped = stop_v1720e_and_publish_state("EOR");
  if (!rpv130_stopped || !v1720_stopped) {
    publish_run_counters();
    snprintf(error, 256, "%s failed during EOR",
         !rpv130_stopped ? "RPV130 BUSY disable" :
                   "V1720E Acquisition Stop");
    return FE_ERR_HW;
  }
  log_run_statistics();
  if (!publish_run_counters()) {
    cm_msg(MERROR, frontend_name,
         "Cannot publish final VME RunCounters ODB values at EOR");
    snprintf(error, 256, "Cannot publish final VME RunCounters");
    return FE_ERR_ODB;
  }
  refresh_enabled_module_variables();

#if ENABLE_V1190_SOFT_TRIGGER_TEST
  if (gV1190Config.run_settings.enabled && !restore_v1190_diagnostic_settings())
    restore_failed = true;
#endif
#if ENABLE_V775_SW_TRIGGER_TEST
  if (gVmeConfig.v775.enabled && !restore_v775_diagnostic_settings())
    restore_failed = true;
#endif
  if (restore_failed) {
    snprintf(error, 256, "Diagnostic settings restoration failed");
    return FE_ERR_HW;
  }
  return SUCCESS;
}


// Restore safe VME state after a failed START
static INT start_abort(INT run_number, char *error)
{
  global_busy::disable_readout();
  global_busy::publish_ready(true, false, 0);
  if (error)
    error[0] = '\0';

  /* A successful BOR enables the legacy MFE readout before another client
   * can fail the common START. Stop software readout before hardware. */
  readout_enable(FALSE);
  const bool rpv130_stopped = !gVmeState.frontend_enabled_for_run ||
    quiesce_rpv130_single_event_busy("STARTABORT");
  V1720StopOutcome stop_outcome = V1720StopOutcome::Disabled;
  const bool stopped = !gVmeState.frontend_enabled_for_run ||
    stop_v1720e_and_publish_state("STARTABORT rollback", &stop_outcome);

  gVmeModuleState.snapshot.frontend_bor_complete = FALSE;
  vme_odb::publish_run_snapshot(gVmeModuleState.snapshot);
  mark_configuration_failed(run_number);

  run_state = STATE_STOPPED;
  cm_set_client_run_state(run_state);
  if (!stopped || !rpv130_stopped) {
    if (error)
      snprintf(error, 256,
           "%s failed during STARTABORT",
           !rpv130_stopped ? "RPV130 BUSY disable" :
                     "V1720E Acquisition Stop");
    return FE_ERR_HW;
  }
  cm_msg(MINFO, frontend_name,
       "STARTABORT rollback completed for run %d; V1720E %s",
       run_number, v1720_stop_outcome_name(stop_outcome));
  return SUCCESS;
}


/* Handle a MIDAS run pause. */
INT pause_run(INT run_number, char *error)
{
  return SUCCESS;
}


/* Handle resuming a paused MIDAS run. */
INT resume_run(INT run_number, char *error)
{
  return SUCCESS;
}


// Maintain VME status and handle stopped-state requests
INT frontend_loop()
{
  if (gVmeState.blt_stop_requested.exchange(false, std::memory_order_relaxed) &&
    run_state == STATE_RUNNING) {
    char error[TRANSITION_ERROR_STRING_LENGTH] = {};
    const INT status = cm_transition(TR_STOP, 0, error, sizeof(error),
                     TR_ASYNC, FALSE);
    if (status != CM_SUCCESS) {
      cm_msg(MERROR, frontend_name,
           "BLT32 failure: RUN stop request failed: status %d %s",
           status, error);
      gVmeState.blt_stop_requested.store(true, std::memory_order_relaxed);
    } else {
      cm_msg(MERROR, frontend_name,
           "BLT32 failure: RUN stop requested");
    }
  }
  global_busy::process_diagnostic_request(run_state == STATE_STOPPED);
  process_manual_buffer_clear_request();
  publish_rpv130_status(false);
  const DWORD now = ss_millitime();
  const auto due=[](DWORD now,DWORD last,bool dirty) {
    const DWORD elapsed=static_cast<DWORD>(now-last);
    return (dirty&&elapsed>=MODULE_VARIABLES_MIN_PUBLISH_INTERVAL_MS)||elapsed>=MODULE_VARIABLES_HEARTBEAT_INTERVAL_MS;
  };
  if (due(now, gVmeStatistics.run_counters_last_publish, gVmeStatistics.run_counters_dirty))
    publish_run_counters();
  if (run_state == STATE_RUNNING && !gVmeState.frontend_enabled_for_run) {
    ss_sleep(FRONTEND_IDLE_SLEEP_MS);
    return SUCCESS;
  }
  if(gVmeConfig.v792.enabled && due(now,gVmeModuleState.v792_last_publish,gVmeModuleState.v792.dirty)) {
    module_status_snapshot::V7xxSnapshot snapshot = {0, 0, gVmeModuleState.v792.event_counter};
    if (module_status_snapshot::read_v7xx(gVme, V792_BASE, V792_CSR1_RO, V792_CSR2_RO,
                         "V792 runtime Status1", "V792 runtime Status2", vme_read16_at,
                         v792_EvtCntRead, snapshot)) {
      gVmeModuleState.v792.communication_ok=TRUE;
      decode_v7xx_runtime(gVmeModuleState.v792,snapshot.status1,snapshot.status2,snapshot.counter);
    } else gVmeModuleState.v792.communication_ok=FALSE;
    publish_v7xx_variables(V792_VARIABLES_PATH,gVmeModuleState.v792,gVmeModuleState.v792_last_publish);
  }
  if(gV1190Config.run_settings.enabled && due(now,gVmeModuleState.v1190_last_publish,gVmeModuleState.v1190.dirty)) {
    module_status_snapshot::V1190Snapshot snapshot = {0, 0, gVmeModuleState.v1190.event_counter};
    if (module_status_snapshot::read_v1190(gVme, V1190_BASE, V1190_STATUS, V1190_EVENT_STORED, V1190_EVENT_COUNTER,
                        "V1190 runtime Status", "V1190 runtime Event Stored", "V1190 runtime Event Counter",
                        vme_read16_at, vme_read32_at, snapshot)) {
      gVmeModuleState.v1190.communication_ok=TRUE;
      decode_v1190_runtime(snapshot.status,snapshot.stored,snapshot.counter);
    } else gVmeModuleState.v1190.communication_ok=FALSE;
    publish_v1190_variables();
  }
  if(gVmeConfig.v775.enabled && due(now,gVmeModuleState.v775_last_publish,gVmeModuleState.v775.dirty)) {
    module_status_snapshot::V7xxSnapshot snapshot = {0, 0, gVmeModuleState.v775.event_counter};
    if (module_status_snapshot::read_v7xx(gVme, V775_BASE, V775_STATUS1, V775_STATUS2,
                         "V775 runtime Status1", "V775 runtime Status2", vme_read16_at,
                         v775_EvtCntRead, snapshot)) {
      gVmeModuleState.v775.communication_ok=TRUE;
      decode_v7xx_runtime(gVmeModuleState.v775,snapshot.status1,snapshot.status2,snapshot.counter);
    } else gVmeModuleState.v775.communication_ok=FALSE;
    publish_v7xx_variables(V775_VARIABLES_PATH,gVmeModuleState.v775,gVmeModuleState.v775_last_publish);
  }
  const DWORD elapsed =
    static_cast<DWORD>(now - gV1720State.runtime.last_variables_publish);
  if (gV1720State.variables_enabled &&
    ((gV1720State.runtime.value.dirty &&
      elapsed >= V1720E_VARIABLES_MIN_PUBLISH_INTERVAL_MS) ||
     elapsed >= V1720E_VARIABLES_HEARTBEAT_INTERVAL_MS))
    publish_v1720e_variables();

  // EQ_POLLED/RO_RUNNING keeps poll_event() out of STOPPED and PAUSED.
  // frontend_call_loop remains enabled for status housekeeping, so yield
  // CPU here when trigger-latency-sensitive readout is inactive.
  if (run_state != STATE_RUNNING)
    ss_sleep(FRONTEND_IDLE_SLEEP_MS);
  return SUCCESS;
}


// Detect a ready VME event without consuming data
INT poll_event(INT source, INT count, BOOL test)
{
  if (!gVmeState.frontend_enabled_for_run)
    return 0;
  if (!global_busy::readout_allowed())
    return 0;
  if (!gVme || gVmeState.readout_failed ||
    !gVmeConfig.v792.enabled ||
    (gV1720State.run.settings.enabled && !gV1720State.lifecycle.started))
    return 0;
  for (INT i = 0; i < count; ++i) {
    if (v792_DataReady(gVme, V792_BASE) && !test) {
      return 1;
    }
  }
  return 0;
}


/* Configure interrupt-driven event acquisition; interrupt mode is not used in this test frontend. */
INT interrupt_configure(INT cmd, INT source, PTYPE adr)
{
  return SUCCESS;
}

// Check counters across participating VME modules
static bool check_event_counter_match(const V792EventInfo &v792,
                    const V1190EventInfo &v1190,
                    const V775EventInfo &v775,
                    const V1720E_EVENT_INFO *v1720)
{
  bool have = false;
  DWORD reference = 0;
#define CMP(enabled, value) do { \
  if (enabled) { \
    DWORD n = (value) & V1190_EVENT_COUNTER_MASK; \
    if (have && n != reference) return false; \
    reference = n; \
    have = true; \
  } \
} while (0)
  CMP(gVmeConfig.v792.enabled, v792.event_counter);
  CMP(gV1190Config.run_settings.enabled, v1190.event_counter);
  CMP(gVmeConfig.v775.enabled, v775.event_counter);
  CMP(v1720 != NULL, v1720 ? v1720->event_counter : 0);
#undef CMP
  return true;
}

static void log_event_counter_mismatch(const V792EventInfo &v792,
                     const V1190EventInfo &v1190,
                     const V775EventInfo &v775,
                     const V1720E_EVENT_INFO *v1720,
                     DWORD midas_serial)
{
  if (gVmeStatistics.run.counter_mismatch_count == 0)
    gVmeStatistics.run.first_mismatch_serial = midas_serial;
  ++gVmeStatistics.run.counter_mismatch_count;
  gVmeStatistics.run.last_mismatch_serial = midas_serial;
  mark_run_counters_dirty();

  const uint64_t count = gVmeStatistics.run.counter_mismatch_count;
  if (count <= 10) {
    if (v1720) {
      cm_msg(MINFO, frontend_name,
           "WARNING: Event counter mismatch (accepted): MIDAS serial=%u "
           "V792=0x%06X V1190=0x%06X "
           "V775=0x%06X "
           "V1720=0x%06X",
           midas_serial, v792.event_counter & V7XX_EVENT_COUNTER_MASK,
           v1190.event_counter & V1190_EVENT_COUNTER_MASK,
           v775.event_counter & V7XX_EVENT_COUNTER_MASK,
           v1720->event_counter & V7XX_EVENT_COUNTER_MASK);
    } else {
      cm_msg(MINFO, frontend_name,
           "WARNING: Event counter mismatch (accepted): MIDAS serial=%u "
           "V792=0x%06X V1190=0x%06X"
           " V775=0x%06X"
           " (V1720 disabled)",
           midas_serial, v792.event_counter & V7XX_EVENT_COUNTER_MASK,
           v1190.event_counter & V1190_EVENT_COUNTER_MASK,
           v775.event_counter & V7XX_EVENT_COUNTER_MASK);
    }
  } else if (count % 1000 == 0) {
    cm_msg(MINFO, frontend_name,
         "WARNING: Event counter mismatch summary: %llu mismatches through MIDAS serial %u",
         static_cast<unsigned long long>(count), midas_serial);
  }
}

// Validate V1720E event continuity and structure
static bool update_v1720e_integrity(const V1720E_EVENT_INFO &event,
                  DWORD midas_serial)
{
  const auto &run = gVmeStatistics.run;
  const v1720e_integrity::State previous = {
    run.v1720_have_previous, run.v1720_first_counter, run.v1720_last_counter,
    run.v1720_previous_counter, run.v1720_previous_ttt, run.v1720_ttt_count,
    run.v1720_min_ttt_delta, run.v1720_max_ttt_delta};
  const v1720e_integrity::Event current = {
    event.event_size, event.channel_mask, event.event_counter, event.trigger_time_tag};
  const auto result = v1720e_integrity::check(
    current, gV1720State.run.expected_event_words,
    gV1720State.run.expected_channel_mask, previous);

  if (!result.size_valid) {
    ++gVmeStatistics.run.v1720_size_error_count;
    mark_run_counters_dirty();
    cm_msg(MERROR, frontend_name,
         "V1720E Event Size error at MIDAS serial %u: got %u, expected %u",
         midas_serial, event.event_size, gV1720State.run.expected_event_words);
  }
  if (!result.channel_mask_valid) {
    ++gVmeStatistics.run.v1720_mask_error_count;
    mark_run_counters_dirty();
    cm_msg(MERROR, frontend_name,
         "V1720E Channel Mask error at MIDAS serial %u: got 0x%02X, expected 0x%02X",
         midas_serial, event.channel_mask, gV1720State.run.expected_channel_mask);
  }
  if (!result.counter_continuous) {
    ++gVmeStatistics.run.v1720_counter_discontinuity_count;
    mark_run_counters_dirty();
    cm_msg(MERROR, frontend_name,
         "V1720E counter discontinuity at MIDAS serial %u: "
         "previous=%u current=%u delta=%u",
         midas_serial, previous.previous_counter,
         event.event_counter, result.counter_delta);
  }
  gVmeStatistics.run.v1720_have_previous = result.next.have_previous;
  gVmeStatistics.run.v1720_first_counter = result.next.first_counter;
  gVmeStatistics.run.v1720_last_counter = result.next.last_counter;
  gVmeStatistics.run.v1720_previous_counter = result.next.previous_counter;
  gVmeStatistics.run.v1720_previous_ttt = result.next.previous_ttt;
  gVmeStatistics.run.v1720_ttt_count = result.next.ttt_count;
  gVmeStatistics.run.v1720_min_ttt_delta = result.next.min_ttt_delta;
  gVmeStatistics.run.v1720_max_ttt_delta = result.next.max_ttt_delta;
  return result.valid();
}

// Build a MIDAS event from enabled VME banks
static INT build_midas_event(char *pevent,
               const DWORD *v792_data, const V792EventInfo &v792,
               const DWORD *v1190_data, const V1190EventInfo &v1190,
               const DWORD *v775_data, const V775EventInfo &v775,
               const DWORD *v1720_data,
               const V1720E_EVENT_INFO *v1720)
{
  bk_init32(pevent);
  void *bank = NULL;
  if (gVmeConfig.v792.enabled) {
    bk_create(pevent, "ADC0", TID_DWORD, &bank);
    memcpy(bank, v792_data, v792.words * sizeof(DWORD));
    bk_close(pevent, static_cast<DWORD *>(bank) + v792.words);
  }

  if (gV1190Config.run_settings.enabled) {
    bank = NULL;
    bk_create(pevent, "TDC0", TID_DWORD, &bank);
    memcpy(bank, v1190_data, v1190.words * sizeof(DWORD));
    bk_close(pevent, static_cast<DWORD *>(bank) + v1190.words);
  }

  if (gVmeConfig.v775.enabled) {
    bank = NULL;
    bk_create(pevent, "TDC1", TID_DWORD, &bank);
    memcpy(bank, v775_data, v775.words * sizeof(DWORD));
    bk_close(pevent, static_cast<DWORD *>(bank) + v775.words);
  }

  if (v1720) {
    bank = NULL;
    bk_create(pevent, "FADC", TID_DWORD, &bank);
    memcpy(bank, v1720_data, v1720->words * sizeof(DWORD));
    bk_close(pevent, static_cast<DWORD *>(bank) + v1720->words);
  }
  return bk_size(pevent);
}


// Read one synchronized VME event
INT read_vme_event(char *pevent, INT off)
{
  if (!gVmeState.frontend_enabled_for_run)
    return 0;
  if (!global_busy::readout_allowed())
    return 0;
  if (!gVme || gVmeState.readout_failed)
    return 0;
  if (gVmeState.single_event_busy_enabled_for_run) {
    bool busy1 = false;
    uint8_t csr1 = 0;
    const int busy_status = rpv130_read_busy1(
      gVme, RPV130_BASE_ADDRESS, &busy1, &csr1);
    if (busy_status != MVME_SUCCESS ||
      !busy1 ||
      (csr1 & RPV130_CSR1_CHANNEL1_ARMED) !=
        RPV130_CSR1_CHANNEL1_ARMED) {
      fail_single_event_busy("FIN1 BUSY1 absent, disarmed, or CSR1 read failed");
      return 0;
    }
    if (!publish_rpv130_busy_state(true, true)) {
      fail_single_event_busy("RPV130 ODB BUSY status update failed");
      return 0;
    }
  }

  // V792 is the primary trigger. Do not consume it until every enabled peer FIFO is ready.
  bool peers_ready = true;
  if (gV1190Config.run_settings.enabled) {
    peers_ready = wait_for_v1190_data_ready();
  }
  if (peers_ready && gVmeConfig.v775.enabled) {
    peers_ready = wait_for_v775_data_ready();
  }
  if (peers_ready && gV1720State.run.settings.enabled) {
    peers_ready = wait_for_v1720e_data_ready();
  }
  if (!peers_ready) {
    gVmeState.readout_failed = true;
    cm_msg(MERROR, frontend_name,
         "Synchronized readout disabled after module-ready timeout; "
         "no FIFO was consumed. Check hardware and restart the run.");
    fail_single_event_busy("module-ready timeout");
    return 0;
  }

  DWORD v792_data[V792_MAX_EVENT_WORDS];
  DWORD v1190_data[V1190_MAX_EVENT_WORDS];
  DWORD v775_data[V775_MAX_EVENT_WORDS];
  DWORD v1720_data[V1720E_MAX_EVENT_WORDS];
  V792EventInfo v792 = {};
  if (gVmeConfig.v792.enabled) {
    v792 = read_v792_event(v792_data);
  }
  if (gVmeConfig.v792.enabled && (!v792.valid || v792.words == 0)) {
    gVmeState.readout_failed = true;
    cm_msg(MERROR, frontend_name,
         "V792 readout disabled after error; no partial bank sent. Check hardware and restart the run to reset readout.");
    fail_single_event_busy("V792 event readout failed");
    return 0;
  }

  V1190EventInfo v1190 = {};
  if (gV1190Config.run_settings.enabled) {
    v1190 = read_v1190_event(v1190_data);
  }
  if (gV1190Config.run_settings.enabled && (!v1190.valid || v1190.words == 0)) {
    gVmeState.readout_failed = true;
    cm_msg(MERROR, frontend_name,
         "V1190 readout failed; V792 consumed. No MIDAS event sent; stop RUN.");
    fail_single_event_busy("V1190 event readout failed");
    return 0;
  }

  V775EventInfo v775 = {};
  if (gVmeConfig.v775.enabled) {
    const v775_readout::Access access = {gVme, V775_BASE, frontend_name,
                       mvme_get_dmode, mvme_set_dmode, mvme_read};
    v775 = v775_readout::read_single_event(access, v775_data);
    if (v775.eob_consumed) {
      gVmeModuleState.v775.event_counter = v775.event_counter;
      gVmeModuleState.v775.dirty = true;
    }
  }
  if (gVmeConfig.v775.enabled && (!v775.valid || v775.words == 0)) {
    gVmeState.readout_failed = true;
    cm_msg(MERROR, frontend_name,
         "V775 readout disabled after error; V792/V1190 events were consumed but no partial "
         "MIDAS event was sent. Check hardware and restart the run.");
    fail_single_event_busy("V775 event readout failed");
    return 0;
  }

  V1720E_EVENT_INFO v1720 = {};
  V1720E_EVENT_INFO *v1720_event = NULL;
  if (gV1720State.run.settings.enabled) {
    const int v1720_status =
      v1720e_read_event_mode(gVme, V1720E_BASE, v1720_data,
                   V1720E_MAX_EVENT_WORDS,
                   gV1720State.run.expected_event_words,
                   gV1720State.run.expected_channel_mask,
                   V1720E_READOUT_MODE_SELECT, &v1720);
    if (v1720_status != MVME_SUCCESS || !v1720.header_valid ||
      v1720.words < 4) {
      if (V1720E_READOUT_MODE_SELECT == BLT32 &&
        v1720.blt_requested_bytes != 0) {
        cm_msg(MERROR, frontend_name,
             "V1720E BLT32 failed: CAEN status=%d requested=%d bytes actual=%d bytes",
             v1720.blt_status, v1720.blt_requested_bytes,
             v1720.blt_actual_bytes);
        gVmeState.blt_stop_requested.store(true, std::memory_order_relaxed);
      }
      if (v1720_status != MVME_SUCCESS)
        gV1720State.runtime.value.communication_ok = FALSE;
      gV1720State.runtime.value.dirty = true;
      ++gVmeStatistics.run.v1720_malformed_count;
      mark_run_counters_dirty();
      gVmeState.readout_failed = true;
      cm_msg(MERROR, frontend_name,
           "V1720E readout disabled after malformed/partial event: "
           "status %d words=%zu header-valid=%d. V792/V1190"
           "/V775"
           " events "
           "were consumed but no partial MIDAS event was sent.",
           v1720_status, v1720.words, v1720.header_valid);
      fail_single_event_busy("V1720E event readout failed");
      return 0;
    }
    v1720_event = &v1720;
    const bool v1720_integrity =
      update_v1720e_integrity(v1720, SERIAL_NUMBER(pevent));
    if (!v1720_integrity) {
      gVmeState.readout_failed = true;
      fail_single_event_busy("V1720E event consistency failure");
      return 0;
    }
    gV1720State.runtime.value.event_counter = v1720.event_counter;
    gV1720State.runtime.value.trigger_time_tag = v1720.trigger_time_tag;
    gV1720State.runtime.value.dirty = true;
  }

  if (!check_event_counter_match(v792, v1190, v775, v1720_event)) {
    if (gVmeState.single_event_busy_enabled_for_run) {
      fail_single_event_busy("participating module event counters differ");
      return 0;
    }
    log_event_counter_mismatch(v792, v1190, v775, v1720_event,
                   SERIAL_NUMBER(pevent));
  }
  const INT event_size = build_midas_event(pevent,
               v792_data, v792,
               v1190_data, v1190,
               v775_data, v775,
               v1720_data, v1720_event);
  if (gVmeState.single_event_busy_enabled_for_run) {
    if (event_size <= 0) {
      fail_single_event_busy("MIDAS event construction failed");
      return 0;
    }
    uint8_t csr1 = 0;
    const int clear_status = rpv130_clear_busy1_preserving_enable_state(
      gVme, RPV130_BASE_ADDRESS, &csr1);
    if (clear_status != MVME_SUCCESS) {
      fail_single_event_busy("CLR1 or CSR1 readback failed");
      return 0;
    }
    if (!publish_rpv130_busy_state(
        (csr1 & RPV130_CSR1_BUSY1) != 0, true)) {
      fail_single_event_busy("RPV130 ODB BUSY status update failed");
      return 0;
    }
  }
  return event_size;
}

// Write the VME run configuration bank
INT read_vme_configuration_event(char *pevent, INT)
{
  if (gVmeModuleState.snapshot.frontend_bor_complete != TRUE)
    return 0;

  HNDLE hDB = 0;
  HNDLE hKey = 0;
  cm_get_experiment_database(&hDB, NULL);
  const INT find_status =
    db_find_key(hDB, 0, VME_RUN_SNAPSHOT_PATH, &hKey);
  if (find_status != DB_SUCCESS) {
    cm_msg(MERROR, frontend_name,
         "Cannot find completed VME RunSnapshot for CONFIG event: %d",
         find_status);
    return 0;
  }

  char *json = NULL;
  int json_capacity = 0;
  int json_length = 0;
  const INT json_status = db_copy_json_save(
    hDB, hKey, &json, &json_capacity, &json_length);
  if (json_status != DB_SUCCESS || json == NULL || json_length <= 0) {
    cm_msg(MERROR, frontend_name,
         "Cannot serialize VME RunSnapshot JSON: status %d length %d",
         json_status, json_length);
    free(json);
    return 0;
  }
  if (json_length > max_event_size - 64) {
    cm_msg(MERROR, frontend_name,
         "VME RunSnapshot JSON is too large: %d bytes", json_length);
    free(json);
    return 0;
  }

  bk_init32(pevent);
  char *data = NULL;
  bk_create(pevent, "VCFG", TID_CHAR, reinterpret_cast<void **>(&data));
  memcpy(data, json, static_cast<size_t>(json_length));
  bk_close(pevent, data + json_length);
  free(json);
  return bk_size(pevent);
}


/* Define the MIDAS equipment handled by this frontend. */
EQUIPMENT equipment[] = {
  {
    "VME",                    // Equipment name
    {
      1,                    // Event ID
      0,                    // Trigger mask
      "SYSTEM",             // Event buffer name
      EQ_POLLED,            // Equipment type
      0,                    // Event source
      "MIDAS",              // Data format
      TRUE,                 // Enable equipment
      RO_RUNNING,           // Readout condition
      500,                  // Polling period [ms]
      0,                    // Event limit; 0 = no automatic stop
      0,                    // Number of sub-events
      0,                    // History logging period [s]
      "",                   // Frontend host name
      "",                   // Frontend name
      "",                   // Frontend source file name
      "",                   // Equipment status text
      "",                   // Equipment status color
      FALSE                 // Hidden flag
    },
    read_vme_event,           // Event readout function
  },
  {
    "VME Configuration",
    {
      5,                    // Unused across repository and current ODB
      0,
      "SYSTEM",
      EQ_PERIODIC,
      0,
      "MIDAS",
      TRUE,
      RO_BOR,
      0,
      0,
      0,
      0,
      "", "", "", "", "", FALSE
    },
    read_vme_configuration_event,
  },

  {""}                          // End of equipment list
};
