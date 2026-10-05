#include "v775_readout.h"
#include "v775.h"

namespace v775_readout {

// Read one bounded D32 event, stopping at the first valid EOB.
EventInfo read_single_event(const Access &access, DWORD (&data)[kMaxEventWords])
{
  EventInfo event;
  int saved_mode;
  if (access.get_dmode(access.vme, &saved_mode) != MVME_SUCCESS) {
    cm_msg(MERROR, access.log_source, "Cannot get VME data mode for V775 readout");
    return event;
  }

  if (access.set_dmode(access.vme, MVME_DMODE_D32) != MVME_SUCCESS) {
    cm_msg(MERROR, access.log_source, "Cannot select D32 for V775 readout");
  } else {
    unsigned expected_measurements = 0;
    unsigned measurements = 0;
    unsigned geo = 0;
    for (size_t i = 0; i < kMaxEventWords; ++i) {
      DWORD word = 0;
      const int status = access.read(access.vme, &word, access.base, sizeof(word));
      if (status != MVME_SUCCESS) {
        cm_msg(MERROR, access.log_source, "V775 read failed at word %zu: status %d", i, status);
        break;
      }

      data[i] = word;
      const unsigned type = (word >> 24) & 0x7;
      if (i == 0) {
        if (type != V775_DATA_TYPE_HEADER) {
          cm_msg(MERROR, access.log_source, "V775 expected Header, got 0x%08X (type %u)", word, type);
          break;
        }
        geo = word >> 27;
        expected_measurements = (word >> 8) & 0x3F;
        event.expected_measurements = expected_measurements;
        event.geo = geo;
        if (expected_measurements > V775_MAX_CHANNELS) {
          cm_msg(MERROR, access.log_source, "V775 invalid Header channel count %u", expected_measurements);
          break;
        }
        continue;
      }

      if ((word >> 27) != geo) {
        cm_msg(MERROR, access.log_source, "V775 GEO mismatch at word %zu: got %u, expected %u", i, word >> 27, geo);
        break;
      }
      if (type == V775_DATA_TYPE_EOB) {
        if (measurements != expected_measurements) {
          cm_msg(MERROR, access.log_source, "V775 EOB count mismatch: Header %u, measurements %u",
                 expected_measurements, measurements);
          break;
        }
        event.event_counter = word & 0x00FFFFFF;
        event.measurements = measurements;
        event.words = i + 1;
        event.valid = true;
        event.eob_consumed = true;
        break; // EOB consumed; never pre-read the next event.
      }

      switch (type) {
      case V775_DATA_TYPE_MEASUREMENT:
        ++measurements;
        if (measurements > expected_measurements) {
          cm_msg(MERROR, access.log_source, "V775 received more measurements than Header count %u",
                 expected_measurements);
          i = kMaxEventWords;
        }
        break;
      case V775_DATA_TYPE_INVALID:
        cm_msg(MERROR, access.log_source, "V775 invalid datum (type 6) at word %zu: 0x%08X", i, word);
        i = kMaxEventWords;
        break;
      default:
        cm_msg(MERROR, access.log_source, "V775 reserved word type %u at word %zu: 0x%08X", type, i, word);
        i = kMaxEventWords;
        break;
      }
      if (i + 1 == kMaxEventWords && event.words == 0)
        cm_msg(MERROR, access.log_source, "V775 readout reached limit of %zu words without EOB", kMaxEventWords);
    }
  }

  if (access.set_dmode(access.vme, saved_mode) != MVME_SUCCESS) {
    cm_msg(MERROR, access.log_source, "Cannot restore VME data mode after V775 readout");
    event.words = 0;
    event.valid = false;
  }
  return event;
}

}
