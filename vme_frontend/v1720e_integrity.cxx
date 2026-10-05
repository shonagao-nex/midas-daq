#include "v1720e_integrity.h"

namespace v1720e_integrity {

Result check(const Event &event, uint32_t expected_event_words,
             uint32_t expected_channel_mask, const State &previous)
{
    Result result = {};
    result.size_valid = event.event_size == expected_event_words;
    result.channel_mask_valid = event.channel_mask == (expected_channel_mask & 0xFFu);
    result.counter_continuous = true;
    result.have_delta = previous.have_previous;
    result.next = previous;
    if (!previous.have_previous) {
        result.next.first_counter = event.event_counter;
        result.next.have_previous = true;
    } else {
        result.counter_delta = (event.event_counter - previous.previous_counter) & 0x00FFFFFFu;
        result.ttt_delta = (event.trigger_time_tag - previous.previous_ttt) & 0x7FFFFFFFu;
        result.counter_continuous = result.counter_delta == 1;
        if (previous.ttt_count == 0 || result.ttt_delta < previous.min_ttt_delta)
            result.next.min_ttt_delta = result.ttt_delta;
        if (result.ttt_delta > previous.max_ttt_delta)
            result.next.max_ttt_delta = result.ttt_delta;
        ++result.next.ttt_count;
    }
    result.next.last_counter = event.event_counter;
    result.next.previous_counter = event.event_counter;
    result.next.previous_ttt = event.trigger_time_tag;
    return result;
}

}
