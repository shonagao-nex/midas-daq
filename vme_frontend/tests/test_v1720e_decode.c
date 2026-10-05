#include "v1720e.h"
#include <assert.h>
#include <stdio.h>

static void check_status(DWORD raw, int run, int event, int clock, int pll, int board)
{
    const V1720E_ACQUISITION_STATE state = v1720e_decode_acquisition_status(raw);
    assert(state.running == run);
    assert(state.event_ready == event);
    assert(state.external_clock == clock);
    assert(state.pll_locked == pll);
    assert(state.board_ready == board);
}

static void check_board(DWORD raw, unsigned low, unsigned model, unsigned channels, int supported)
{
    const V1720E_BOARD_ID id = v1720e_decode_board_info(raw);
    assert(id.low_byte == low && id.model_byte == model && id.channels == channels);
    assert(id.supported == supported);
}

int main(void)
{
    check_status(0, 0, 0, 0, 0, 0);
    check_status(0x04u, 1, 0, 0, 0, 0);
    check_status(0x08u, 0, 1, 0, 0, 0);
    check_status(0x20u, 0, 0, 1, 0, 0);
    check_status(0x80u, 0, 0, 0, 1, 0);
    check_status(0x100u, 0, 0, 0, 0, 1);
    check_status(0x1acu, 1, 1, 1, 1, 1);
    check_status(0xfffffdd3u, 0, 0, 0, 1, 1);

    check_board(0x00080203u, 3, 2, 8, 1);
    check_board(0xff080203u, 3, 2, 8, 1); /* The high byte remains ignored. */
    check_board(0x00080202u, 2, 2, 8, 0);
    check_board(0x00080204u, 4, 2, 8, 0);
    check_board(0x00080103u, 3, 1, 8, 0);
    check_board(0x00080303u, 3, 3, 8, 0);
    check_board(0x00070203u, 3, 2, 7, 0);
    check_board(0x00090203u, 3, 2, 9, 0);
    check_board(0, 0, 0, 0, 0);
    check_board(0xffffffffu, 255, 255, 255, 0);
    puts("V1720E decode tests passed");
    return 0;
}
