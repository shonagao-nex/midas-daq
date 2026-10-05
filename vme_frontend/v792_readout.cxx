#include "v792_readout.h"
#include "vme/v792.h"

namespace v792_readout {

// Read one D32 event through its Footer without scanning into the next event.
EventInfo read_single_event(const Access &access, DWORD (&data)[kMaxEventWords])
{
  EventInfo event;
  int saved_mode;
  if (access.get_dmode(access.vme, &saved_mode) != MVME_SUCCESS) {
    cm_msg(MERROR, access.log_source, "Cannot get VME data mode");
    return event;
  }

  size_t result = 0;
  unsigned expected = 0;
  unsigned measurements = 0;
  unsigned geo = 0;
  if (access.set_dmode(access.vme, MVME_DMODE_D32) != MVME_SUCCESS) {
    cm_msg(MERROR, access.log_source, "Cannot select D32 for V792 readout");
  } else {
    for (size_t i = 0; i < kMaxEventWords; ++i) {
      DWORD word = 0;
      const int status = access.read(access.vme, &word, access.base, sizeof(word));
      if (status != MVME_SUCCESS) {
        cm_msg(MERROR, access.log_source, "V792 read failed at word %zu: status %d", i, status);
        break;
      }
      const unsigned type = (word >> 24) & 0x7;
      if (i == 0) {
        if (type != 2) {
          cm_msg(MERROR, access.log_source, "V792 expected Header, got 0x%08X (type %u)", word, type);
          break;
        }
        expected = (word >> 8) & 0x3f;
        geo = word >> 27;
        event.expected_measurements = expected;
        event.geo = geo;
        if (expected > V792_MAX_CHANNELS || expected + 2 > kMaxEventWords) {
          cm_msg(MERROR, access.log_source, "V792 invalid Header count %u", expected);
          break;
        }
      } else {
        if ((word >> 27) != geo || (type != 0 && type != 4)) {
          cm_msg(MERROR, access.log_source, "V792 invalid word %zu: 0x%08X (type %u, GEO %u, expected GEO %u)",
                 i, word, type, word >> 27, geo);
          break;
        }
        if (type == 4) {
          if (measurements != expected) {
            cm_msg(MERROR, access.log_source, "V792 Footer count mismatch: Header %u, received %u", expected, measurements);
            break;
          }
          data[i] = word;
          result = i + 1;
          event.event_counter = word & 0x00FFFFFF;
          event.measurements = measurements;
          event.valid = true;
          event.footer_consumed = true;
          break;
        }
        if (measurements >= expected) {
          cm_msg(MERROR, access.log_source, "V792 expected Footer after %u measurements, got 0x%08X", measurements, word);
          break;
        }
        ++measurements;
      }
      data[i] = word;
      if (i + 1 == kMaxEventWords)
        cm_msg(MERROR, access.log_source, "V792 readout reached limit of %zu words without Footer", kMaxEventWords);
    }
  }
  if (access.set_dmode(access.vme, saved_mode) != MVME_SUCCESS) {
    cm_msg(MERROR, access.log_source, "Cannot restore VME data mode");
    result = 0;
    event.valid = false;
  }
  event.words = result;
  return event;
}

// Adapt a checked MVME header read to the existing V792 BLT reader.
static int blt_header_read(void *context, uint32_t address, uint32_t *word)
{
  const auto &access = *static_cast<const Access *>(context);
  return access.read(access.vme, word, address, sizeof(*word)) == MVME_SUCCESS ? 0 : -1;
}

// Forward the supplied BLT callback without changing its transfer semantics.
static int blt_transfer(void *context, uint32_t address, void *destination, int requested_bytes, int *actual_bytes)
{
  const auto &access = *static_cast<const Access *>(context);
  return access.blt_read(access.blt_context, address, destination, requested_bytes, actual_bytes);
}

// Run the existing V792 BLT reader with the established D32 mode handling.
BltResult read_blt32_event(const Access &access, DWORD (&data)[kMaxEventWords])
{
  BltResult outcome;
  int saved_mode = 0;
  if (access.get_dmode(access.vme, &saved_mode) != MVME_SUCCESS) {
    cm_msg(MERROR, access.log_source, "V792 BLT32 cannot get VME data mode");
    return outcome;
  }
  if (access.set_dmode(access.vme, MVME_DMODE_D32) != MVME_SUCCESS) {
    access.set_dmode(access.vme, saved_mode);
    cm_msg(MERROR, access.log_source, "V792 BLT32 cannot select D32 header mode");
    return outcome;
  }
  Access io_access = access;
  const V792_BLT_IO io = {blt_header_read, blt_transfer, &io_access};
  outcome.status = v792_read_blt32(&io, access.base, data, kMaxEventWords, &outcome.details);
  outcome.restore_status = access.set_dmode(access.vme, saved_mode);
  if (outcome.status != V792_BLT_OK || outcome.restore_status != MVME_SUCCESS) {
    outcome.stop_required = true;
    return outcome;
  }
  outcome.event.words = outcome.details.words;
  outcome.event.event_counter = outcome.details.event_counter;
  outcome.event.expected_measurements = outcome.details.measurements;
  outcome.event.measurements = outcome.details.measurements;
  outcome.event.geo = outcome.details.geo;
  outcome.event.valid = true;
  return outcome;
}

}
