#include "v1720e_integrity.h"
#include <cassert>

int main()
{
    using namespace v1720e_integrity;
    State state = {false, 0, 0, 0, 0, 0, 0x7FFFFFFFu, 0};
    Event event = {12, 0x03, 10, 100};
    auto result = check(event, 12, 0x103, state);
    assert(result.valid() && !result.have_delta && result.next.first_counter == 10);
    state = result.next;
    event.event_counter = 11; event.trigger_time_tag = 125;
    result = check(event, 12, 0x03, state);
    assert(result.valid() && result.counter_delta == 1 && result.ttt_delta == 25);
    assert(result.next.ttt_count == 1 && result.next.min_ttt_delta == 25 && result.next.max_ttt_delta == 25);

    state = result.next;
    state.previous_counter = 0x00FFFFFFu; event.event_counter = 0;
    result = check(event, 12, 3, state);
    assert(result.valid() && result.counter_delta == 1);

    state = result.next;
    state.previous_ttt = 0x7FFFFFFEu; event.event_counter = 1; event.trigger_time_tag = 2;
    result = check(event, 12, 3, state);
    assert(result.valid() && result.ttt_delta == 4 && result.next.min_ttt_delta == 0);

    state = result.next;
    event.event_counter = 2; event.event_size = 13;
    result = check(event, 12, 3, state);
    assert(!result.valid() && !result.size_valid && result.channel_mask_valid && result.counter_continuous);

    state = result.next;
    event.event_counter = 3; event.event_size = 12; event.channel_mask = 4;
    result = check(event, 12, 3, state);
    assert(!result.valid() && result.size_valid && !result.channel_mask_valid && result.counter_continuous);

    state = result.next;
    event.event_counter = 7; event.channel_mask = 3;
    result = check(event, 12, 3, state);
    assert(!result.valid() && result.counter_delta == 4 && !result.counter_continuous);
    assert(result.next.previous_counter == 7);

    state = result.next;
    event.event_counter = 8; event.trigger_time_tag = 0x70000000u;
    result = check(event, 12, 3, state);
    assert(result.valid() && result.ttt_delta == 0x6FFFFFFEu);
    assert(result.next.max_ttt_delta == result.ttt_delta && result.next.ttt_count == 7);
}
