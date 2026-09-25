#include "out0_diagnostic.h"

#include <CAENVMElib.h>

#include <iomanip>
#include <sstream>

#include "midas.h"

namespace {

// V3718 User Manual rev. 4, Internal Registers (16-bit), pp. 28-35.
// The legacy CVRegisters enumerators in CAENVMEtypes.h describe older bridges.
enum V3718Register {
  kStatus = 0x00,
  kIoLevel = 0x07,
  kIoPolarity = 0x08,
  kOutMux = 0x09,
  kIoStatusRead = 0x0B,
  kIoStatusSet = 0x0C,
  kPulseASetup = 0x10,
  kPulseBSetup = 0x17,
};

const char* mux_source(unsigned value) {
  switch (value) {
    case 0: return "DSn";
    case 1: return "ASn";
    case 2: return "DTACKn";
    case 3: return "BERRn";
    case 4: return "coincidence";
    case 5: return "Pulser A";
    case 6: return "Pulser B";
    case 7: return "counter end gate";
    case 8: return "location monitor";
    case 9: return "Register Set Status";
    case 10: return "VME bus grant";
    default: return "reserved/unknown";
  }
}

void append_register(std::ostringstream& log, int32_t handle,
                     const char* name, int offset,
                     unsigned int* value, bool* valid) {
  *value = 0;
  const CVErrorCodes rc = CAENVME_ReadRegister(
      handle, static_cast<CVRegisters>(offset), value);
  *valid = rc == cvSuccess;
  log << "; " << name << "=";
  if (*valid) {
    log << "0x" << std::hex << std::setw(4) << std::setfill('0')
        << (*value & 0xFFFFu) << std::dec;
  } else {
    log << "read error " << static_cast<int>(rc);
  }
}

void append_pulser(std::ostringstream& log, int32_t handle,
                   CVPulserSelect selected) {
  const char* name = selected == cvPulserA ? "A" : "B";
  unsigned char period = 0, width = 0, pulse_count = 0;
  CVTimeUnits unit = cvUnit25ns;
  CVIOSources start = cvManualSW, reset = cvManualSW;
  const CVErrorCodes rc = CAENVME_GetPulserConf(
      handle, selected, &period, &width, &unit, &pulse_count, &start, &reset);
  log << "; Pulser" << name << "_GetPulserConf=";
  if (rc == cvSuccess) {
    log << "period=" << static_cast<unsigned>(period)
        << ",width=" << static_cast<unsigned>(width)
        << ",unit=" << static_cast<int>(unit)
        << ",pulse_count=" << static_cast<unsigned>(pulse_count)
        << ",start_source=" << static_cast<int>(start)
        << ",reset_source=" << static_cast<int>(reset);
  } else {
    log << "read error " << static_cast<int>(rc);
  }

  // SETUP, START, CLEAR, NCYCLE, WIDTH, DELAY, PERIOD are consecutive
  // read/write registers in the V3718 manual. Read only; START/CLEAR values
  // do not prove whether a burst is currently running.
  const int base = selected == cvPulserA ? kPulseASetup : kPulseBSetup;
  const char* fields[] = {"setup", "start", "clear", "ncycle", "width",
                          "delay", "period"};
  unsigned int values[7] = {};
  bool valid[7] = {};
  for (int i = 0; i < 7; ++i) {
    std::ostringstream field;
    field << "Pulser" << name << "_" << fields[i];
    append_register(log, handle, field.str().c_str(), base + i,
                    &values[i], &valid[i]);
  }
  if (valid[3] && valid[4] && valid[6]) {
    const unsigned unit_ns = values[3] & 0x8000u ? 25000u : 25u;
    log << "; Pulser" << name << "_decoded=ncycle:"
        << (values[3] & 0x7FFFu) << ",period_ticks:" << values[6]
        << ",width_ticks:" << values[4] << ",tick_ns:" << unit_ns;
  }
  log << "; Pulser" << name << "_running=unverified";
}

}  // namespace

void log_v3718_out0_diagnostic(MVME_INTERFACE* vme) {
  static bool attempted = false;
  if (attempted) return;
  attempted = true;

  std::ostringstream log;
  log << "V3718 OUT0 diagnostic (read-only, before Global BUSY configuration): ";
  if (!vme) {
    log << "VME handle unavailable";
    cm_msg(MINFO, "fevme", "%s", log.str().c_str());
    return;
  }

  CVIOPolarity polarity = cvDirect;
  CVLEDPolarity led_polarity = cvActiveHigh;
  CVIOSources source = cvManualSW;
  const CVErrorCodes conf = CAENVME_GetOutputConf(
      vme->handle, cvOutput0, &polarity, &led_polarity, &source);
  if (conf == cvSuccess) {
    log << "GetOutputConf source=" << static_cast<int>(source)
        << ",polarity=" << (polarity == cvDirect ? "direct" :
                             polarity == cvInverted ? "inverted" : "unknown")
        << ",LED_polarity=" << (led_polarity == cvActiveHigh ? "active high" :
                                 led_polarity == cvActiveLow ? "active low" : "unknown");
  } else {
    log << "GetOutputConf read error " << static_cast<int>(conf);
  }

  unsigned int mux = 0, io_polarity = 0, level = 0, status = 0;
  unsigned int io_status = 0, set_status = 0;
  bool mux_ok = false, polarity_ok = false, level_ok = false;
  bool status_ok = false, io_status_ok = false, set_status_ok = false;
  append_register(log, vme->handle, "OUT_2_0_MUX_SET", kOutMux,
                  &mux, &mux_ok);
  append_register(log, vme->handle, "IO_POLARITY", kIoPolarity,
                  &io_polarity, &polarity_ok);
  append_register(log, vme->handle, "IO_LEVEL", kIoLevel, &level, &level_ok);
  append_register(log, vme->handle, "STATUS", kStatus, &status, &status_ok);
  append_register(log, vme->handle, "IO_STATUS_READ", kIoStatusRead,
                  &io_status, &io_status_ok);
  append_register(log, vme->handle, "IO_STATUS_SET", kIoStatusSet,
                  &set_status, &set_status_ok);

  if (mux_ok) {
    log << "; OUT0_source=" << mux_source(mux & 0xFu)
        << "; OUT0_software_control="
        << ((mux & 0xFu) == 9u ? "enabled" : "disabled");
  }
  if (polarity_ok)
    log << "; OUT0_polarity=" << (io_polarity & 1u ? "inverted" : "direct");
  if (level_ok)
    log << "; level_selection=" << (level & 0x40u ? "software" : "hardware")
        << ",IO_LEVEL_setting=" << (level & 0x80u ? "TTL" : "NIM");
  if (status_ok)
    log << "; level_status=" << (status & 0x2000u ? "TTL" : "NIM");
  if (io_status_ok) log << "; OUT0_io_status=" << (io_status & 1u);
  if (set_status_ok) log << "; OUT0_software_set_status=" << (set_status & 1u);

  const bool pulser_a = (mux_ok && (mux & 0xFu) == 5u) ||
                        (conf == cvSuccess && source == cvPulserV3718A);
  const bool pulser_b = (mux_ok && (mux & 0xFu) == 6u) ||
                        (conf == cvSuccess && source == cvPulserV3718B);
  if (pulser_a) append_pulser(log, vme->handle, cvPulserA);
  if (pulser_b) append_pulser(log, vme->handle, cvPulserB);

  cm_msg(MINFO, "fevme", "%s", log.str().c_str());
}
