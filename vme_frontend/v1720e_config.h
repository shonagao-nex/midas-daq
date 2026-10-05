#ifndef V1720E_CONFIG_H
#define V1720E_CONFIG_H

#include "vme_odb.h"

extern "C" {
int v1720e_configure(MVME_INTERFACE *vme, DWORD base,
                     const V1720E_CONFIG *config);
int v1720e_read_configuration(MVME_INTERFACE *vme, DWORD base,
                              V1720E_CONFIG_READBACK *readback);
}

namespace v1720e_config {

V1720ESettings default_settings();

struct RunConfig {
    V1720ESettings settings = default_settings();
    V1720E_CONFIG hardware = {};
    DWORD expected_event_words = V1720E_DEFAULT_EVENT_WORDS;
    DWORD expected_channel_mask = V1720E_DEFAULT_CHANNEL_MASK;
};

bool snapshot_run_settings(RunConfig &run, const V1720ESettings &settings);
bool verify_readback(const V1720E_CONFIG &expected,
                     const V1720E_CONFIG_READBACK &actual);
void capture_readback(V1720EReadbackSnapshot &snapshot,
                      const V1720E_CONFIG_READBACK &readback, bool valid);

}  // namespace v1720e_config

#endif
