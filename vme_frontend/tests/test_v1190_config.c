/* Read-only V1190A/B configuration diagnostic using CAENVMELib directly.
 *
 * The only VME writes made by this program are read-command opcodes sent to
 * the microcontroller.  It does not reset or clear the module, write a
 * configuration opcode, or read the event/output FIFO.
 *
 * Build:
 *   gcc -std=c11 -O2 -Wall -Wextra -Wpedantic \
 *       -o test_v1190_config.exe test_v1190_config.c -lCAENVME
 */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include <CAENVMElib.h>

#define V1190_BASE             UINT32_C(0x00C10000)
#define V1190_MICRO_DATA       UINT32_C(0x102E)
#define V1190_MICRO_HANDSHAKE  UINT32_C(0x1030)
#define V1190_ROM_VERSION      UINT32_C(0x4030)
#define MICRO_WRITE_OK         UINT16_C(0x0001)
#define MICRO_READ_OK          UINT16_C(0x0002)

enum { MICRO_MAX_POLLS = 1000, MICRO_POLL_NS = 1000000 };

static void report_error(const char *operation, uint32_t address,
                         CVErrorCodes rc)
{
    fprintf(stderr, "%s at 0x%08" PRIX32 " failed: rc=%d (%s)\n",
            operation, address, (int)rc, CAENVME_DecodeError(rc));
}

static int read16(int32_t handle, uint32_t offset, uint16_t *value)
{
    CVErrorCodes rc = CAENVME_ReadCycle(handle, V1190_BASE + offset, value,
                                        cvA24_U_DATA, cvD16);
    if (rc != cvSuccess) {
        report_error("D16 read", V1190_BASE + offset, rc);
        return -1;
    }
    return 0;
}

static int read32(int32_t handle, uint32_t offset, uint32_t *value)
{
    CVErrorCodes rc = CAENVME_ReadCycle(handle, V1190_BASE + offset, value,
                                        cvA24_U_DATA, cvD32);
    if (rc != cvSuccess) {
        report_error("D32 read", V1190_BASE + offset, rc);
        return -1;
    }
    return 0;
}

static void poll_delay(void)
{
    const struct timespec delay = { .tv_sec = 0, .tv_nsec = MICRO_POLL_NS };
    (void)nanosleep(&delay, NULL);
}

static int micro_wait_write_ready(int32_t handle)
{
    unsigned poll;
    uint16_t handshake = 0;

    for (poll = 0; poll < MICRO_MAX_POLLS; ++poll) {
        if (read16(handle, V1190_MICRO_HANDSHAKE, &handshake) != 0)
            return -1;
        if ((handshake & MICRO_WRITE_OK) != 0)
            return 0;
        poll_delay();
    }
    fprintf(stderr, "micro write-ready timeout after %u polls "
            "(last handshake=0x%04X)\n", MICRO_MAX_POLLS, handshake);
    return -1;
}

static int micro_write_opcode(int32_t handle, uint16_t opcode)
{
    CVErrorCodes rc;

    if (micro_wait_write_ready(handle) != 0)
        return -1;
    rc = CAENVME_WriteCycle(handle, V1190_BASE + V1190_MICRO_DATA,
                            &opcode, cvA24_U_DATA, cvD16);
    if (rc != cvSuccess) {
        report_error("micro read-opcode write",
                     V1190_BASE + V1190_MICRO_DATA, rc);
        return -1;
    }
    return 0;
}

static int micro_wait_read_ready(int32_t handle)
{
    unsigned poll;
    uint16_t handshake = 0;

    for (poll = 0; poll < MICRO_MAX_POLLS; ++poll) {
        if (read16(handle, V1190_MICRO_HANDSHAKE, &handshake) != 0)
            return -1;
        if ((handshake & MICRO_READ_OK) != 0)
            return 0;
        poll_delay();
    }
    fprintf(stderr, "micro read-ready timeout after %u polls "
            "(last handshake=0x%04X)\n", MICRO_MAX_POLLS, handshake);
    return -1;
}

static int micro_read_word(int32_t handle, uint16_t *value)
{
    if (micro_wait_read_ready(handle) != 0)
        return -1;
    return read16(handle, V1190_MICRO_DATA, value);
}

static int micro_read_command(int32_t handle, uint16_t opcode,
                              uint16_t *words, size_t count)
{
    size_t i;

    if (micro_write_opcode(handle, opcode) != 0)
        return -1;
    /* In particular, do not abandon the five-word 0x1600 response after a
       successful word: consume every requested response unless a bus access
       or bounded handshake wait itself fails. */
    for (i = 0; i < count; ++i) {
        if (micro_read_word(handle, &words[i]) != 0) {
            fprintf(stderr, "opcode 0x%04X response word %zu/%zu failed\n",
                    opcode, i + 1, count);
            return -1;
        }
    }
    return 0;
}

static const char *yes_no(unsigned enabled)
{
    return enabled ? "enabled" : "disabled";
}

static const char *edge_mode_name(unsigned mode)
{
    static const char *const names[] = {
        "pair mode", "trailing edge only", "leading edge only",
        "leading and trailing edges"
    };
    return names[mode & 3u];
}

static const char *single_edge_resolution(unsigned code)
{
    static const char *const names[] = { "800 ps", "200 ps", "100 ps", "reserved" };
    return names[code & 3u];
}

static const char *leading_resolution(unsigned code)
{
    static const char *const names[] = { "100 ps", "200 ps", "400 ps", "800 ps" };
    return names[code & 3u];
}

static const char *width_resolution(unsigned code)
{
    static const char *const names[] = {
        "100 ps", "200 ps", "400 ps", "800 ps", "1.6 ns", "3.2 ns",
        "6.25 ns", "12.5 ns", "25 ns", "50 ns", "100 ns", "200 ns",
        "400 ns", "800 ns", "reserved", "reserved"
    };
    return names[code & 15u];
}

static const char *dead_time_name(unsigned code)
{
    static const char *const names[] = { "approximately 5 ns", "10 ns", "30 ns", "100 ns" };
    return names[code & 3u];
}

static void print_max_hits(uint16_t raw)
{
    unsigned code = raw & 0x0fu;
    if (code <= 8u)
        printf("  decoded: maximum hits/event = %u\n",
               code == 0u ? 0u : 1u << (code - 1u));
    else if (code == 9u)
        puts("  decoded: maximum hits/event = unlimited");
    else
        printf("  decoded: maximum hits/event = reserved code %u\n", code);
}

static int read_regular_registers(int32_t handle, uint16_t *control)
{
    uint16_t value16;
    uint32_t value32;

#define READ_PRINT16(offset, label) do {                                      \
    if (read16(handle, (offset), &value16) != 0) return -1;                    \
    printf("  %-24s [0x%04X] raw=0x%04X (%u)\n", (label), (offset),           \
           value16, value16);                                                  \
} while (0)

    puts("Regular registers:");
    if (read16(handle, UINT32_C(0x1000), control) != 0)
        return -1;
    printf("  %-24s [0x1000] raw=0x%04X\n", "Control", *control);
    printf("    Empty Event:               %s\n", yes_no(*control & (1u << 3)));
    printf("    Event FIFO:                %s\n", yes_no(*control & (1u << 8)));
    printf("    Extended Trigger Time Tag: %s\n", yes_no(*control & (1u << 9)));
    READ_PRINT16(UINT32_C(0x1002), "Status");
    if (read32(handle, UINT32_C(0x101C), &value32) != 0)
        return -1;
    printf("  %-24s [0x101C] raw=0x%08" PRIX32 " (%" PRIu32 ")\n",
           "Event Counter", value32, value32);
    READ_PRINT16(UINT32_C(0x1020), "Event Stored");
    READ_PRINT16(UINT32_C(0x1022), "Almost Full Level");
    READ_PRINT16(UINT32_C(0x1026), "Firmware Revision");
    READ_PRINT16(UINT32_C(0x103C), "Event FIFO Stored");
    READ_PRINT16(UINT32_C(0x103E), "Event FIFO Status");
#undef READ_PRINT16
    return 0;
}

static int read_module_version(int32_t handle, unsigned *channel_words)
{
    uint16_t version;

    if (read16(handle, V1190_ROM_VERSION, &version) != 0)
        return -1;
    printf("Configuration ROM Version [0x4030]: raw=0x%04X, ", version);
    if ((version & UINT16_C(0x00FF)) == 0u) {
        puts("decoded=V1190A (128 channels)");
        *channel_words = 8;
    } else if ((version & UINT16_C(0x00FF)) == 1u) {
        puts("decoded=V1190B (64 channels)");
        *channel_words = 4;
    } else {
        printf("decoded=unknown version 0x%02X\n", version & 0xffu);
        fputs("Refusing to send micro opcodes because the required 0x4500 "
              "response length is unknown.\n", stderr);
        return -1;
    }
    return 0;
}

static int read_micro_configuration(int32_t handle, unsigned channel_words)
{
    uint16_t word[8];
    unsigned edge_mode;
    unsigned i;
    uint64_t mask_low;
    uint64_t mask_high = 0;

    puts("\nMicrocontroller read commands:");
    if (micro_read_command(handle, UINT16_C(0x0200), word, 1) != 0) return -1;
    printf("Acquisition mode (0x0200): raw=0x%04X, decoded=%s\n", word[0],
           (word[0] & 1u) ? "trigger matching" : "continuous storage");

    if (micro_read_command(handle, UINT16_C(0x1600), word, 5) != 0) return -1;
    printf("Trigger configuration (0x1600): raw=[0x%04X 0x%04X 0x%04X 0x%04X 0x%04X]\n",
           word[0], word[1], word[2], word[3], word[4]);
    printf("  Window width=%u counts (%u ns)\n", word[0], word[0] * 25u);
    {
        int16_t offset = (int16_t)(word[1] & UINT16_C(0x0FFF));
        if ((offset & INT16_C(0x0800)) != 0)
            offset = (int16_t)(offset | (int16_t)UINT16_C(0xF000));
        printf("  Window offset=signed-12-bit %d counts (%d ns)\n",
               offset, (int)offset * 25);
    }
    printf("  Extra search margin=%u counts (%u ns)\n", word[2], word[2] * 25u);
    printf("  Reject margin=%u counts (%u ns)\n", word[3], word[3] * 25u);
    printf("  Trigger-time subtraction=%s\n", yes_no(word[4] & 1u));

    if (micro_read_command(handle, UINT16_C(0x2300), word, 1) != 0) return -1;
    edge_mode = word[0] & 3u;
    printf("Edge mode (0x2300): raw=0x%04X, decoded=%s\n",
           word[0], edge_mode_name(edge_mode));

    if (micro_read_command(handle, UINT16_C(0x2600), word, 1) != 0) return -1;
    printf("Resolution (0x2600): raw=0x%04X", word[0]);
    if (edge_mode == 0u)
        printf(", decoded=leading %s, width %s\n",
               leading_resolution(word[0]), width_resolution(word[0] >> 8));
    else
        printf(", decoded=%s\n", single_edge_resolution(word[0]));

    if (micro_read_command(handle, UINT16_C(0x2900), word, 1) != 0) return -1;
    printf("Dead time (0x2900): raw=0x%04X, decoded=%s\n",
           word[0], dead_time_name(word[0]));

    if (micro_read_command(handle, UINT16_C(0x3200), word, 1) != 0) return -1;
    printf("TDC Header/Trailer (0x3200): raw=0x%04X, decoded=%s\n",
           word[0], yes_no(word[0] & 1u));

    if (micro_read_command(handle, UINT16_C(0x3400), word, 1) != 0) return -1;
    printf("Maximum hits/event (0x3400): raw=0x%04X\n", word[0]);
    print_max_hits(word[0]);

    if (micro_read_command(handle, UINT16_C(0x3A00), word, 1) != 0) return -1;
    printf("TDC error mask (0x3A00): raw=0x%04X, decoded mask=0x%03X\n",
           word[0], word[0] & 0x07ffu);

    if (micro_read_command(handle, UINT16_C(0x3C00), word, 1) != 0) return -1;
    printf("Effective TDC FIFO size (0x3C00): raw=0x%04X, decoded code=%u",
           word[0], word[0] & 0x0fu);
    if ((word[0] & 0x0fu) <= 7u)
        printf(" (%u words)\n", 1u << ((word[0] & 0x0fu) + 1u));
    else
        puts(" (reserved)");

    if (micro_read_command(handle, UINT16_C(0x4500), word, channel_words) != 0)
        return -1;
    printf("Channel enable mask, V1190%c (0x4500): raw=[",
           channel_words == 8 ? 'A' : 'B');
    for (i = 0; i < channel_words; ++i)
        printf("%s0x%04X", i == 0 ? "" : " ", word[i]);
    puts("]");
    mask_low = (uint64_t)word[0] | ((uint64_t)word[1] << 16) |
               ((uint64_t)word[2] << 32) | ((uint64_t)word[3] << 48);
    if (channel_words == 8)
        mask_high = (uint64_t)word[4] | ((uint64_t)word[5] << 16) |
                    ((uint64_t)word[6] << 32) | ((uint64_t)word[7] << 48);
    if (channel_words == 8)
        printf("  decoded 128-channel mask=0x%016" PRIX64 "%016" PRIX64 "\n",
               mask_high, mask_low);
    else
        printf("  decoded 64-channel mask=0x%016" PRIX64 "\n", mask_low);
    printf("  enabled channels:");
    for (i = 0; i < channel_words * 16u; ++i) {
        uint64_t part = i < 64u ? mask_low : mask_high;
        unsigned bit = i < 64u ? i : i - 64u;
        if ((part & (UINT64_C(1) << bit)) != 0)
            printf(" %u", i);
    }
    if (mask_low == 0 && mask_high == 0)
        printf(" none");
    putchar('\n');
    return 0;
}

int main(void)
{
    int32_t handle = -1;
    int link = 0;
    uint16_t control;
    unsigned channel_words;
    CVErrorCodes rc;
    int status = 1;

    rc = CAENVME_Init2(cvUSB_V3718, &link, 0, &handle);
    if (rc != cvSuccess) {
        fprintf(stderr, "CAENVME_Init2(cvUSB_V3718, link=0, board=0) failed: "
                "rc=%d (%s)\n", (int)rc, CAENVME_DecodeError(rc));
        return 1;
    }

    puts("V1190A/B configuration diagnostic: base=0x00C10000, A24 user data");
    puts("Read-only policy: normal-register reads plus micro read opcodes only;");
    puts("no reset, clear, configuration opcode, or event/output FIFO read.");

    if (read_module_version(handle, &channel_words) == 0 &&
        read_regular_registers(handle, &control) == 0 &&
        read_micro_configuration(handle, channel_words) == 0)
        status = 0;
    else
        fputs("Aborting: no further micro opcode will be sent.\n", stderr);

    rc = CAENVME_End(handle);
    if (rc != cvSuccess) {
        fprintf(stderr, "CAENVME_End failed: rc=%d (%s)\n",
                (int)rc, CAENVME_DecodeError(rc));
        status = 1;
    }
    return status;
}
