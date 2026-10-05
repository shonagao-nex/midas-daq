#include "v1720e_config.h"
#include "v1720e_internal.h"

#include <cstring>

extern const char *frontend_name;

namespace v1720e_config {

static const DWORD V1720E_TRIGGER_SOFTWARE = 0x80000000u;
static const DWORD V1720E_TRIGGER_EXTERNAL = 0x40000000u;
static const DWORD V1720E_CHANNEL_CONFIG_ZS_MASK = 0x000F0000u;
static const DWORD V1720E_CHANNEL_CONFIG_PACK25 = 0x00000800u;

//************************************//
// Provide the verified default V1720E run settings
//************************************//
V1720ESettings default_settings()
{
    V1720ESettings settings = {};
    settings.enabled = TRUE;
    settings.buffer_organization = 0x0Au;
    settings.record_length_samples = V1720E_DEFAULT_RECORD_SAMPLES;
    settings.post_trigger = 0x30u;
    settings.software_trigger_enabled = TRUE;
    settings.external_trigger_enabled = TRUE;
    for (unsigned channel = 0; channel < V1720E_CHANNEL_COUNT; ++channel) {
        settings.channel_self_trigger_enabled[channel] = FALSE;
        settings.channel_enabled[channel] = TRUE;
        settings.dc_offset[channel] = 0x8000u;
    }
    return settings;
}

//************************************//
// Build the V1720E hardware configuration for a run
//************************************//
static bool make_run_configuration(const V1720ESettings &settings,
                                          V1720E_CONFIG &config,
                                          DWORD &expected_event_words,
                                          DWORD &expected_channel_mask)
{
    if (settings.buffer_organization > 0x0Au) {
        cm_msg(MERROR, frontend_name,
               "V1720E BufferOrganization %u is outside supported range 0..10",
               settings.buffer_organization);
        return false;
    }
    if (settings.post_trigger > 0xFFu) {
        cm_msg(MERROR, frontend_name,
               "V1720E PostTrigger %u is outside the 8-bit register range",
               settings.post_trigger);
        return false;
    }
    /*
     * The installed waveform-recording firmware 4.5 was verified with
     * Custom Size 0x40 producing 256 samples/channel.  The older CAEN
     * register description warns that its generic NLOC conversion may not
     * apply above ROC firmware 3.8, so the first ODB version deliberately
     * accepts only this verified pair.
     */
    if (settings.record_length_samples != V1720E_DEFAULT_RECORD_SAMPLES) {
        cm_msg(MERROR, frontend_name,
               "V1720E RecordLengthSamples %u is unsupported; first ODB version accepts only %u",
               settings.record_length_samples,
               V1720E_DEFAULT_RECORD_SAMPLES);
        return false;
    }

    config = {};
    config.buffer_organization = settings.buffer_organization;
    config.custom_size = V1720E_DEFAULT_CUSTOM_SIZE;
    config.post_trigger = settings.post_trigger;
    if (settings.software_trigger_enabled)
        config.trigger_source |= V1720E_TRIGGER_SOFTWARE;
    if (settings.external_trigger_enabled)
        config.trigger_source |= V1720E_TRIGGER_EXTERNAL;

    unsigned enabled_channels = 0;
    for (unsigned channel = 0; channel < V1720E_CHANNEL_COUNT; ++channel) {
        if (settings.channel_self_trigger_enabled[channel])
            config.trigger_source |= (1u << channel);
        if (settings.channel_enabled[channel]) {
            config.channel_enable |= (1u << channel);
            ++enabled_channels;
        }
        config.dc_offset[channel] = settings.dc_offset[channel];
    }
    if (enabled_channels == 0) {
        cm_msg(MERROR, frontend_name,
               "V1720E configuration has no enabled channels");
        return false;
    }

    expected_channel_mask = config.channel_enable & 0xFFu;
    expected_event_words = 4u + enabled_channels *
        (settings.record_length_samples / 2u);
    if (expected_event_words > V1720E_MAX_EVENT_WORDS) {
        cm_msg(MERROR, frontend_name,
               "V1720E expected event size %u exceeds safety limit %u",
               expected_event_words, V1720E_MAX_EVENT_WORDS);
        return false;
    }
    return true;
}

//************************************//
// Capture V1720E ODB settings for the next run
//************************************//
bool snapshot_run_settings(RunConfig &run, const V1720ESettings &settings)
{
    V1720E_CONFIG config = {};
    DWORD expected_event_words = 0;
    DWORD expected_channel_mask = 0;
    if (settings.enabled &&
        !make_run_configuration(settings, config,
                                       expected_event_words,
                                       expected_channel_mask))
        return false;
    run.settings = settings;
    run.hardware = config;
    run.expected_event_words = expected_event_words;
    run.expected_channel_mask = expected_channel_mask;
    return true;
}

//************************************//
// Copy V1720E register readback into the run snapshot
//************************************//
void capture_readback(V1720EReadbackSnapshot &snapshot,
                      const V1720E_CONFIG_READBACK &readback, bool valid)
{
    snapshot = {};
    snapshot.valid = valid ? TRUE : FALSE;
    snapshot.board_info = readback.board_info;
    snapshot.roc_firmware_revision = readback.roc_firmware;
    snapshot.buffer_organization = readback.buffer_organization;
    snapshot.custom_size_raw = readback.custom_size;
    snapshot.record_length_samples =
        readback.custom_size == V1720E_DEFAULT_CUSTOM_SIZE
            ? V1720E_DEFAULT_RECORD_SAMPLES : 0u;
    snapshot.post_trigger = readback.post_trigger;
    snapshot.software_trigger_enabled =
        (readback.trigger_source & V1720E_TRIGGER_SOFTWARE) != 0;
    snapshot.external_trigger_enabled =
        (readback.trigger_source & V1720E_TRIGGER_EXTERNAL) != 0;
    for (unsigned channel = 0; channel < V1720E_CHANNEL_COUNT; ++channel) {
        snapshot.channel_self_trigger_enabled[channel] =
            (readback.trigger_source & (1u << channel)) != 0;
        snapshot.channel_enabled[channel] =
            (readback.channel_enable & (1u << channel)) != 0;
        snapshot.dc_offset[channel] = readback.dc_offset[channel];
    }
    snapshot.zero_suppression_enabled =
        (readback.channel_config & V1720E_CHANNEL_CONFIG_ZS_MASK) != 0;
    snapshot.pack25_enabled =
        (readback.channel_config & V1720E_CHANNEL_CONFIG_PACK25) != 0;
    snapshot.trigger_source_raw = readback.trigger_source;
    snapshot.channel_enable_raw = readback.channel_enable;
    snapshot.channel_config_raw = readback.channel_config;
}

//************************************//
// Compare V1720E register readback with requested settings
//************************************//
bool verify_readback(const V1720E_CONFIG &expected,
                                   const V1720E_CONFIG_READBACK &actual)
{
    bool ok = true;
#define VERIFY_V1720(name, expected_value, actual_value) \
    do { \
        if ((expected_value) != (actual_value)) { \
            cm_msg(MERROR, frontend_name, \
                   "V1720E readback mismatch: %s expected 0x%X, got 0x%X", \
                   name, static_cast<unsigned>(expected_value), \
                   static_cast<unsigned>(actual_value)); \
            ok = false; \
        } \
    } while (0)
    VERIFY_V1720("BufferOrganization", expected.buffer_organization,
                 actual.buffer_organization);
    VERIFY_V1720("CustomSizeRaw", expected.custom_size, actual.custom_size);
    VERIFY_V1720("PostTrigger", expected.post_trigger, actual.post_trigger);
    VERIFY_V1720("TriggerSourceRaw", expected.trigger_source,
                 actual.trigger_source);
    VERIFY_V1720("ChannelEnableRaw", expected.channel_enable,
                 actual.channel_enable);
    VERIFY_V1720("ZeroSuppression", 0u,
                 actual.channel_config & V1720E_CHANNEL_CONFIG_ZS_MASK);
    VERIFY_V1720("Pack25", 0u,
                 actual.channel_config & V1720E_CHANNEL_CONFIG_PACK25);
    for (unsigned channel = 0; channel < V1720E_CHANNEL_COUNT; ++channel) {
        if (expected.dc_offset[channel] != actual.dc_offset[channel]) {
            cm_msg(MERROR, frontend_name,
                   "V1720E readback mismatch: DCOffset[%u] expected 0x%04X, got 0x%04X",
                   channel, expected.dc_offset[channel],
                   actual.dc_offset[channel]);
            ok = false;
        }
    }
#undef VERIFY_V1720
    return ok;
}


}  // namespace v1720e_config

extern "C" {

int v1720e_configure(MVME_INTERFACE *vme, DWORD base,
                     const V1720E_CONFIG *config)
{
    DWORD control = 0, status_reg = 0, channel_config = 0;
    unsigned channel;
    int status;
    if (!vme || !config)
        return MVME_INVALID_PARAM;
    status = v1720e_read32(vme, base, REG_ACQUISITION_CONTROL, &control);
    if (status != MVME_SUCCESS)
        return status;
    status = v1720e_read32(vme, base, REG_ACQUISITION_STATUS, &status_reg);
    if (status != MVME_SUCCESS)
        return status;
    if ((control & ACQUISITION_CONTROL_RUN_REQUEST) ||
        (status_reg & ACQUISITION_STATUS_RUN_ACTIVE) ||
        (status_reg & STATUS_EXTERNAL_CLOCK) ||
        (status_reg & (STATUS_PLL_OK | STATUS_BOARD_READY)) !=
            (STATUS_PLL_OK | STATUS_BOARD_READY))
        return MVME_ACCESS_ERROR;
    status = v1720e_read32(vme, base, REG_CHANNEL_CONFIG, &channel_config);
    if (status != MVME_SUCCESS)
        return status;
    channel_config &= ~(CHANNEL_CONFIG_ZS_MASK | CHANNEL_CONFIG_PACK25);
    status = v1720e_write_verify(vme, base, REG_CHANNEL_CONFIG, channel_config);
    if (status != MVME_SUCCESS)
        return status;

    /*
     * This exact configuration is verified on ROC FPGA firmware 4.5.
     * In particular, Custom Size 0x40 produces 256 samples/channel.
     * MEB cleanup is verified after RUN start, whose documented memory
     * reset avoids adding a separate Software Clear.
     */
    status = v1720e_write_verify(vme, base, REG_BUFFER_ORGANIZATION,
                          config->buffer_organization);
    if (status != MVME_SUCCESS)
        return status;
    status = v1720e_write_verify(vme, base, REG_CUSTOM_SIZE, config->custom_size);
    if (status != MVME_SUCCESS)
        return status;
    status = v1720e_write_verify(vme, base, REG_TRIGGER_SOURCE,
                          config->trigger_source);
    if (status != MVME_SUCCESS)
        return status;
    status = v1720e_write_verify(vme, base, REG_POST_TRIGGER, config->post_trigger);
    if (status != MVME_SUCCESS)
        return status;
    status = v1720e_write_verify(vme, base, REG_CHANNEL_ENABLE,
                          config->channel_enable);
    if (status != MVME_SUCCESS)
        return status;
    for (channel = 0; channel < V1720E_CHANNEL_COUNT; ++channel) {
        status = v1720e_write_verify(vme, base, REG_DC_OFFSET(channel),
                              config->dc_offset[channel]);
        if (status != MVME_SUCCESS)
            return status;
    }
    return MVME_SUCCESS;
}

int v1720e_read_configuration(MVME_INTERFACE *vme, DWORD base,
                              V1720E_CONFIG_READBACK *readback)
{
    DWORD dc_offset;
    unsigned channel;
    int status;
    if (!vme || !readback)
        return MVME_INVALID_PARAM;
    memset(readback, 0, sizeof(*readback));
#define READ_CONFIG(member, reg) \
    do { \
        status = v1720e_read32(vme, base, reg, &readback->member); \
        if (status != MVME_SUCCESS) return status; \
    } while (0)
    READ_CONFIG(board_info, REG_BOARD_INFO);
    READ_CONFIG(roc_firmware, REG_ROC_FIRMWARE);
    READ_CONFIG(buffer_organization, REG_BUFFER_ORGANIZATION);
    READ_CONFIG(custom_size, REG_CUSTOM_SIZE);
    READ_CONFIG(post_trigger, REG_POST_TRIGGER);
    READ_CONFIG(trigger_source, REG_TRIGGER_SOURCE);
    READ_CONFIG(channel_enable, REG_CHANNEL_ENABLE);
    READ_CONFIG(channel_config, REG_CHANNEL_CONFIG);
#undef READ_CONFIG
    for (channel = 0; channel < V1720E_CHANNEL_COUNT; ++channel) {
        status = v1720e_read32(vme, base, REG_DC_OFFSET(channel), &dc_offset);
        if (status != MVME_SUCCESS)
            return status;
        readback->dc_offset[channel] = (WORD)(dc_offset & 0xFFFFu);
    }
    return MVME_SUCCESS;
}

}
