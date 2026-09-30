#include "v1190_config.h"
#include "v1190_fifo_blt32.h"

#include <cstdio>
#include <cstring>

extern const char *frontend_name;

namespace v1190_config {

static const DWORD V1190_BASE = 0x00C10000;
static const char *V1190_READBACK_PATH = "/Equipment/VME/Readback/V1190";
/* V1190 regular registers and normal-run Control bits. */
static const DWORD V1190_CONTROL = 0x1000;
static const DWORD V1190_STATUS = 0x1002;
static const DWORD V1190_SOFT_CLEAR = 0x1016;
static const DWORD V1190_SOFT_TRIGGER = 0x101A;
static const DWORD V1190_EVENT_COUNTER = 0x101C;
static const DWORD V1190_EVENT_STORED = 0x1020;
static const DWORD V1190_ALMOST_FULL_LEVEL = 0x1022;
static const DWORD V1190_FIRMWARE_REVISION = 0x1026;
static const DWORD V1190_OUT_PROG = 0x102C;
static const WORD V1190_POUT_ALMOST_FULL = 2;
static const DWORD V1190_MICRO_DATA = 0x102E;
static const DWORD V1190_MICRO_HANDSHAKE = 0x1030;
static const DWORD V1190_EVENT_FIFO_STATUS = V1190_FIFO_STATUS_OFFSET;
static const DWORD V1190_EVENT_FIFO_STORED = V1190_FIFO_STORED_OFFSET;
static const WORD V1190_CONTROL_EMPTY_EVENT = 0x0008;
static const WORD V1190_CONTROL_EVENT_FIFO = 0x0100;
static const WORD V1190_CONTROL_EXT_TRIGGER_TIME = 0x0200;
static const WORD V1190_STATUS_DATA_READY = 0x0001;
static const WORD V1190_STATUS_ALMOST_FULL = 0x0002;
static const WORD V1190_STATUS_FULL = 0x0004;
static const WORD V1190_STATUS_TRIGGER_MATCH = 0x0008;
static const WORD V1190_FIFO_STATUS_DATA_READY = 0x0001;
static const WORD V1190_MICRO_WRITE_OK = 0x0001;
static const WORD V1190_MICRO_READ_OK = 0x0002;

/* V1190 microcontroller opcodes and operands from the V1190 manual. */
static const WORD V1190_OPCODE_TRIGGER_MATCH = 0x0000;
static const WORD V1190_OPCODE_CONTINUOUS = 0x0100;
static const WORD V1190_OPCODE_READ_ACQ_MODE = 0x0200;
static const WORD V1190_OPCODE_SET_WINDOW_WIDTH = 0x1000;
static const WORD V1190_OPCODE_SET_WINDOW_OFFSET = 0x1100;
static const WORD V1190_OPCODE_SET_EXTRA_MARGIN = 0x1200;
static const WORD V1190_OPCODE_SET_REJECT_MARGIN = 0x1300;
static const WORD V1190_OPCODE_DISABLE_TRIGGER_SUBTRACTION = 0x1500;
static const WORD V1190_OPCODE_ENABLE_TRIGGER_SUBTRACTION = 0x1400;
static const WORD V1190_OPCODE_READ_TRIGGER_CONFIG = 0x1600;
static const WORD V1190_OPCODE_SET_EDGE_MODE = 0x2200;
static const WORD V1190_OPCODE_READ_EDGE_MODE = 0x2300;
static const WORD V1190_OPCODE_SET_RESOLUTION = 0x2400;
static const WORD V1190_OPCODE_READ_RESOLUTION = 0x2600;
static const WORD V1190_OPCODE_SET_DEAD_TIME = 0x2800;
static const WORD V1190_OPCODE_READ_DEAD_TIME = 0x2900;
static const WORD V1190_OPCODE_ENABLE_TDC_HEADER = 0x3000;
static const WORD V1190_OPCODE_DISABLE_TDC_HEADER = 0x3100;
static const WORD V1190_OPCODE_READ_TDC_HEADER = 0x3200;
static const WORD V1190_OPCODE_SET_MAX_HITS = 0x3300;
static const WORD V1190_OPCODE_READ_MAX_HITS = 0x3400;
static const WORD V1190_OPCODE_READ_ERROR_MASK = 0x3A00;
static const WORD V1190_OPCODE_READ_FIFO_SIZE = 0x3C00;
static const WORD V1190_OPCODE_ENABLE_ALL_CHANNELS = 0x4200;
static const WORD V1190_OPCODE_WRITE_CHANNEL_MASK = 0x4400;
static const WORD V1190_OPCODE_READ_CHANNEL_MASK = 0x4500;
static const DWORD V1190_CONFIGURATION_ROM_VERSION = 0x4030;
static const WORD V1190_RUN_WINDOW_WIDTH = 12;
static const WORD V1190_RUN_WINDOW_OFFSET = 0xFFF4; // signed -12 counts
static const WORD V1190_RUN_EXTRA_MARGIN = 8;
static const WORD V1190_RUN_REJECT_MARGIN = 4;
static const WORD V1190_RUN_EDGE_MODE = 3;
static const WORD V1190_RUN_RESOLUTION = 2; // 100 ps
static const WORD V1190_RUN_DEAD_TIME = 0;  // approximately 5 ns
static const WORD V1190_RUN_MAX_HITS = 9;   // unlimited
static const size_t V1190_CHANNEL_MASK_WORDS = 8;




//************************************//
// Build the existing V1190 default settings
//************************************//
V1190Settings default_settings()
{
    V1190Settings s = {};
    s.enabled = TRUE;
    s.trigger_matching_enabled = TRUE;
    s.window_width = V1190_RUN_WINDOW_WIDTH;
    s.window_offset = -12;
    s.extra_search_margin = V1190_RUN_EXTRA_MARGIN;
    s.reject_margin = V1190_RUN_REJECT_MARGIN;
    s.trigger_subtraction_enabled = FALSE;
    s.edge_mode = V1190_RUN_EDGE_MODE;
    s.resolution_ps = 100;
    s.dead_time_ns = 5;
    s.max_hits_per_event = -1;
    s.tdc_header_enabled = TRUE;
    s.empty_event_enabled = TRUE;
    s.event_fifo_enabled = FALSE;
    s.extended_trigger_time_enabled = FALSE;
    for (unsigned i = 0; i < 128; ++i)
        s.channel_enabled[i] = TRUE;
    return s;
}

//************************************//
// Read V1190 registers and microcontroller configuration
//************************************//
bool read_configuration(const Access &access, V1190Configuration &c)
{
    return access.read_acquisition_mode(c.mode) &&
           access.micro_read_command(V1190_OPCODE_READ_TRIGGER_CONFIG, c.trigger, 5) &&
           access.micro_read_command(V1190_OPCODE_READ_EDGE_MODE, &c.edge, 1) &&
           access.micro_read_command(V1190_OPCODE_READ_RESOLUTION, &c.resolution, 1) &&
           access.micro_read_command(V1190_OPCODE_READ_DEAD_TIME, &c.dead_time, 1) &&
           access.micro_read_command(V1190_OPCODE_READ_TDC_HEADER, &c.header, 1) &&
           access.micro_read_command(V1190_OPCODE_READ_MAX_HITS, &c.max_hits, 1) &&
           access.micro_read_command(V1190_OPCODE_READ_ERROR_MASK, &c.error_mask, 1) &&
           access.micro_read_command(V1190_OPCODE_READ_FIFO_SIZE, &c.fifo_size, 1) &&
           access.micro_read_command(V1190_OPCODE_READ_CHANNEL_MASK, c.channels,
                                    V1190_CHANNEL_MASK_WORDS) &&
           access.read16(V1190_BASE + V1190_CONTROL, c.control, "V1190 Control") &&
           access.read16(V1190_BASE + V1190_STATUS, c.status, "V1190 Status") &&
           access.read16(V1190_BASE + V1190_OUT_PROG, c.pout_selection,
                      "V1190 POUT selection") &&
           access.read16(V1190_BASE + V1190_ALMOST_FULL_LEVEL,
                      c.almost_full_level_words, "V1190 Almost Full Level") &&
           access.read16(V1190_BASE + V1190_FIRMWARE_REVISION, c.firmware,
                      "V1190 Firmware Revision") &&
           access.read16(V1190_BASE + V1190_CONFIGURATION_ROM_VERSION,
                      c.rom_version, "V1190 Configuration ROM Version");
}

static int decode_signed_12(WORD value)
{
    int result=value&0x0FFF;
    return (result&0x0800)?result-0x1000:result;
}

static const char *v1190_pout_function(WORD selection)
{
    static const char *const names[] = {
        "DATA_READY", "FULL", "ALMOST_FULL", "ERROR"
    };
    return selection < 4 ? names[selection] : "UNKNOWN";
}

//************************************//
// Configure and verify the startup POUT selection
//************************************//
bool configure_pout_startup(const Access &access, bool enabled)
{
    if (!enabled) return true;
    if (!access.write16(V1190_BASE + V1190_OUT_PROG,
                     V1190_POUT_ALMOST_FULL, "V1190 startup POUT ALMOST_FULL"))
        return false;

    WORD actual = 0;
    if (!access.read16(V1190_BASE + V1190_OUT_PROG, actual,
                    "V1190 startup POUT readback"))
        return false;
    if (actual != V1190_POUT_ALMOST_FULL) {
        cm_msg(MERROR, frontend_name,
               "V1190 startup POUT mismatch: expected ALMOST_FULL (0x%04X), actual %s (0x%04X)",
               V1190_POUT_ALMOST_FULL, v1190_pout_function(actual), actual);
        return false;
    }

    WORD almost_full_level = 0;
    if (!access.read16(V1190_BASE + V1190_ALMOST_FULL_LEVEL,
                    almost_full_level, "V1190 startup Almost Full Level"))
        return false;
    const char *function = v1190_pout_function(actual);
    if (!vme_odb::set_module_output(V1190_READBACK_PATH, "POUTSelection", &actual,
                           sizeof(actual), 1, TID_WORD) ||
        !vme_odb::set_module_output(V1190_READBACK_PATH, "POUTFunction", function,
                           strlen(function) + 1, 1, TID_STRING) ||
        !vme_odb::set_module_output(V1190_READBACK_PATH, "AlmostFullLevelWords",
                           &almost_full_level, sizeof(almost_full_level),
                           1, TID_WORD)) {
        cm_msg(MERROR, frontend_name,
               "Cannot publish V1190 startup POUT readback");
        return false;
    }
    cm_msg(MINFO, frontend_name,
           "V1190 startup POUT expected=ALMOST_FULL (0x%04X) actual=%s (0x%04X); Almost Full Level=%u words",
           V1190_POUT_ALMOST_FULL, function, actual, almost_full_level);
    return true;
}

//************************************//
// Validate and encode requested V1190 settings
//************************************//
bool encode_semantics(const V1190Settings &s, WORD &resolution,
                                   WORD &dead_time, WORD &max_hits,
                                   WORD (&channels)[V1190_CHANNEL_MASK_WORDS])
{
    if (s.window_width>0x0FFF || s.window_offset < -2048 || s.window_offset > 2047 ||
        s.extra_search_margin>0x0FFF || s.reject_margin>0x0FFF || s.edge_mode<1 || s.edge_mode>3) {
        cm_msg(MERROR,frontend_name,"Invalid V1190 trigger window or EdgeMode setting"); return false;
    }
    if (s.resolution_ps==800) resolution=0;
    else if (s.resolution_ps==200) resolution=1;
    else if (s.resolution_ps==100) resolution=2;
    else { cm_msg(MERROR,frontend_name,"V1190 ResolutionPs must be 800, 200, or 100"); return false; }
    if (s.dead_time_ns==5) dead_time=0;
    else if (s.dead_time_ns==10) dead_time=1;
    else if (s.dead_time_ns==30) dead_time=2;
    else if (s.dead_time_ns==100) dead_time=3;
    else { cm_msg(MERROR,frontend_name,"V1190 DeadTimeNs must be 5, 10, 30, or 100"); return false; }
    if (s.max_hits_per_event==-1) max_hits=9;
    else if (s.max_hits_per_event==0) max_hits=0;
    else {
        const int values[]={1,2,4,8,16,32,64,128}; max_hits=0xFFFF;
        for (unsigned i=0;i<8;++i) if (s.max_hits_per_event==values[i]) max_hits=i+1;
        if (max_hits==0xFFFF) { cm_msg(MERROR,frontend_name,"V1190 MaxHitsPerEvent must be -1, 0, or a power of two from 1 through 128"); return false; }
    }
    memset(channels,0,sizeof(channels));
    for (unsigned i=0;i<128;++i) if (s.channel_enabled[i]) channels[i/16]|=WORD(1u<<(i%16));
    return true;
}

static DWORD resolution_ps_from_code(WORD code) { const DWORD v[]={800,200,100,0}; return v[code&3]; }

static DWORD dead_time_ns_from_code(WORD code) { const DWORD v[]={5,10,30,100}; return v[code&3]; }

static INT max_hits_from_code(WORD code) { code&=0xF; if(code==9)return -1; if(code==0)return 0; return code<=8?INT(1u<<(code-1)):-2; }

//************************************//
// Apply V1190 Control bits for the selected readout mode
//************************************//
bool configure_control_for_run(const Access &access, const V1190Settings &settings,
                                      bool fifo_blt32, WORD current)
{
    const WORD mask = V1190_CONTROL_EMPTY_EVENT |
                      V1190_CONTROL_EVENT_FIFO |
                      V1190_CONTROL_EXT_TRIGGER_TIME;
    WORD requested = 0;
    if (settings.empty_event_enabled)
        requested |= V1190_CONTROL_EMPTY_EVENT;
    /* The source-selected BLT mode requires a FIFO entry for each event. */
    if (settings.event_fifo_enabled ||
        fifo_blt32)
        requested |= V1190_CONTROL_EVENT_FIFO;
    if (settings.extended_trigger_time_enabled)
        requested |= V1190_CONTROL_EXT_TRIGGER_TIME;
    const WORD control = WORD((current & ~mask) | requested);
    if (!access.write16(V1190_BASE + V1190_CONTROL, control,
                     "V1190 Control run settings")) return false;
    if (fifo_blt32) {
        WORD readback = 0;
        if (!access.read16(V1190_BASE + V1190_CONTROL, readback,
                        "V1190 Event FIFO enable readback")) return false;
        if (!(readback & V1190_CONTROL_EVENT_FIFO)) {
            cm_msg(MERROR, frontend_name,
                   "V1190 Event FIFO enable readback failed: Control=0x%04X",
                   readback);
            return false;
        }
    }
    return true;
}

//************************************//
// Apply the requested V1190 BOR configuration
//************************************//
bool configure_for_run(const Access &access,
                       const V1190Settings &settings, bool fifo_blt32)
{
    if (!settings.enabled) return true;
    const WORD width=settings.window_width;
    const WORD offset=WORD(settings.window_offset)&0x0FFF;
    const WORD extra=settings.extra_search_margin, reject=settings.reject_margin;
    const WORD edge=settings.edge_mode; WORD resolution=0,dead=0,hits=0;
    WORD channels[V1190_CHANNEL_MASK_WORDS]={};
    if(!encode_semantics(settings,resolution,dead,hits,channels)) return false;
    WORD control = 0;
    if (!access.micro_write_command(settings.trigger_matching_enabled?V1190_OPCODE_TRIGGER_MATCH:V1190_OPCODE_CONTINUOUS,NULL,0) ||
        !access.micro_write_command(V1190_OPCODE_SET_WINDOW_WIDTH, &width, 1) ||
        !access.micro_write_command(V1190_OPCODE_SET_WINDOW_OFFSET, &offset, 1) ||
        !access.micro_write_command(V1190_OPCODE_SET_EXTRA_MARGIN, &extra, 1) ||
        !access.micro_write_command(V1190_OPCODE_SET_REJECT_MARGIN, &reject, 1) ||
        !access.micro_write_command(settings.trigger_subtraction_enabled?V1190_OPCODE_ENABLE_TRIGGER_SUBTRACTION:V1190_OPCODE_DISABLE_TRIGGER_SUBTRACTION,NULL,0) ||
        !access.micro_write_command(V1190_OPCODE_SET_EDGE_MODE, &edge, 1) ||
        !access.micro_write_command(V1190_OPCODE_SET_RESOLUTION, &resolution, 1) ||
        !access.micro_write_command(V1190_OPCODE_SET_DEAD_TIME, &dead, 1) ||
        !access.micro_write_command(V1190_OPCODE_SET_MAX_HITS, &hits, 1) ||
        !access.micro_write_command(V1190_OPCODE_WRITE_CHANNEL_MASK,channels,V1190_CHANNEL_MASK_WORDS) ||
        !access.micro_write_command(settings.tdc_header_enabled?V1190_OPCODE_ENABLE_TDC_HEADER:V1190_OPCODE_DISABLE_TDC_HEADER,NULL,0) ||
        !access.read16(V1190_BASE + V1190_CONTROL, control, "V1190 Control RMW read")) return false;
    return configure_control_for_run(access, settings, fifo_blt32, control);
}

//************************************//
// Report a V1190 configuration readback mismatch
//************************************//
static bool verify_value(const char *module, const char *item,
                         unsigned expected, unsigned actual)
{
    if (expected == actual) return true;
    cm_msg(MERROR, frontend_name,
           "%s configuration verify failed: %s expected 0x%X, read back 0x%X",
           module, item, expected, actual);
    return false;
}

//************************************//
// Verify V1190 BOR settings and publish readback
//************************************//
bool verify_configuration(const Access &access, const V1190Settings &settings,
                          bool fifo_blt32, V1190ReadbackSnapshot &readback)
{
    if(!settings.enabled) { vme_odb::set_module_readback_valid(V1190_READBACK_PATH,false); return true; }
    V1190Configuration c = {};
    if (!read_configuration(access, c)) return false;
    WORD er=0,ed=0,eh=0,channels[V1190_CHANNEL_MASK_WORDS]={};
    if(!encode_semantics(settings,er,ed,eh,channels)) return false;
    bool ok = true;
#define V1190_VERIFY(item, expected, actual) \
    do { ok = verify_value("V1190", item, expected, actual) && ok; } while (0)
    V1190_VERIFY("Acquisition mode",settings.trigger_matching_enabled,c.mode&1);
    V1190_VERIFY("Window width",settings.window_width,c.trigger[0]&0xFFF);
    V1190_VERIFY("Window offset",WORD(settings.window_offset)&0xFFF,c.trigger[1]&0xFFF);
    V1190_VERIFY("Extra search margin",settings.extra_search_margin,c.trigger[2]&0xFFF);
    V1190_VERIFY("Reject margin",settings.reject_margin,c.trigger[3]&0xFFF);
    V1190_VERIFY("Trigger subtraction",settings.trigger_subtraction_enabled,c.trigger[4]&1);
    V1190_VERIFY("Edge mode",settings.edge_mode,c.edge&3);
    V1190_VERIFY("Resolution",er,c.resolution&3); V1190_VERIFY("Dead time",ed,c.dead_time&3);
    V1190_VERIFY("Maximum hits",eh,c.max_hits&0xF); V1190_VERIFY("TDC Header/Trailer",settings.tdc_header_enabled,c.header&1);
    for (size_t i = 0; i < V1190_CHANNEL_MASK_WORDS; ++i)
        V1190_VERIFY("Channel mask word",channels[i],c.channels[i]);
    V1190_VERIFY("Empty Event",settings.empty_event_enabled,!!(c.control&V1190_CONTROL_EMPTY_EVENT));
    V1190_VERIFY("Event FIFO",
                 settings.event_fifo_enabled ||
                     fifo_blt32,
                 !!(c.control&V1190_CONTROL_EVENT_FIFO));
    V1190_VERIFY("Extended Trigger Time Tag",settings.extended_trigger_time_enabled,
                 !!(c.control & V1190_CONTROL_EXT_TRIGGER_TIME));
    V1190_VERIFY("POUT selection", V1190_POUT_ALMOST_FULL,
                 c.pout_selection);
    cm_msg(MINFO, frontend_name,
           "V1190 POUT expected=ALMOST_FULL (%u) actual=%s (0x%04X); Almost Full Level=%u words",
           V1190_POUT_ALMOST_FULL,
           v1190_pout_function(c.pout_selection),
           c.pout_selection, c.almost_full_level_words);
#undef V1190_VERIFY
    V1190Settings rb={}; rb.trigger_matching_enabled=!!(c.mode&1); rb.window_width=c.trigger[0]&0xFFF;
    rb.window_offset=decode_signed_12(c.trigger[1]); rb.extra_search_margin=c.trigger[2]&0xFFF; rb.reject_margin=c.trigger[3]&0xFFF;
    rb.trigger_subtraction_enabled=!!(c.trigger[4]&1); rb.edge_mode=c.edge&3; rb.resolution_ps=resolution_ps_from_code(c.resolution);
    rb.dead_time_ns=dead_time_ns_from_code(c.dead_time); rb.max_hits_per_event=max_hits_from_code(c.max_hits); rb.tdc_header_enabled=!!(c.header&1);
    rb.empty_event_enabled=!!(c.control&V1190_CONTROL_EMPTY_EVENT); rb.event_fifo_enabled=!!(c.control&V1190_CONTROL_EVENT_FIFO); rb.extended_trigger_time_enabled=!!(c.control&V1190_CONTROL_EXT_TRIGGER_TIME);
    for(unsigned i=0;i<128;++i) rb.channel_enabled[i]=!!(c.channels[i/16]&(1u<<(i%16)));
    const char *board=(c.rom_version&0xFF)==0?"V1190A":((c.rom_version&0xFF)==1?"V1190B":"Unknown");
    readback.valid = ok ? TRUE : FALSE;
    readback.firmware_revision = c.firmware;
    readback.configuration_rom_version = c.rom_version;
    snprintf(readback.board_type,
             sizeof(readback.board_type), "%s", board);
    readback.settings = rb;
    readback.error_mask = c.error_mask & 0x7FF;
    readback.effective_fifo_size_words =
        (c.fifo_size & 0xF) <= 7 ? (1u << ((c.fifo_size & 0xF) + 1)) : 0;
    readback.control_raw = c.control;
    readback.pout_selection = c.pout_selection;
    snprintf(readback.pout_function,
             sizeof(readback.pout_function), "%s",
             v1190_pout_function(c.pout_selection));
    readback.almost_full_level_words =
        c.almost_full_level_words;
#define P1190(k,m,cnt,t) vme_odb::set_module_output(V1190_READBACK_PATH,k,&rb.m,sizeof(rb.m),cnt,t)
    vme_odb::set_module_output(V1190_READBACK_PATH,"FirmwareRevision",&c.firmware,sizeof(c.firmware),1,TID_WORD); vme_odb::set_module_output(V1190_READBACK_PATH,"ConfigurationRomVersion",&c.rom_version,sizeof(c.rom_version),1,TID_WORD); vme_odb::set_module_output(V1190_READBACK_PATH,"BoardType",board,strlen(board)+1,1,TID_STRING);
    P1190("TriggerMatchingEnabled",trigger_matching_enabled,1,TID_BOOL); P1190("WindowWidth",window_width,1,TID_DWORD); P1190("WindowOffset",window_offset,1,TID_INT); P1190("ExtraSearchMargin",extra_search_margin,1,TID_DWORD); P1190("RejectMargin",reject_margin,1,TID_DWORD); P1190("TriggerSubtractionEnabled",trigger_subtraction_enabled,1,TID_BOOL); P1190("EdgeMode",edge_mode,1,TID_DWORD); P1190("ResolutionPs",resolution_ps,1,TID_DWORD); P1190("DeadTimeNs",dead_time_ns,1,TID_DWORD); P1190("MaxHitsPerEvent",max_hits_per_event,1,TID_INT); P1190("TdcHeaderEnabled",tdc_header_enabled,1,TID_BOOL); P1190("EmptyEventEnabled",empty_event_enabled,1,TID_BOOL); P1190("EventFifoEnabled",event_fifo_enabled,1,TID_BOOL); P1190("ExtendedTriggerTimeEnabled",extended_trigger_time_enabled,1,TID_BOOL); P1190("ChannelEnabled",channel_enabled,128,TID_BOOL);
#undef P1190
    WORD error=c.error_mask&0x7FF; DWORD fifo=(c.fifo_size&0xF)<=7?(1u<<((c.fifo_size&0xF)+1)):0;
    vme_odb::set_module_output(V1190_READBACK_PATH,"ErrorMask",&error,sizeof(error),1,TID_WORD); vme_odb::set_module_output(V1190_READBACK_PATH,"EffectiveFifoSizeWords",&fifo,sizeof(fifo),1,TID_DWORD); vme_odb::set_module_output(V1190_READBACK_PATH,"ControlRaw",&c.control,sizeof(c.control),1,TID_WORD);
    vme_odb::set_module_output(V1190_READBACK_PATH,"POUTSelection",&c.pout_selection,sizeof(c.pout_selection),1,TID_WORD);
    const char *pout_function = v1190_pout_function(c.pout_selection);
    vme_odb::set_module_output(V1190_READBACK_PATH,"POUTFunction",pout_function,strlen(pout_function)+1,1,TID_STRING);
    vme_odb::set_module_output(V1190_READBACK_PATH,"AlmostFullLevelWords",&c.almost_full_level_words,sizeof(c.almost_full_level_words),1,TID_WORD);
    vme_odb::set_module_readback_valid(V1190_READBACK_PATH,ok);
    return ok;
}

//************************************//
// Restore saved V1190 diagnostic settings
//************************************//
bool restore_diagnostic_settings(const Access &access, DiagnosticState &state)
{
    if (!state.saved)
        return true;

    bool ok = true;
    if (state.empty_event_may_have_changed) {
        WORD control = 0;
        if (!access.read16(V1190_BASE + V1190_CONTROL, control, "V1190 Control restore read")) {
            ok = false;
        } else {
            const WORD restored = static_cast<WORD>(
                (control & ~V1190_CONTROL_EMPTY_EVENT) |
                (state.saved_control & V1190_CONTROL_EMPTY_EVENT));
            if (!access.write16(V1190_BASE + V1190_CONTROL, restored,
                             "V1190 Empty Event restore")) {
                ok = false;
            } else if (!access.read16(V1190_BASE + V1190_CONTROL, control,
                                   "V1190 Control restore verify") ||
                       (control & V1190_CONTROL_EMPTY_EVENT) !=
                           (state.saved_control & V1190_CONTROL_EMPTY_EVENT)) {
                cm_msg(MERROR, frontend_name, "V1190 Empty Event restoration verification failed");
                ok = false;
            } else {
                cm_msg(MINFO, frontend_name, "V1190 Empty Event bit restored (Control 0x%04X)", control);
                state.empty_event_may_have_changed = false;
            }
        }
    }

    // Mode restoration is attempted even if Control restoration failed.
    if (state.mode_may_have_changed) {
        WORD mode = 0;
        WORD status = 0;
        if (!access.micro_write_opcode(V1190_OPCODE_CONTINUOUS) ||
            !access.read_acquisition_mode(mode) ||
            !access.read16(V1190_BASE + V1190_STATUS, status,
                        "V1190 Status restore verify") ||
            (mode & 1) != 0 || (status & V1190_STATUS_TRIGGER_MATCH) != 0) {
            cm_msg(MERROR, frontend_name, "V1190 Continuous Storage restoration verification failed");
            ok = false;
        } else {
            cm_msg(MINFO, frontend_name, "V1190 Continuous Storage restored and verified");
            state.mode_may_have_changed = false;
        }
    }

    if (!state.empty_event_may_have_changed && !state.mode_may_have_changed)
        state.saved = false;
    return ok;
}

//************************************//
// Set up the V1190 soft trigger diagnostic
//************************************//
bool setup_soft_trigger_test(const Access &access, DiagnosticState &state)
{
    WORD mode = 0;
    WORD status = 0;
    WORD events_stored = 0;
    if (!access.read_acquisition_mode(mode) ||
        !access.read16(V1190_BASE + V1190_CONTROL, state.saved_control,
                    "V1190 Control save") ||
        !access.read16(V1190_BASE + V1190_STATUS, status, "V1190 Status") ||
        !access.read16(V1190_BASE + V1190_EVENT_STORED, events_stored,
                    "V1190 Event Stored"))
        return false;

    state.saved = true;
    if (!!(mode & 1) != !!(status & V1190_STATUS_TRIGGER_MATCH)) {
        cm_msg(MERROR, frontend_name,
               "V1190 acquisition mode/status mismatch before soft-trigger test");
        return false;
    }
    if ((status & V1190_STATUS_DATA_READY) != 0 || events_stored != 0) {
        cm_msg(MERROR, frontend_name,
               "V1190 soft-trigger test requires empty FIFO (Status 0x%04X, Event Stored %u)",
               status, events_stored);
        return false;
    }

    if ((mode & 1) == 0) {
        state.mode_may_have_changed = true; // Include an ambiguous opcode-write failure.
        if (!access.micro_write_opcode(V1190_OPCODE_TRIGGER_MATCH) ||
            !access.read_acquisition_mode(mode) ||
            !access.read16(V1190_BASE + V1190_STATUS, status, "V1190 Status") ||
            (mode & 1) == 0 || (status & V1190_STATUS_TRIGGER_MATCH) == 0) {
            cm_msg(MERROR, frontend_name, "V1190 Trigger Matching setup verification failed");
            restore_diagnostic_settings(access, state);
            return false;
        }
    }

    WORD control = 0;
    if (!access.read16(V1190_BASE + V1190_CONTROL, control, "V1190 Control RMW read")) {
        restore_diagnostic_settings(access, state);
        return false;
    }
    state.empty_event_may_have_changed = true; // Include an ambiguous write failure.
    const WORD temporary = static_cast<WORD>(control | V1190_CONTROL_EMPTY_EVENT);
    WORD readback = 0;
    if (!access.write16(V1190_BASE + V1190_CONTROL, temporary,
                     "V1190 Empty Event enable") ||
        !access.read16(V1190_BASE + V1190_CONTROL, readback,
                    "V1190 Control enable verify") ||
        (readback & V1190_CONTROL_EMPTY_EVENT) == 0 ||
        (readback & ~V1190_CONTROL_EMPTY_EVENT) !=
            (control & ~V1190_CONTROL_EMPTY_EVENT)) {
        cm_msg(MERROR, frontend_name, "V1190 Empty Event setup verification failed");
        restore_diagnostic_settings(access, state);
        return false;
    }

    if (!access.write16(V1190_BASE + V1190_SOFT_TRIGGER, 0,
                     "V1190 Soft Trigger")) {
        restore_diagnostic_settings(access, state);
        return false;
    }
    cm_msg(MINFO, frontend_name,
           "V1190 soft-trigger test armed: Trigger Matching, Empty Event, one Soft Trigger");
    return true;
}

}  // namespace v1190_config
