#ifndef V1720E_CONFIG_H
#define V1720E_CONFIG_H

#include "vme_odb.h"

namespace v1720e_config {

V1720ESettings default_settings();

struct RunConfig {
    V1720ESettings settings = default_settings();
    V1720E_CONFIG hardware = {};
    bool variables_enabled = true;
    DWORD expected_event_words = V1720E_DEFAULT_EVENT_WORDS;
    DWORD expected_channel_mask = V1720E_DEFAULT_CHANNEL_MASK;
};

struct LifecycleState {
    bool startup_enabled = true;
    INT startup_run_state = -1;
    bool started = false;
    // A failed RUN verification can follow a successful hardware write.
    bool start_attempted = false;
};

struct RuntimeState {
    V1720ERuntimeState value = {};
    DWORD last_variables_publish = 0;
};

struct State {
    RunConfig run;
    LifecycleState lifecycle;
    RuntimeState runtime;
};

bool snapshot_run_settings(State &state);
bool verify_readback(const V1720E_CONFIG &expected,
                     const V1720E_CONFIG_READBACK &actual);
void capture_readback(V1720EReadbackSnapshot &snapshot,
                      const V1720E_CONFIG_READBACK &readback, bool valid);

}  // namespace v1720e_config

#endif
