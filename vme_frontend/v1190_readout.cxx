#include "v1190_readout.h"

namespace v1190_readout {

constexpr DWORD kEventCounterMask = 0x003FFFFF;

// Read one bounded D32 event through its Global Trailer.
EventInfo read_single_event(const Access &access, DWORD (&data)[kMaxEventWords])
{
  EventInfo event;
  int saved_mode;
  if (access.get_dmode(access.vme, &saved_mode) != MVME_SUCCESS) {
    cm_msg(MERROR, access.log_source, "Cannot get VME data mode for V1190 readout");
    return event;
  }

  if (access.set_dmode(access.vme, MVME_DMODE_D32) != MVME_SUCCESS) {
    cm_msg(MERROR, access.log_source, "Cannot select D32 for V1190 readout");
  } else {
    for (size_t i = 0; i < kMaxEventWords; ++i) {
      DWORD word = 0;
      const int status = access.read(access.vme, &word, access.base, sizeof(word));
      if (status != MVME_SUCCESS) {
        cm_msg(MERROR, access.log_source, "V1190 read failed at word %zu: status %d", i, status);
        break;
      }

      data[i] = word;
      const unsigned type = (word >> 27) & 0x1F;
      if (i == 0) {
        if (type != 0x08) {
          cm_msg(MERROR, access.log_source, "V1190 expected Global Header, got 0x%08X (type 0x%02X)", word, type);
          break;
        }
        event.event_counter = (word >> 5) & kEventCounterMask;
        continue;
      }

      if (type == 0x10) {
        const unsigned trailer_words = (word >> 5) & 0xFFFF;
        const size_t actual_words = i + 1;
        if (trailer_words != actual_words) {
          cm_msg(MERROR, access.log_source, "V1190 Global Trailer count mismatch: trailer %u, read %zu", trailer_words, actual_words);
          break;
        }
        event.trailer_word_count = trailer_words;
        event.words = actual_words;
        event.valid = true;
        event.trailer_consumed = true;
        break; // Trailer consumed; never pre-read the next event.
      }

      bool invalid_type = false;
      switch (type) {
      case 0x00: // Measurement
      case 0x01: // TDC Header
      case 0x03: // TDC Trailer
      case 0x04: // Error
      case 0x11: // Extended Trigger Time Tag
        break;
      case 0x08:
        cm_msg(MERROR, access.log_source, "V1190 unexpected Global Header at word %zu: 0x%08X", i, word);
        invalid_type = true;
        break;
      case 0x18:
        cm_msg(MERROR, access.log_source, "V1190 unexpected Filler at word %zu: 0x%08X", i, word);
        invalid_type = true;
        break;
      default:
        cm_msg(MERROR, access.log_source, "V1190 reserved word type 0x%02X at word %zu: 0x%08X", type, i, word);
        invalid_type = true;
        break;
      }
      if (invalid_type) break;
      if (i + 1 == kMaxEventWords && event.words == 0)
        cm_msg(MERROR, access.log_source, "V1190 readout reached limit of %zu words without Global Trailer", kMaxEventWords);
    }
  }

  if (access.set_dmode(access.vme, saved_mode) != MVME_SUCCESS) {
    cm_msg(MERROR, access.log_source, "Cannot restore VME data mode after V1190 readout");
    event.words = 0;
    event.valid = false;
  }
  return event;
}

// Adapt the frontend's checked D16 access to the FIFO reader.
static int fifo_read16(void *context, uint32_t address, uint16_t *value)
{
  const Access &access = *static_cast<Access *>(context);
  WORD readback = 0;
  if (!access.read16(access.vme, address, readback, "V1190 Event FIFO D16 read")) return -1;
  *value = readback;
  return 0;
}

// Adapt the frontend's checked D32 access to the FIFO reader.
static int fifo_read32(void *context, uint32_t address, uint32_t *value)
{
  const Access &access = *static_cast<Access *>(context);
  DWORD readback = 0;
  if (!access.read32(access.vme, address, readback, "V1190 Event FIFO entry read")) return -1;
  *value = readback;
  return 0;
}

// Forward the shared VME BLT callback with its original context.
static int fifo_blt_read(void *context, uint32_t address, void *destination, int requested_bytes, int *actual_bytes)
{
  const Access &access = *static_cast<Access *>(context);
  return access.blt_read(access.blt_context, address, destination, requested_bytes, actual_bytes);
}

// Read and validate one FIFO/BLT event using the existing V1190 implementation.
BltResult read_fifo_blt32_event(const Access &access, DWORD (&data)[kMaxEventWords], bool strict_sync_check,
                                V1190_FIFO_BLT_STATE &state)
{
  BltResult result;
  Access io_access = access;
  const V1190_FIFO_BLT_IO io = {fifo_read16, fifo_read32, fifo_blt_read, &io_access};
  result.status = v1190_fifo_read_blt32(&io, access.base, data, kMaxEventWords, strict_sync_check, &state, &result.details);
  result.stop_required = result.status != V1190_FIFO_BLT_OK;
  if (!result.stop_required) {
    result.event.words = result.details.words;
    result.event.event_counter = result.details.event_counter;
    result.event.trailer_word_count = result.details.trailer_word_count;
    result.event.valid = true;
    result.event.trailer_consumed = true;
  }
  return result;
}

}
