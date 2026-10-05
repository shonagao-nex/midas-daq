#ifndef V1720E_INTEGRITY_H
#define V1720E_INTEGRITY_H

#include <cstdint>

namespace v1720e_integrity {

struct Event {
    uint32_t event_size;
    uint32_t channel_mask;
    uint32_t event_counter;
    uint32_t trigger_time_tag;
};

struct State {
    bool have_previous;
    uint32_t first_counter;
    uint32_t last_counter;
    uint32_t previous_counter;
    uint32_t previous_ttt;
    uint64_t ttt_count;
    uint32_t min_ttt_delta;
    uint32_t max_ttt_delta;
};

struct Result {
    bool size_valid;
    bool channel_mask_valid;
    bool counter_continuous;
    bool have_delta;
    uint32_t counter_delta;
    uint32_t ttt_delta;
    State next;
    bool valid() const { return size_valid && channel_mask_valid && counter_continuous; }
};

Result check(const Event &event, uint32_t expected_event_words,
             uint32_t expected_channel_mask, const State &previous);

}

#endif
