#include "vme_odb.h"
#include "rpv130.h"
#include "v775.h"

#include <cstdio>
#include <cstring>
#include <ctime>

extern const char *frontend_name;

namespace vme_odb {

static const char *RPV130_SETTINGS_PATH = "/Equipment/VME/Settings/RPV130";
static const char *RPV130_INFO_PATH = "/Equipment/VME/Info/RPV130";
static const char *RPV130_VARIABLES_PATH = "/Equipment/VME/Variables/RPV130";
static const char *RPV130_STATUS_PATH = "/Equipment/VME/Status/RPV130";
static const char *V792_SETTINGS_PATH = "/Equipment/VME/Settings/V792";
static const char *V1190_SETTINGS_PATH = "/Equipment/VME/Settings/V1190";
static const char *V775_SETTINGS_PATH = "/Equipment/VME/Settings/V775";
static const char *V1720E_SETTINGS_PATH = "/Equipment/VME/Settings/V1720E";
static const char *V1720E_INFO_PATH = "/Equipment/VME/Info/V1720E";
static const char *V1720E_READBACK_PATH = "/Equipment/VME/Readback/V1720E";

static const char *V792_READBACK_PATH = "/Equipment/VME/Readback/V792";
static const char *V1190_READBACK_PATH = "/Equipment/VME/Readback/V1190";
static const char *V775_READBACK_PATH = "/Equipment/VME/Readback/V775";
static const char *FRONTEND_VARIABLES_PATH =
    "/Equipment/VME/Variables/Frontend";
static const char *VME_RUN_SNAPSHOT_PATH = "/Equipment/VME/RunSnapshot";
static const char *BUFFER_CLEAR_COMMAND_PATH = "/Equipment/VME/Commands/BufferClearRequestId";
static const char *BUFFER_CLEAR_STATUS_PATH = "/Equipment/VME/Variables/BufferClear";
static const DWORD RUN_SNAPSHOT_SCHEMA_VERSION = 1;
static const DWORD V1720E_TRIGGER_SOFTWARE = 0x80000000u;
static const DWORD V1720E_TRIGGER_EXTERNAL = 0x40000000u;
static const DWORD V1720E_CHANNEL_CONFIG_ZS_MASK = 0x000F0000u;
static const DWORD V1720E_CHANNEL_CONFIG_PACK25 = 0x00000800u;

//************************************//
// Build an absolute path for a VME ODB field
//************************************//
bool make_odb_path(char *path, size_t capacity, const char *base,
                          const char *name)
{
    const int length = snprintf(path, capacity, "%s/%s", base, name);
    return length >= 0 && static_cast<size_t>(length) < capacity;
}

//************************************//
// Write one absolute VME ODB field
//************************************//
bool set_absolute_odb_value(const char *path, const void *value,
                                   INT size, INT count, DWORD type)
{
    HNDLE hDB = 0;
    cm_get_experiment_database(&hDB, NULL);
    const INT status = db_set_value(hDB, 0, path, value, size, count, type);
    if (status != DB_SUCCESS)
        cm_msg(MERROR, frontend_name, "Cannot update ODB %s: status %d",
               path, status);
    return status == DB_SUCCESS;
}

//************************************//
// Create a VME ODB field only when absent
//************************************//
bool ensure_odb_value(const char *path, const void *default_value,
                             INT size, INT count, DWORD type)
{
    HNDLE hDB = 0;
    HNDLE hKey = 0;
    cm_get_experiment_database(&hDB, NULL);
    const INT find_status = db_find_key(hDB, 0, path, &hKey);
    if (find_status == DB_SUCCESS)
        return true;
    if (find_status != DB_NO_KEY) {
        cm_msg(MERROR, frontend_name, "Cannot inspect ODB %s: status %d",
               path, find_status);
        return false;
    }
    return set_absolute_odb_value(path, default_value, size, count, type);
}

//************************************//
// Read an absolute VME ODB field with its expected size
//************************************//
bool get_absolute_odb_value(const char *path, void *value, INT size,
                                   DWORD type)
{
    HNDLE hDB = 0;
    cm_get_experiment_database(&hDB, NULL);
    INT actual_size = size;
    const INT status = db_get_value(hDB, 0, path, value, &actual_size,
                                    type, FALSE);
    if (status != DB_SUCCESS || actual_size != size) {
        cm_msg(MERROR, frontend_name,
               "Cannot read ODB %s: status %d size %d expected %d",
               path, status, actual_size, size);
        return false;
    }
    return true;
}

//************************************//
// Publish VME configuration status and run identity
//************************************//
bool publish_configuration_status(bool configuration_ok,
                                         INT run_number)
{
    const BOOL value = configuration_ok ? TRUE : FALSE;
    const time_t now = time(NULL);
    const uint64_t checked_unix =
        now < 0 ? 0 : static_cast<uint64_t>(now);
    char path[256];
    bool ok = true;

    /* Clear the gate first on negative updates. On positive updates, publish
     * the identifying metadata before opening the gate. */
    if (!configuration_ok) {
        ok = make_odb_path(path, sizeof(path), FRONTEND_VARIABLES_PATH,
                           "ConfigurationOK") &&
             set_absolute_odb_value(path, &value, sizeof(value), 1,
                                    TID_BOOL) && ok;
    }
    ok = make_odb_path(path, sizeof(path), FRONTEND_VARIABLES_PATH,
                       "ConfigurationCheckedUnix") &&
         set_absolute_odb_value(path, &checked_unix, sizeof(checked_unix), 1,
                                TID_QWORD) && ok;
    ok = make_odb_path(path, sizeof(path), FRONTEND_VARIABLES_PATH,
                       "ConfigurationRunNumber") &&
         set_absolute_odb_value(path, &run_number, sizeof(run_number), 1,
                                TID_INT) && ok;
    if (configuration_ok) {
        ok = make_odb_path(path, sizeof(path), FRONTEND_VARIABLES_PATH,
                           "ConfigurationOK") &&
             set_absolute_odb_value(path, &value, sizeof(value), 1,
                                    TID_BOOL) && ok;
    }
    return ok;
}

//************************************//
// Publish one module output field in ODB
//************************************//
bool set_module_output(const char *base, const char *name,
                              const void *value, INT size, INT count,
                              DWORD type)
{
    char path[256];
    return make_odb_path(path, sizeof(path), base, name) &&
           set_absolute_odb_value(path, value, size, count, type);
}

static bool set_vme_snapshot_value(const char *relative_path,
                                   const void *value, INT size, INT count,
                                   DWORD type)
{
    char path[320];
    return make_odb_path(path, sizeof(path), VME_RUN_SNAPSHOT_PATH,
                         relative_path) &&
           set_absolute_odb_value(path, value, size, count, type);
}

static void format_iso8601_utc(time_t value, char *buffer, size_t capacity)
{
    struct tm utc = {};
    if (gmtime_r(&value, &utc) == NULL ||
        strftime(buffer, capacity, "%Y-%m-%dT%H:%M:%SZ", &utc) == 0)
        buffer[0] = '\0';
}

//************************************//
// Reset the VME run snapshot for a new run
//************************************//
void reset_run_snapshot(VmeRunSnapshot &snapshot, INT run_number,
                        const char *frontend_name)
{
    snapshot = {};
    snapshot.schema_version = RUN_SNAPSHOT_SCHEMA_VERSION;
    snapshot.run_number = run_number;
    const time_t now = time(NULL);
    snapshot.bor_unix_time =
        now < 0 ? 0 : static_cast<uint64_t>(now);
    format_iso8601_utc(now, snapshot.bor_time_iso8601,
                       sizeof(snapshot.bor_time_iso8601));
    snprintf(snapshot.frontend_name,
             sizeof(snapshot.frontend_name), "%s", frontend_name);
    snprintf(snapshot.snapshot_id,
             sizeof(snapshot.snapshot_id), "run-%d_%llu_%s",
             run_number,
             static_cast<unsigned long long>(snapshot.bor_unix_time),
             frontend_name);
    snprintf(snapshot.v1190_readback.board_type,
             sizeof(snapshot.v1190_readback.board_type), "Unknown");
}

//************************************//
// Publish the VME run snapshot with completion marker last
//************************************//
bool publish_run_snapshot(const VmeRunSnapshot &snapshot)
{
    bool ok = true;
#define SNAP(path, value, count, type) \
    do { \
        ok = set_vme_snapshot_value(path, &(value), sizeof(value), count, type) \
             && ok; \
    } while (0)
#define SNAP_STRING(path, value) \
    do { \
        ok = set_vme_snapshot_value(path, value, sizeof(value), 1, TID_STRING) \
             && ok; \
    } while (0)
    SNAP("Metadata/SchemaVersion", snapshot.schema_version, 1,
         TID_DWORD);
    SNAP_STRING("Metadata/SnapshotId", snapshot.snapshot_id);
    SNAP("Metadata/RunNumber", snapshot.run_number, 1, TID_INT);
    SNAP("Metadata/BORUnixTime", snapshot.bor_unix_time, 1,
         TID_QWORD);
    SNAP_STRING("Metadata/BORTimeISO8601",
                snapshot.bor_time_iso8601);
    SNAP_STRING("Metadata/FrontendName", snapshot.frontend_name);
    SNAP("Metadata/EnabledForRun", snapshot.enabled_for_run, 1,
         TID_BOOL);

#define SNAP_V792(base, object) \
    SNAP(base "/Enabled", object.enabled, 1, TID_BOOL); \
    SNAP(base "/Iped", object.iped, 1, TID_WORD); \
    SNAP(base "/ZeroSuppressionEnabled", object.zero_suppression_enabled, 1, TID_BOOL); \
    SNAP(base "/AllTriggerEnabled", object.all_trigger_enabled, 1, TID_BOOL)
    SNAP_V792("Requested/V792", snapshot.v792_requested);
#undef SNAP_V792

#define SNAP_V1190(base, object) \
    SNAP(base "/Enabled", object.enabled, 1, TID_BOOL); \
    SNAP(base "/TriggerMatchingEnabled", object.trigger_matching_enabled, 1, TID_BOOL); \
    SNAP(base "/WindowWidth", object.window_width, 1, TID_DWORD); \
    SNAP(base "/WindowOffset", object.window_offset, 1, TID_INT); \
    SNAP(base "/ExtraSearchMargin", object.extra_search_margin, 1, TID_DWORD); \
    SNAP(base "/RejectMargin", object.reject_margin, 1, TID_DWORD); \
    SNAP(base "/TriggerSubtractionEnabled", object.trigger_subtraction_enabled, 1, TID_BOOL); \
    SNAP(base "/EdgeMode", object.edge_mode, 1, TID_DWORD); \
    SNAP(base "/ResolutionPs", object.resolution_ps, 1, TID_DWORD); \
    SNAP(base "/DeadTimeNs", object.dead_time_ns, 1, TID_DWORD); \
    SNAP(base "/MaxHitsPerEvent", object.max_hits_per_event, 1, TID_INT); \
    SNAP(base "/TdcHeaderEnabled", object.tdc_header_enabled, 1, TID_BOOL); \
    SNAP(base "/EmptyEventEnabled", object.empty_event_enabled, 1, TID_BOOL); \
    SNAP(base "/EventFifoEnabled", object.event_fifo_enabled, 1, TID_BOOL); \
    SNAP(base "/ExtendedTriggerTimeEnabled", object.extended_trigger_time_enabled, 1, TID_BOOL); \
    SNAP(base "/ChannelEnabled", object.channel_enabled, 128, TID_BOOL)
    SNAP_V1190("Requested/V1190", snapshot.v1190_requested);
#undef SNAP_V1190

#define SNAP_V775(base, object) \
    SNAP(base "/Enabled", object.enabled, 1, TID_BOOL); \
    SNAP(base "/FullScaleRange", object.full_scale_range, 1, TID_WORD); \
    SNAP(base "/OverRangeEnabled", object.over_range_enabled, 1, TID_BOOL); \
    SNAP(base "/LowThresholdEnabled", object.low_threshold_enabled, 1, TID_BOOL); \
    SNAP(base "/CommonStop", object.common_stop, 1, TID_BOOL); \
    SNAP(base "/EmptyProgramEnabled", object.empty_program_enabled, 1, TID_BOOL); \
    SNAP(base "/ValidControlEnabled", object.valid_control_enabled, 1, TID_BOOL); \
    SNAP(base "/SlidingScaleEnabled", object.sliding_scale_enabled, 1, TID_BOOL); \
    SNAP(base "/AllTriggerEnabled", object.all_trigger_enabled, 1, TID_BOOL)
    SNAP_V775("Requested/V775", snapshot.v775_requested);
#undef SNAP_V775

    SNAP("Requested/V1720E/Enabled", snapshot.v1720e_requested.enabled,
         1, TID_BOOL);
    SNAP("Requested/V1720E/BufferOrganization",
         snapshot.v1720e_requested.buffer_organization, 1, TID_DWORD);
    SNAP("Requested/V1720E/RecordLengthSamples",
         snapshot.v1720e_requested.record_length_samples, 1,
         TID_DWORD);
    SNAP("Requested/V1720E/PostTrigger",
         snapshot.v1720e_requested.post_trigger, 1, TID_DWORD);
    SNAP("Requested/V1720E/SoftwareTriggerEnabled",
         snapshot.v1720e_requested.software_trigger_enabled, 1,
         TID_BOOL);
    SNAP("Requested/V1720E/ExternalTriggerEnabled",
         snapshot.v1720e_requested.external_trigger_enabled, 1,
         TID_BOOL);
    SNAP("Requested/V1720E/ChannelSelfTriggerEnabled",
         snapshot.v1720e_requested.channel_self_trigger_enabled,
         V1720E_CHANNEL_COUNT, TID_BOOL);
    SNAP("Requested/V1720E/ChannelEnabled",
         snapshot.v1720e_requested.channel_enabled,
         V1720E_CHANNEL_COUNT, TID_BOOL);
    SNAP("Requested/V1720E/DCOffset",
         snapshot.v1720e_requested.dc_offset, V1720E_CHANNEL_COUNT,
         TID_WORD);
    SNAP("Requested/RPV130/Enabled", snapshot.rpv130_enabled, 1,
         TID_BOOL);
    SNAP("Requested/RPV130/SingleEventBusyEnabled",
         snapshot.rpv130_single_event_busy_enabled, 1, TID_BOOL);

    SNAP("Readback/V792/Valid", snapshot.v792_readback.valid, 1,
         TID_BOOL);
    SNAP("Readback/V792/FirmwareRevision",
         snapshot.v792_readback.firmware_revision, 1, TID_WORD);
    SNAP("Readback/V792/Iped", snapshot.v792_readback.iped, 1,
         TID_WORD);
    SNAP("Readback/V792/ZeroSuppressionEnabled",
         snapshot.v792_readback.zero_suppression_enabled, 1,
         TID_BOOL);
    SNAP("Readback/V792/AllTriggerEnabled",
         snapshot.v792_readback.all_trigger_enabled, 1, TID_BOOL);
    SNAP("Readback/V792/BitSet2Raw",
         snapshot.v792_readback.bit_set2_raw, 1, TID_WORD);
    SNAP("Readback/V792/Threshold",
         snapshot.v792_readback.threshold, 32, TID_WORD);

    const V1190ReadbackSnapshot &r1190 = snapshot.v1190_readback;
    SNAP("Readback/V1190/Valid", r1190.valid, 1, TID_BOOL);
    SNAP("Readback/V1190/FirmwareRevision", r1190.firmware_revision, 1,
         TID_WORD);
    SNAP("Readback/V1190/ConfigurationRomVersion",
         r1190.configuration_rom_version, 1, TID_WORD);
    SNAP_STRING("Readback/V1190/BoardType", r1190.board_type);
#define SNAP_R1190(name, member, count, type) \
    SNAP("Readback/V1190/" name, r1190.settings.member, count, type)
    SNAP_R1190("TriggerMatchingEnabled", trigger_matching_enabled, 1, TID_BOOL);
    SNAP_R1190("WindowWidth", window_width, 1, TID_DWORD);
    SNAP_R1190("WindowOffset", window_offset, 1, TID_INT);
    SNAP_R1190("ExtraSearchMargin", extra_search_margin, 1, TID_DWORD);
    SNAP_R1190("RejectMargin", reject_margin, 1, TID_DWORD);
    SNAP_R1190("TriggerSubtractionEnabled", trigger_subtraction_enabled, 1, TID_BOOL);
    SNAP_R1190("EdgeMode", edge_mode, 1, TID_DWORD);
    SNAP_R1190("ResolutionPs", resolution_ps, 1, TID_DWORD);
    SNAP_R1190("DeadTimeNs", dead_time_ns, 1, TID_DWORD);
    SNAP_R1190("MaxHitsPerEvent", max_hits_per_event, 1, TID_INT);
    SNAP_R1190("TdcHeaderEnabled", tdc_header_enabled, 1, TID_BOOL);
    SNAP_R1190("EmptyEventEnabled", empty_event_enabled, 1, TID_BOOL);
    SNAP_R1190("EventFifoEnabled", event_fifo_enabled, 1, TID_BOOL);
    SNAP_R1190("ExtendedTriggerTimeEnabled", extended_trigger_time_enabled, 1, TID_BOOL);
    SNAP_R1190("ChannelEnabled", channel_enabled, 128, TID_BOOL);
#undef SNAP_R1190
    SNAP("Readback/V1190/ErrorMask", r1190.error_mask, 1, TID_WORD);
    SNAP("Readback/V1190/EffectiveFifoSizeWords",
         r1190.effective_fifo_size_words, 1, TID_DWORD);
    SNAP("Readback/V1190/ControlRaw", r1190.control_raw, 1, TID_WORD);
    SNAP("Readback/V1190/POUTSelection", r1190.pout_selection, 1, TID_WORD);
    SNAP_STRING("Readback/V1190/POUTFunction", r1190.pout_function);
    SNAP("Readback/V1190/AlmostFullLevelWords",
         r1190.almost_full_level_words, 1, TID_WORD);

    const V775ReadbackSnapshot &r775 = snapshot.v775_readback;
    SNAP("Readback/V775/Valid", r775.valid, 1, TID_BOOL);
    SNAP("Readback/V775/FirmwareRevision", r775.firmware_revision, 1,
         TID_WORD);
    SNAP("Readback/V775/FullScaleRange", r775.full_scale_range, 1,
         TID_WORD);
    SNAP("Readback/V775/FastClearWindow", r775.fast_clear_window, 1,
         TID_WORD);
#define SNAP_R775(name, member, type) \
    SNAP("Readback/V775/" name, r775.member, 1, type)
    SNAP_R775("OverRangeEnabled", over_range_enabled, TID_BOOL);
    SNAP_R775("LowThresholdEnabled", low_threshold_enabled, TID_BOOL);
    SNAP_R775("CommonStop", common_stop, TID_BOOL);
    SNAP_R775("EmptyProgramEnabled", empty_program_enabled, TID_BOOL);
    SNAP_R775("ValidControlEnabled", valid_control_enabled, TID_BOOL);
    SNAP_R775("SlidingScaleEnabled", sliding_scale_enabled, TID_BOOL);
    SNAP_R775("AllTriggerEnabled", all_trigger_enabled, TID_BOOL);
#undef SNAP_R775
    SNAP("Readback/V775/BitSet2Raw", r775.bit_set2_raw, 1, TID_WORD);
    SNAP("Readback/V775/Threshold", r775.threshold, 32, TID_WORD);

    const V1720EReadbackSnapshot &r1720 = snapshot.v1720e_readback;
    SNAP("Readback/V1720E/Valid", r1720.valid, 1, TID_BOOL);
    SNAP("Readback/V1720E/BoardInfo", r1720.board_info, 1, TID_DWORD);
    SNAP("Readback/V1720E/RocFirmwareRevision",
         r1720.roc_firmware_revision, 1, TID_DWORD);
    SNAP("Readback/V1720E/BufferOrganization",
         r1720.buffer_organization, 1, TID_DWORD);
    SNAP("Readback/V1720E/CustomSizeRaw", r1720.custom_size_raw, 1,
         TID_DWORD);
    SNAP("Readback/V1720E/RecordLengthSamples",
         r1720.record_length_samples, 1, TID_DWORD);
    SNAP("Readback/V1720E/PostTrigger", r1720.post_trigger, 1, TID_DWORD);
    SNAP("Readback/V1720E/SoftwareTriggerEnabled",
         r1720.software_trigger_enabled, 1, TID_BOOL);
    SNAP("Readback/V1720E/ExternalTriggerEnabled",
         r1720.external_trigger_enabled, 1, TID_BOOL);
    SNAP("Readback/V1720E/ChannelSelfTriggerEnabled",
         r1720.channel_self_trigger_enabled, V1720E_CHANNEL_COUNT, TID_BOOL);
    SNAP("Readback/V1720E/ChannelEnabled", r1720.channel_enabled,
         V1720E_CHANNEL_COUNT, TID_BOOL);
    SNAP("Readback/V1720E/DCOffset", r1720.dc_offset,
         V1720E_CHANNEL_COUNT, TID_WORD);
    SNAP("Readback/V1720E/ZeroSuppressionEnabled",
         r1720.zero_suppression_enabled, 1, TID_BOOL);
    SNAP("Readback/V1720E/Pack25Enabled", r1720.pack25_enabled, 1,
         TID_BOOL);
    SNAP("Readback/V1720E/TriggerSourceRaw", r1720.trigger_source_raw, 1,
         TID_DWORD);
    SNAP("Readback/V1720E/ChannelEnableRaw", r1720.channel_enable_raw, 1,
         TID_DWORD);
    SNAP("Readback/V1720E/ChannelConfigRaw", r1720.channel_config_raw, 1,
         TID_DWORD);

    /* Publish the completion marker last. If any preceding write failed,
     * leave the fixed subtree explicitly incomplete. */
    {
        BOOL published_complete =
            (snapshot.frontend_bor_complete && ok) ? TRUE : FALSE;
        const bool complete_ok = set_vme_snapshot_value(
            "Metadata/FrontendBORComplete", &published_complete,
            sizeof(published_complete), 1, TID_BOOL);
        ok = complete_ok && ok;
    }
#undef SNAP_STRING
#undef SNAP
    return ok;
}


bool publish_v7xx_variables(const char *path, V7xxRuntimeState &r,
                                   DWORD &last)
{
    bool ok=true;
#define PV7(k,m,t) do { ok=set_module_output(path,k,&r.m,sizeof(r.m),1,t)&&ok; } while(0)
    PV7("EnabledForRun",enabled_for_run,TID_BOOL);
    PV7("CommunicationOK",communication_ok,TID_BOOL); PV7("Status1",status1,TID_WORD);
    PV7("Status2",status2,TID_WORD); PV7("DataReady",data_ready,TID_BOOL);
    PV7("Busy",busy,TID_BOOL); PV7("BufferEmpty",buffer_empty,TID_BOOL);
    PV7("BufferFull",buffer_full,TID_BOOL); PV7("EventCounter",event_counter,TID_DWORD);
#undef PV7
    r.dirty=!ok; last=ss_millitime(); return ok;
}

//************************************//
// Publish V1190 runtime variables in ODB
//************************************//
bool publish_v1190_variables(V1190RuntimeState &r, DWORD &last)
{
    bool ok=true;
#define PV1190(k,m,t) do { ok=set_module_output("/Equipment/VME/Variables/V1190",k,&r.m,sizeof(r.m),1,t)&&ok; } while(0)
    PV1190("EnabledForRun",enabled_for_run,TID_BOOL);
    PV1190("CommunicationOK",communication_ok,TID_BOOL); PV1190("Status",status,TID_WORD);
    PV1190("DataReady",data_ready,TID_BOOL); PV1190("AlmostFull",almost_full,TID_BOOL);
    PV1190("Full",full,TID_BOOL); PV1190("TriggerMatching",trigger_matching,TID_BOOL);
    PV1190("EventStored",event_stored,TID_DWORD); PV1190("EventCounter",event_counter,TID_DWORD);
#undef PV1190
    r.dirty=!ok; last=ss_millitime(); return ok;
}

//************************************//
// Publish V1720E runtime variables in ODB
//************************************//
bool publish_v1720e_variables(V1720ERuntimeState &runtime, DWORD &last_publish)
{
    bool ok = true;
#define PUBLISH_VARIABLE(name, member, type) \
    do { \
        ok = set_module_output("/Equipment/VME/Variables/V1720E", name, \
                               &runtime.member, \
                               sizeof(runtime.member), 1, type) && ok; \
    } while (0)
    PUBLISH_VARIABLE("EnabledForRun", enabled_for_run, TID_BOOL);
    PUBLISH_VARIABLE("CommunicationOK", communication_ok, TID_BOOL);
    PUBLISH_VARIABLE("AcquisitionControl", acquisition_control, TID_DWORD);
    PUBLISH_VARIABLE("AcquisitionStatus", acquisition_status, TID_DWORD);
    PUBLISH_VARIABLE("Running", running, TID_BOOL);
    PUBLISH_VARIABLE("EventReady", event_ready, TID_BOOL);
    PUBLISH_VARIABLE("ExternalClock", external_clock, TID_BOOL);
    PUBLISH_VARIABLE("PllLocked", pll_locked, TID_BOOL);
    PUBLISH_VARIABLE("BoardReady", board_ready, TID_BOOL);
    PUBLISH_VARIABLE("EventStored", event_stored, TID_DWORD);
    PUBLISH_VARIABLE("EventCounter", event_counter, TID_DWORD);
    PUBLISH_VARIABLE("TriggerTimeTag", trigger_time_tag, TID_DWORD);
#undef PUBLISH_VARIABLE
    runtime.dirty = !ok;
    last_publish = ss_millitime();
    return ok;
}

//************************************//
// Publish per-run VME diagnostic counters in ODB
//************************************//
bool publish_run_counters(const RunStatistics &counters, bool &dirty,
                          DWORD &last_publish)
{
    bool ok = true;
#define PUBLISH_RUN_COUNTER(name, member, type) \
    do { \
        ok = set_module_output("/Equipment/VME/Variables/RunCounters", name, \
                               &counters.member, \
                               sizeof(counters.member), 1, type) && ok; \
    } while (0)
    PUBLISH_RUN_COUNTER("EventSlipCount", counter_mismatch_count, TID_QWORD);
    PUBLISH_RUN_COUNTER("V1720EMalformedEventCount", v1720_malformed_count,
                        TID_QWORD);
    PUBLISH_RUN_COUNTER("V1720ESizeErrorCount", v1720_size_error_count,
                        TID_QWORD);
    PUBLISH_RUN_COUNTER("V1720EChannelMaskErrorCount", v1720_mask_error_count,
                        TID_QWORD);
    PUBLISH_RUN_COUNTER("V1720EReadTimeoutCount", v1720_read_timeout_count,
                        TID_QWORD);
    PUBLISH_RUN_COUNTER("V1720ECounterDiscontinuityCount",
                        v1720_counter_discontinuity_count, TID_QWORD);
    PUBLISH_RUN_COUNTER("FirstEventSlipSerial", first_mismatch_serial,
                        TID_DWORD);
    PUBLISH_RUN_COUNTER("LastEventSlipSerial", last_mismatch_serial,
                        TID_DWORD);
#undef PUBLISH_RUN_COUNTER
    dirty = !ok;
    last_publish = ss_millitime();
    return ok;
}

//************************************//
// Initialize RPV130 settings and status in ODB
//************************************//
bool initialize_rpv130_odb(bool &rpv130_enabled_for_run,
                            bool &single_event_busy_enabled_for_run)
{
    const BOOL default_enabled = TRUE;
    const BOOL default_single_event_busy = FALSE;
    const DWORD base_address = RPV130_BASE_ADDRESS;
    const char address_modifier[] = "A16_ND";
    const char register_width[] = "D16";
    const BOOL default_communication_ok = FALSE;
    const BYTE default_status = 0;
    char path[256];

    if (!make_odb_path(path, sizeof(path), RPV130_SETTINGS_PATH, "Enabled") ||
        !ensure_odb_value(path, &default_enabled, sizeof(default_enabled), 1,
                          TID_BOOL))
        return false;
    BOOL startup_enabled = FALSE;
    if (!get_absolute_odb_value(path, &startup_enabled,
                                sizeof(startup_enabled), TID_BOOL))
        return false;
    rpv130_enabled_for_run = startup_enabled != FALSE;
    if (!make_odb_path(path, sizeof(path), RPV130_SETTINGS_PATH,
                       "SingleEventBusyEnabled") ||
        !ensure_odb_value(path, &default_single_event_busy,
                          sizeof(default_single_event_busy), 1, TID_BOOL))
        return false;
    BOOL startup_busy_enabled = FALSE;
    if (!get_absolute_odb_value(path, &startup_busy_enabled,
                                sizeof(startup_busy_enabled), TID_BOOL))
        return false;
    single_event_busy_enabled_for_run = startup_busy_enabled != FALSE;
    if (!set_module_output(RPV130_STATUS_PATH, "Busy1",
                           &default_single_event_busy,
                           sizeof(default_single_event_busy), 1, TID_BOOL) ||
        !set_module_output(RPV130_STATUS_PATH, "SingleEventBusyArmed",
                           &default_single_event_busy,
                           sizeof(default_single_event_busy), 1, TID_BOOL))
        return false;

#define SET_RPV130_INFO(name, value, size, type) \
    do { \
        if (!make_odb_path(path, sizeof(path), RPV130_INFO_PATH, name) || \
            !set_absolute_odb_value(path, value, size, 1, type)) \
            return false; \
    } while (0)
    SET_RPV130_INFO("BaseAddress", &base_address, sizeof(base_address), TID_DWORD);
    SET_RPV130_INFO("AddressModifier", address_modifier,
                    sizeof(address_modifier), TID_STRING);
    SET_RPV130_INFO("RegisterDataWidth", register_width,
                    sizeof(register_width), TID_STRING);
#undef SET_RPV130_INFO

#define SET_RPV130_VARIABLE_DEFAULT(name, value, size, type) \
    do { \
        if (!make_odb_path(path, sizeof(path), RPV130_VARIABLES_PATH, name) || \
            !set_absolute_odb_value(path, value, size, 1, type)) \
            return false; \
    } while (0)
    SET_RPV130_VARIABLE_DEFAULT("CommunicationOK", &default_communication_ok,
                                sizeof(default_communication_ok), TID_BOOL);
    SET_RPV130_VARIABLE_DEFAULT("EnabledForRun", &default_communication_ok,
                                sizeof(default_communication_ok), TID_BOOL);
    SET_RPV130_VARIABLE_DEFAULT("Latch1", &default_status,
                                sizeof(default_status), TID_BYTE);
    SET_RPV130_VARIABLE_DEFAULT("Latch2", &default_status,
                                sizeof(default_status), TID_BYTE);
    SET_RPV130_VARIABLE_DEFAULT("RSFF", &default_status,
                                sizeof(default_status), TID_BYTE);
    SET_RPV130_VARIABLE_DEFAULT("Through", &default_status,
                                sizeof(default_status), TID_BYTE);
    SET_RPV130_VARIABLE_DEFAULT("CSR1", &default_status,
                                sizeof(default_status), TID_BYTE);
    SET_RPV130_VARIABLE_DEFAULT("CSR2", &default_status,
                                sizeof(default_status), TID_BYTE);
#undef SET_RPV130_VARIABLE_DEFAULT
    const BOOL enabled_for_run = rpv130_enabled_for_run ? TRUE : FALSE;
    if (!make_odb_path(path, sizeof(path), RPV130_VARIABLES_PATH,
                       "EnabledForRun") ||
        !set_absolute_odb_value(path, &enabled_for_run,
                                sizeof(enabled_for_run), 1, TID_BOOL))
        return false;
    return true;
}

//************************************//
// Publish fixed VME module information in ODB
//************************************//
bool publish_module_info(const char *path, DWORD base_address,
                                const char *address_modifier,
                                const char *register_width,
                                const char *event_width)
{
    return set_module_output(path, "BaseAddress", &base_address,
                             sizeof(base_address), 1, TID_DWORD) &&
           set_module_output(path, "AddressModifier", address_modifier,
                             strlen(address_modifier) + 1, 1, TID_STRING) &&
           set_module_output(path, "RegisterDataWidth", register_width,
                             strlen(register_width) + 1, 1, TID_STRING) &&
           set_module_output(path, "EventDataWidth", event_width,
                             strlen(event_width) + 1, 1, TID_STRING);
}

//************************************//
// Initialize V792 settings in ODB
//************************************//
bool ensure_v792_settings_schema(const V792Settings &d)
{
    char p[256];
#define E792(n, m, t) do { if (!make_odb_path(p,sizeof(p),V792_SETTINGS_PATH,n) || !ensure_odb_value(p,&d.m,sizeof(d.m),1,t)) return false; } while (0)
    E792("Enabled", enabled, TID_BOOL);
    E792("Iped", iped, TID_WORD);
    E792("ZeroSuppressionEnabled", zero_suppression_enabled, TID_BOOL);
    E792("AllTriggerEnabled", all_trigger_enabled, TID_BOOL);
#undef E792
    return true;
}

//************************************//
// Initialize V1190 settings in ODB
//************************************//
bool ensure_v1190_settings_schema(const V1190Settings &d)
{
    char p[256];
#define E1190(n, m, c, t) do { if (!make_odb_path(p,sizeof(p),V1190_SETTINGS_PATH,n) || !ensure_odb_value(p,&d.m,sizeof(d.m),c,t)) return false; } while (0)
    E1190("Enabled", enabled, 1, TID_BOOL);
    E1190("TriggerMatchingEnabled", trigger_matching_enabled, 1, TID_BOOL);
    E1190("WindowWidth", window_width, 1, TID_DWORD);
    E1190("WindowOffset", window_offset, 1, TID_INT);
    E1190("ExtraSearchMargin", extra_search_margin, 1, TID_DWORD);
    E1190("RejectMargin", reject_margin, 1, TID_DWORD);
    E1190("TriggerSubtractionEnabled", trigger_subtraction_enabled, 1, TID_BOOL);
    E1190("EdgeMode", edge_mode, 1, TID_DWORD);
    E1190("ResolutionPs", resolution_ps, 1, TID_DWORD);
    E1190("DeadTimeNs", dead_time_ns, 1, TID_DWORD);
    E1190("MaxHitsPerEvent", max_hits_per_event, 1, TID_INT);
    E1190("TdcHeaderEnabled", tdc_header_enabled, 1, TID_BOOL);
    E1190("EmptyEventEnabled", empty_event_enabled, 1, TID_BOOL);
    E1190("EventFifoEnabled", event_fifo_enabled, 1, TID_BOOL);
    E1190("ExtendedTriggerTimeEnabled", extended_trigger_time_enabled, 1, TID_BOOL);
    E1190("ChannelEnabled", channel_enabled, 128, TID_BOOL);
#undef E1190
    return true;
}

//************************************//
// Initialize V775 settings in ODB
//************************************//
bool ensure_v775_settings_schema(const V775Settings &d)
{
    char p[256];
#define E775(n, m, t) do { if (!make_odb_path(p,sizeof(p),V775_SETTINGS_PATH,n) || !ensure_odb_value(p,&d.m,sizeof(d.m),1,t)) return false; } while (0)
    E775("Enabled", enabled, TID_BOOL);
    E775("FullScaleRange", full_scale_range, TID_WORD);
    E775("OverRangeEnabled", over_range_enabled, TID_BOOL);
    E775("LowThresholdEnabled", low_threshold_enabled, TID_BOOL);
    E775("CommonStop", common_stop, TID_BOOL);
    E775("EmptyProgramEnabled", empty_program_enabled, TID_BOOL);
    E775("ValidControlEnabled", valid_control_enabled, TID_BOOL);
    E775("SlidingScaleEnabled", sliding_scale_enabled, TID_BOOL);
    E775("AllTriggerEnabled", all_trigger_enabled, TID_BOOL);
#undef E775
    return true;
}

//************************************//
// Read V792 settings from ODB
//************************************//
bool read_v792_settings(V792Settings &s)
{
    V792Settings n = {}; char p[256];
#define R792(k,m,t) do { if (!make_odb_path(p,sizeof(p),V792_SETTINGS_PATH,k) || !get_absolute_odb_value(p,&n.m,sizeof(n.m),t)) return false; } while (0)
    R792("Enabled",enabled,TID_BOOL); R792("Iped",iped,TID_WORD);
    R792("ZeroSuppressionEnabled",zero_suppression_enabled,TID_BOOL);
    R792("AllTriggerEnabled",all_trigger_enabled,TID_BOOL);
#undef R792
    s=n; return true;
}

//************************************//
// Read V1190 settings from ODB
//************************************//
bool read_v1190_settings(V1190Settings &s)
{
    V1190Settings n = {}; char p[256];
#define R1190(k,m,t) do { if (!make_odb_path(p,sizeof(p),V1190_SETTINGS_PATH,k) || !get_absolute_odb_value(p,&n.m,sizeof(n.m),t)) return false; } while (0)
    R1190("Enabled",enabled,TID_BOOL); R1190("TriggerMatchingEnabled",trigger_matching_enabled,TID_BOOL);
    R1190("WindowWidth",window_width,TID_DWORD); R1190("WindowOffset",window_offset,TID_INT);
    R1190("ExtraSearchMargin",extra_search_margin,TID_DWORD); R1190("RejectMargin",reject_margin,TID_DWORD);
    R1190("TriggerSubtractionEnabled",trigger_subtraction_enabled,TID_BOOL); R1190("EdgeMode",edge_mode,TID_DWORD);
    R1190("ResolutionPs",resolution_ps,TID_DWORD); R1190("DeadTimeNs",dead_time_ns,TID_DWORD);
    R1190("MaxHitsPerEvent",max_hits_per_event,TID_INT); R1190("TdcHeaderEnabled",tdc_header_enabled,TID_BOOL);
    R1190("EmptyEventEnabled",empty_event_enabled,TID_BOOL); R1190("EventFifoEnabled",event_fifo_enabled,TID_BOOL);
    R1190("ExtendedTriggerTimeEnabled",extended_trigger_time_enabled,TID_BOOL); R1190("ChannelEnabled",channel_enabled,TID_BOOL);
#undef R1190
    s=n; return true;
}

//************************************//
// Read V775 settings from ODB
//************************************//
bool read_v775_settings(V775Settings &s)
{
    V775Settings n = {}; char p[256];
#define R775(k,m,t) do { if (!make_odb_path(p,sizeof(p),V775_SETTINGS_PATH,k) || !get_absolute_odb_value(p,&n.m,sizeof(n.m),t)) return false; } while (0)
    R775("Enabled",enabled,TID_BOOL); R775("FullScaleRange",full_scale_range,TID_WORD);
    R775("OverRangeEnabled",over_range_enabled,TID_BOOL); R775("LowThresholdEnabled",low_threshold_enabled,TID_BOOL);
    R775("CommonStop",common_stop,TID_BOOL); R775("EmptyProgramEnabled",empty_program_enabled,TID_BOOL);
    R775("ValidControlEnabled",valid_control_enabled,TID_BOOL); R775("SlidingScaleEnabled",sliding_scale_enabled,TID_BOOL);
    R775("AllTriggerEnabled",all_trigger_enabled,TID_BOOL);
#undef R775
    s=n; return true;
}

//************************************//
// Initialize V1720E settings in ODB
//************************************//
bool ensure_v1720e_settings_schema(const V1720ESettings &defaults)
{
    char path[256];
#define ENSURE_SETTING(name, member, count, type) \
    do { \
        if (!make_odb_path(path, sizeof(path), V1720E_SETTINGS_PATH, name) || \
            !ensure_odb_value(path, &defaults.member, sizeof(defaults.member), \
                              count, type)) return false; \
    } while (0)
    ENSURE_SETTING("Enabled", enabled, 1, TID_BOOL);
    ENSURE_SETTING("BufferOrganization", buffer_organization, 1, TID_DWORD);
    ENSURE_SETTING("RecordLengthSamples", record_length_samples, 1, TID_DWORD);
    ENSURE_SETTING("PostTrigger", post_trigger, 1, TID_DWORD);
    ENSURE_SETTING("SoftwareTriggerEnabled", software_trigger_enabled, 1, TID_BOOL);
    ENSURE_SETTING("ExternalTriggerEnabled", external_trigger_enabled, 1, TID_BOOL);
    ENSURE_SETTING("ChannelSelfTriggerEnabled", channel_self_trigger_enabled,
                   V1720E_CHANNEL_COUNT, TID_BOOL);
    ENSURE_SETTING("ChannelEnabled", channel_enabled,
                   V1720E_CHANNEL_COUNT, TID_BOOL);
    ENSURE_SETTING("DCOffset", dc_offset, V1720E_CHANNEL_COUNT, TID_WORD);
#undef ENSURE_SETTING
    return true;
}

//************************************//
// Read V1720E settings from ODB
//************************************//
bool read_v1720e_settings(V1720ESettings &settings)
{
    V1720ESettings next = {};
    char path[256];
#define READ_SETTING(name, member, type) \
    do { \
        if (!make_odb_path(path, sizeof(path), V1720E_SETTINGS_PATH, name) || \
            !get_absolute_odb_value(path, &next.member, sizeof(next.member), \
                                    type)) return false; \
    } while (0)
    READ_SETTING("Enabled", enabled, TID_BOOL);
    READ_SETTING("BufferOrganization", buffer_organization, TID_DWORD);
    READ_SETTING("RecordLengthSamples", record_length_samples, TID_DWORD);
    READ_SETTING("PostTrigger", post_trigger, TID_DWORD);
    READ_SETTING("SoftwareTriggerEnabled", software_trigger_enabled, TID_BOOL);
    READ_SETTING("ExternalTriggerEnabled", external_trigger_enabled, TID_BOOL);
    READ_SETTING("ChannelSelfTriggerEnabled", channel_self_trigger_enabled, TID_BOOL);
    READ_SETTING("ChannelEnabled", channel_enabled, TID_BOOL);
    READ_SETTING("DCOffset", dc_offset, TID_WORD);
#undef READ_SETTING
    settings = next;
    return true;
}

//************************************//
// Publish fixed V1720E information in ODB
//************************************//
bool publish_v1720e_info()
{
    const DWORD base_address = V1720E_BASE_ADDRESS;
    const char address_modifier[] = "A32_ND";
    const char register_width[] = "D32";
    const char event_width[] = "D32";
    char path[256];
#define SET_INFO(name, value, size, count, type) \
    do { \
        if (!make_odb_path(path, sizeof(path), V1720E_INFO_PATH, name) || \
            !set_absolute_odb_value(path, value, size, count, type)) \
            return false; \
    } while (0)
    SET_INFO("BaseAddress", &base_address, sizeof(base_address), 1, TID_DWORD);
    SET_INFO("AddressModifier", address_modifier, sizeof(address_modifier), 1, TID_STRING);
    SET_INFO("RegisterDataWidth", register_width, sizeof(register_width), 1, TID_STRING);
    SET_INFO("EventDataWidth", event_width, sizeof(event_width), 1, TID_STRING);
#undef SET_INFO
    return true;
}

//************************************//
// Publish a module readback validity flag
//************************************//
void set_module_readback_valid(const char *path, bool valid)
{
    const BOOL v=valid?TRUE:FALSE;
    set_module_output(path,"Valid",&v,sizeof(v),1,TID_BOOL);
}

//************************************//
// Publish verified V792 register values to ODB
//************************************//
void publish_v792_readback(WORD firmware, WORD iped, BOOL zero_suppression,
                           BOOL all_trigger, WORD bits,
                           const WORD (&thresholds)[32], bool valid)
{
    set_module_output(V792_READBACK_PATH,"FirmwareRevision",&firmware,sizeof(firmware),1,TID_WORD);
    set_module_output(V792_READBACK_PATH,"Iped",&iped,sizeof(iped),1,TID_WORD);
    set_module_output(V792_READBACK_PATH,"ZeroSuppressionEnabled",&zero_suppression,sizeof(zero_suppression),1,TID_BOOL);
    set_module_output(V792_READBACK_PATH,"AllTriggerEnabled",&all_trigger,sizeof(all_trigger),1,TID_BOOL);
    set_module_output(V792_READBACK_PATH,"BitSet2Raw",&bits,sizeof(bits),1,TID_WORD);
    set_module_output(V792_READBACK_PATH,"Threshold",thresholds,sizeof(thresholds),32,TID_WORD);
    set_module_readback_valid(V792_READBACK_PATH,valid);
}

//************************************//
// Publish verified V775 register values to ODB
//************************************//
void publish_v775_readback(WORD firmware, WORD full_scale, WORD fast_clear,
                           WORD bits, const WORD (&thresholds)[32], bool valid)
{
    set_module_output(V775_READBACK_PATH,"FirmwareRevision",&firmware,sizeof(firmware),1,TID_WORD);
    set_module_output(V775_READBACK_PATH,"FullScaleRange",&full_scale,sizeof(full_scale),1,TID_WORD);
    set_module_output(V775_READBACK_PATH,"FastClearWindow",&fast_clear,sizeof(fast_clear),1,TID_WORD);
#define RB775(k,m,b) { const BOOL v=!!(bits&b); set_module_output(V775_READBACK_PATH,k,&v,sizeof(v),1,TID_BOOL); }
    RB775("OverRangeEnabled",over_range_enabled,V775_BIT2_OVER_RANGE); RB775("LowThresholdEnabled",low_threshold_enabled,V775_BIT2_LOW_THRESHOLD); RB775("CommonStop",common_stop,V775_BIT2_COMMON_STOP); RB775("EmptyProgramEnabled",empty_program_enabled,V775_BIT2_EMPTY_PROGRAM); RB775("ValidControlEnabled",valid_control_enabled,V775_BIT2_VALID_CONTROL); RB775("SlidingScaleEnabled",sliding_scale_enabled,V775_BIT2_SLIDE_ENABLE); RB775("AllTriggerEnabled",all_trigger_enabled,V775_BIT2_ALL_TRIGGER);
#undef RB775
    set_module_output(V775_READBACK_PATH,"BitSet2Raw",&bits,sizeof(bits),1,TID_WORD);
    set_module_output(V775_READBACK_PATH,"Threshold",thresholds,sizeof(thresholds),32,TID_WORD);
    set_module_readback_valid(V775_READBACK_PATH,valid);
}

//************************************//
// Initialize VME module output records in ODB
//************************************//
void initialize_module_output_schema(
    V7xxRuntimeState &v792, V1190RuntimeState &v1190, V7xxRuntimeState &v775,
    DWORD &v792_last_publish, DWORD &v1190_last_publish,
    DWORD &v775_last_publish)
{
    WORD zword=0, thresholds[32]={}; DWORD zdword=0; BOOL zbool=FALSE;
    set_module_readback_valid(V792_READBACK_PATH,false);
    set_module_output(V792_READBACK_PATH,"FirmwareRevision",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V792_READBACK_PATH,"Iped",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V792_READBACK_PATH,"ZeroSuppressionEnabled",&zbool,sizeof(zbool),1,TID_BOOL);
    set_module_output(V792_READBACK_PATH,"AllTriggerEnabled",&zbool,sizeof(zbool),1,TID_BOOL);
    set_module_output(V792_READBACK_PATH,"BitSet2Raw",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V792_READBACK_PATH,"Threshold",thresholds,sizeof(thresholds),32,TID_WORD);

    set_module_readback_valid(V1190_READBACK_PATH,false);
    const char empty_board[]="Unknown";
    set_module_output(V1190_READBACK_PATH,"FirmwareRevision",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V1190_READBACK_PATH,"ConfigurationRomVersion",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V1190_READBACK_PATH,"BoardType",empty_board,sizeof(empty_board),1,TID_STRING);
    const V1190Settings d1190={};
#define Z1190(k,m,c,t) set_module_output(V1190_READBACK_PATH,k,&d1190.m,sizeof(d1190.m),c,t)
    Z1190("TriggerMatchingEnabled",trigger_matching_enabled,1,TID_BOOL);
    Z1190("WindowWidth",window_width,1,TID_DWORD); Z1190("WindowOffset",window_offset,1,TID_INT);
    Z1190("ExtraSearchMargin",extra_search_margin,1,TID_DWORD); Z1190("RejectMargin",reject_margin,1,TID_DWORD);
    Z1190("TriggerSubtractionEnabled",trigger_subtraction_enabled,1,TID_BOOL); Z1190("EdgeMode",edge_mode,1,TID_DWORD);
    Z1190("ResolutionPs",resolution_ps,1,TID_DWORD); Z1190("DeadTimeNs",dead_time_ns,1,TID_DWORD);
    Z1190("MaxHitsPerEvent",max_hits_per_event,1,TID_INT); Z1190("TdcHeaderEnabled",tdc_header_enabled,1,TID_BOOL);
    Z1190("EmptyEventEnabled",empty_event_enabled,1,TID_BOOL); Z1190("EventFifoEnabled",event_fifo_enabled,1,TID_BOOL);
    Z1190("ExtendedTriggerTimeEnabled",extended_trigger_time_enabled,1,TID_BOOL); Z1190("ChannelEnabled",channel_enabled,128,TID_BOOL);
#undef Z1190
    set_module_output(V1190_READBACK_PATH,"ErrorMask",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V1190_READBACK_PATH,"EffectiveFifoSizeWords",&zdword,sizeof(zdword),1,TID_DWORD);
    set_module_output(V1190_READBACK_PATH,"ControlRaw",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V1190_READBACK_PATH,"POUTSelection",&zword,sizeof(zword),1,TID_WORD);
    const char unknown_pout[]="Unknown";
    set_module_output(V1190_READBACK_PATH,"POUTFunction",unknown_pout,sizeof(unknown_pout),1,TID_STRING);
    set_module_output(V1190_READBACK_PATH,"AlmostFullLevelWords",&zword,sizeof(zword),1,TID_WORD);

    set_module_readback_valid(V775_READBACK_PATH,false);
    set_module_output(V775_READBACK_PATH,"FirmwareRevision",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V775_READBACK_PATH,"FullScaleRange",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V775_READBACK_PATH,"FastClearWindow",&zword,sizeof(zword),1,TID_WORD);
    const V775Settings d775={};
#define Z775(k,m,t) set_module_output(V775_READBACK_PATH,k,&d775.m,sizeof(d775.m),1,t)
    Z775("OverRangeEnabled",over_range_enabled,TID_BOOL); Z775("LowThresholdEnabled",low_threshold_enabled,TID_BOOL);
    Z775("CommonStop",common_stop,TID_BOOL); Z775("EmptyProgramEnabled",empty_program_enabled,TID_BOOL);
    Z775("ValidControlEnabled",valid_control_enabled,TID_BOOL); Z775("SlidingScaleEnabled",sliding_scale_enabled,TID_BOOL);
    Z775("AllTriggerEnabled",all_trigger_enabled,TID_BOOL);
#undef Z775
    set_module_output(V775_READBACK_PATH,"BitSet2Raw",&zword,sizeof(zword),1,TID_WORD);
    set_module_output(V775_READBACK_PATH,"Threshold",thresholds,sizeof(thresholds),32,TID_WORD);
    v792={}; v1190={}; v775={};
    v792.dirty=v1190.dirty=v775.dirty=true;
    publish_v7xx_variables("/Equipment/VME/Variables/V792",v792,v792_last_publish);
    publish_v1190_variables(v1190, v1190_last_publish);
    publish_v7xx_variables("/Equipment/VME/Variables/V775",v775,v775_last_publish);
}

//************************************//
// Write one V1720E readback field to ODB
//************************************//
static bool set_v1720e_output(const char *base, const char *name,
                              const void *value, INT size, INT count,
                              DWORD type)
{
    char path[256];
    return make_odb_path(path, sizeof(path), base, name) &&
           set_absolute_odb_value(path, value, size, count, type);
}

//************************************//
// Publish V1720E readback validity without changing other fields
//************************************//
void set_v1720e_readback_valid(bool valid)
{
    const BOOL value = valid ? TRUE : FALSE;
    set_v1720e_output(V1720E_READBACK_PATH, "Valid", &value, sizeof(value),
                      1, TID_BOOL);
}

//************************************//
// Publish the acquired V1720E register readback to ODB
//************************************//
void publish_v1720e_readback(const V1720E_CONFIG_READBACK &readback,
                                    bool valid)
{
    const DWORD record_length =
        readback.custom_size == V1720E_DEFAULT_CUSTOM_SIZE
            ? V1720E_DEFAULT_RECORD_SAMPLES : 0u;
    const BOOL software_trigger =
        (readback.trigger_source & V1720E_TRIGGER_SOFTWARE) != 0;
    const BOOL external_trigger =
        (readback.trigger_source & V1720E_TRIGGER_EXTERNAL) != 0;
    const BOOL zero_suppression =
        (readback.channel_config & V1720E_CHANNEL_CONFIG_ZS_MASK) != 0;
    const BOOL pack25 =
        (readback.channel_config & V1720E_CHANNEL_CONFIG_PACK25) != 0;
    BOOL self_trigger[V1720E_CHANNEL_COUNT] = {};
    BOOL channel_enabled[V1720E_CHANNEL_COUNT] = {};
    for (unsigned channel = 0; channel < V1720E_CHANNEL_COUNT; ++channel) {
        self_trigger[channel] =
            (readback.trigger_source & (1u << channel)) != 0;
        channel_enabled[channel] =
            (readback.channel_enable & (1u << channel)) != 0;
    }

#define SET_READBACK(name, value, count, type) \
    set_v1720e_output(V1720E_READBACK_PATH, name, &(value), sizeof(value), \
                      count, type)
    SET_READBACK("BoardInfo", readback.board_info, 1, TID_DWORD);
    SET_READBACK("RocFirmwareRevision", readback.roc_firmware, 1, TID_DWORD);
    SET_READBACK("BufferOrganization", readback.buffer_organization, 1, TID_DWORD);
    SET_READBACK("CustomSizeRaw", readback.custom_size, 1, TID_DWORD);
    SET_READBACK("RecordLengthSamples", record_length, 1, TID_DWORD);
    SET_READBACK("PostTrigger", readback.post_trigger, 1, TID_DWORD);
    SET_READBACK("SoftwareTriggerEnabled", software_trigger, 1, TID_BOOL);
    SET_READBACK("ExternalTriggerEnabled", external_trigger, 1, TID_BOOL);
    SET_READBACK("ChannelSelfTriggerEnabled", self_trigger,
                 V1720E_CHANNEL_COUNT, TID_BOOL);
    SET_READBACK("ChannelEnabled", channel_enabled,
                 V1720E_CHANNEL_COUNT, TID_BOOL);
    SET_READBACK("DCOffset", readback.dc_offset,
                 V1720E_CHANNEL_COUNT, TID_WORD);
    SET_READBACK("ZeroSuppressionEnabled", zero_suppression, 1, TID_BOOL);
    SET_READBACK("Pack25Enabled", pack25, 1, TID_BOOL);
    SET_READBACK("TriggerSourceRaw", readback.trigger_source, 1, TID_DWORD);
    SET_READBACK("ChannelEnableRaw", readback.channel_enable, 1, TID_DWORD);
    SET_READBACK("ChannelConfigRaw", readback.channel_config, 1, TID_DWORD);
#undef SET_READBACK
    set_v1720e_readback_valid(valid);
}

//************************************//
// Publish disabled RPV130 status fields
//************************************//
void publish_rpv130_disabled_state()
{
    const BOOL no = FALSE;
    const BYTE zero = 0;
    set_module_output(RPV130_VARIABLES_PATH, "EnabledForRun", &no,
                      sizeof(no), 1, TID_BOOL);
    set_module_output(RPV130_VARIABLES_PATH, "CommunicationOK", &no,
                      sizeof(no), 1, TID_BOOL);
    set_module_output(RPV130_VARIABLES_PATH, "Latch1", &zero,
                      sizeof(zero), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "Latch2", &zero,
                      sizeof(zero), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "RSFF", &zero,
                      sizeof(zero), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "Through", &zero,
                      sizeof(zero), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "CSR1", &zero,
                      sizeof(zero), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "CSR2", &zero,
                      sizeof(zero), 1, TID_BYTE);
}

//************************************//
// Publish acquired RPV130 status without reading hardware
//************************************//
void publish_rpv130_status(const RPV130_STATUS &status, BOOL communication_ok,
                           BOOL busy1, BOOL armed)
{
    set_module_output(RPV130_VARIABLES_PATH, "Latch1", &status.latch1,
                      sizeof(status.latch1), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "Latch2", &status.latch2,
                      sizeof(status.latch2), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "RSFF", &status.rsff,
                      sizeof(status.rsff), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "Through", &status.through,
                      sizeof(status.through), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "CSR1", &status.csr1,
                      sizeof(status.csr1), 1, TID_BYTE);
    set_module_output(RPV130_VARIABLES_PATH, "CSR2", &status.csr2,
                      sizeof(status.csr2), 1, TID_BYTE);
    // Keep the last hardware-derived BUSY1 value when CSR1 cannot be read.
    if (communication_ok)
        set_module_output(RPV130_STATUS_PATH, "Busy1", &busy1,
                          sizeof(busy1), 1, TID_BOOL);
    set_module_output(RPV130_STATUS_PATH, "SingleEventBusyArmed", &armed,
                      sizeof(armed), 1, TID_BOOL);
    set_module_output(RPV130_VARIABLES_PATH, "CommunicationOK",
                      &communication_ok, sizeof(communication_ok), 1,
                      TID_BOOL);
    const BOOL enabled_for_run = TRUE;
    set_module_output(RPV130_VARIABLES_PATH, "EnabledForRun",
                      &enabled_for_run, sizeof(enabled_for_run), 1,
                      TID_BOOL);
}

//************************************//
// Publish the RPV130 BUSY and armed state
//************************************//
bool publish_rpv130_busy_state(bool busy, bool armed)
{
    const BOOL b = busy ? TRUE : FALSE;
    const BOOL a = armed ? TRUE : FALSE;
    return set_module_output(RPV130_STATUS_PATH, "Busy1", &b,
                             sizeof(b), 1, TID_BOOL) &&
           set_module_output(RPV130_STATUS_PATH, "SingleEventBusyArmed", &a,
                             sizeof(a), 1, TID_BOOL);
}

//************************************//
// Publish one RPV130 armed status update
//************************************//
void publish_rpv130_armed(bool armed)
{
    const BOOL value = armed ? TRUE : FALSE;
    set_module_output(RPV130_STATUS_PATH, "SingleEventBusyArmed", &value,
                      sizeof(value), 1, TID_BOOL);
}

//************************************//
// Publish the RPV130 run enable flag
//************************************//
void publish_rpv130_enabled_for_run(bool enabled)
{
    const BOOL value = enabled ? TRUE : FALSE;
    set_module_output(RPV130_VARIABLES_PATH, "EnabledForRun", &value,
                      sizeof(value), 1, TID_BOOL);
}

//************************************//
// Write one manual buffer clear status string to ODB
//************************************//
static bool set_buffer_clear_string(const char *name, const std::string &value)
{
    char path[256];
    if (!make_odb_path(path, sizeof(path), BUFFER_CLEAR_STATUS_PATH, name))
        return false;
    char buffer[256] = {};
    snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    return set_absolute_odb_value(path, buffer, sizeof(buffer), 1, TID_STRING);
}

//************************************//
// Publish manual buffer clear status and module results
//************************************//
bool publish_buffer_clear_status(const daq::BufferClearStatus &status,
                                 const VmeBufferClearResults &results)
{
    char path[256];
    bool ok = true;
    const DWORD active = status.active_request_id;
    const DWORD handled = status.last_handled_request_id;
    const DWORD successful = status.last_successful_request_id;
    const BOOL in_progress = status.in_progress ? TRUE : FALSE;
    const BOOL last_succeeded =
        status.last_attempt_succeeded ? TRUE : FALSE;
#define PUBLISH_CLEAR_VALUE(name, value, type) \
    do { \
        ok = make_odb_path(path, sizeof(path), BUFFER_CLEAR_STATUS_PATH, name) && \
             set_absolute_odb_value(path, &(value), sizeof(value), 1, type) && ok; \
    } while (0)
    PUBLISH_CLEAR_VALUE("ActiveRequestId", active, TID_DWORD);
    PUBLISH_CLEAR_VALUE("LastHandledRequestId", handled, TID_DWORD);
    PUBLISH_CLEAR_VALUE("LastSuccessfulRequestId", successful, TID_DWORD);
    PUBLISH_CLEAR_VALUE("InProgress", in_progress, TID_BOOL);
    PUBLISH_CLEAR_VALUE("LastAttemptSucceeded", last_succeeded, TID_BOOL);
    PUBLISH_CLEAR_VALUE("LastClearUnixTime",
                        status.last_clear_unix_time, TID_QWORD);
#undef PUBLISH_CLEAR_VALUE
    ok = set_buffer_clear_string(
             "State", daq::bufferClearStateName(status.state)) && ok;
    ok = set_buffer_clear_string("LastError", status.last_error) && ok;
    ok = set_buffer_clear_string("V792Result", results.v792) && ok;
    ok = set_buffer_clear_string("V1190Result", results.v1190) && ok;
    ok = set_buffer_clear_string("V775Result", results.v775) && ok;
    ok = set_buffer_clear_string("V1720EResult", results.v1720e) && ok;
    return ok;
}

//************************************//
// Create manual buffer clear command and status fields
//************************************//
bool ensure_buffer_clear_schema()
{
    const DWORD zero = 0;
    const BOOL no = FALSE;
    const uint64_t zero_time = 0;
    char path[256];
    char idle[32] = "Idle";
    char empty[256] = {};
#define ENSURE_CLEAR_VALUE(name, value, count, type) \
    do { \
        if (!make_odb_path(path, sizeof(path), BUFFER_CLEAR_STATUS_PATH, name) || \
            !ensure_odb_value(path, &(value), sizeof(value), count, type)) \
            return false; \
    } while (0)
    if (!ensure_odb_value(BUFFER_CLEAR_COMMAND_PATH, &zero, sizeof(zero), 1,
                          TID_DWORD))
        return false;
    ENSURE_CLEAR_VALUE("ActiveRequestId", zero, 1, TID_DWORD);
    ENSURE_CLEAR_VALUE("LastHandledRequestId", zero, 1, TID_DWORD);
    ENSURE_CLEAR_VALUE("LastSuccessfulRequestId", zero, 1, TID_DWORD);
    ENSURE_CLEAR_VALUE("InProgress", no, 1, TID_BOOL);
    ENSURE_CLEAR_VALUE("LastAttemptSucceeded", no, 1, TID_BOOL);
    ENSURE_CLEAR_VALUE("LastClearUnixTime", zero_time, 1, TID_QWORD);
    ENSURE_CLEAR_VALUE("State", idle, 1, TID_STRING);
    ENSURE_CLEAR_VALUE("LastError", empty, 1, TID_STRING);
    ENSURE_CLEAR_VALUE("V792Result", empty, 1, TID_STRING);
    ENSURE_CLEAR_VALUE("V1190Result", empty, 1, TID_STRING);
    ENSURE_CLEAR_VALUE("V775Result", empty, 1, TID_STRING);
    ENSURE_CLEAR_VALUE("V1720EResult", empty, 1, TID_STRING);
#undef ENSURE_CLEAR_VALUE

    return true;
}

}  // namespace vme_odb
