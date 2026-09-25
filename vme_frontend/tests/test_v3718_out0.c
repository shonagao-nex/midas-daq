/* Standalone V3718 OUT0 polarity diagnostic. Run only while fevme is stopped.
 * status is read-only; set/clear explicitly configure and drive OUT0 only.
 * V3718 register offsets: CAEN V3718 User Manual rev. 4, pp. 28-32.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <CAENVMElib.h>

enum command { COMMAND_STATUS, COMMAND_SET, COMMAND_CLEAR };

/* V3718 rev.4 IO_STATUS_SET bit 0 controls OUT0. CAENVMELib 4.1.3
 * applies Set/ClearOutputRegister masks without V3718 bit remapping. */
enum { V3718_OUT0_STATUS_BIT = 0x0001 };

struct bridge_register {
    const char *name;
    int offset;
    unsigned int value;
};

static const char *out0_source(unsigned int mux)
{
    static const char *const names[] = {
        "DSn", "ASn", "DTACKn", "BERRn", "Coincidence", "Pulser A",
        "Pulser B", "Counter End Gate", "Location Monitor",
        "Register Set Status", "VME Bus Grant"
    };
    unsigned int source = mux & 0xFu;
    return source < sizeof names / sizeof names[0] ? names[source] : "reserved";
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s status|set|clear\n", program);
    fputs("  status: read V3718 OUT0 configuration; no OUT0 write\n"
          "  set:    configure OUT0 direct/active-high LED/manual software, then SET OUT0\n"
          "  clear:  configure OUT0 direct/active-high LED/manual software, then CLEAR OUT0\n"
          "Run only with fevme stopped. Measure OUT0 with the intended 50-ohm termination.\n"
          "No OUT1-OUT3, common NIM/TTL, or VME module access is requested.\n",
          stderr);
}

static int print_status(int32_t handle)
{
    /* Do not use the legacy CVRegisters names: they describe older bridges. */
    struct bridge_register reg[] = {
        {"OUT_2_0_MUX_SET", 0x09, 0},
        {"IO_POLARITY", 0x08, 0},
        {"IO_LEVEL", 0x07, 0},
        {"STATUS", 0x00, 0},
        {"IO_STATUS_READ", 0x0B, 0},
        {"IO_STATUS_SET", 0x0C, 0}
    };
    CVIOPolarity polarity = cvDirect;
    CVLEDPolarity led = cvActiveHigh;
    CVIOSources source = cvManualSW;
    CVErrorCodes rc = CAENVME_GetOutputConf(handle, cvOutput0,
                                             &polarity, &led, &source);
    int ok = 1;
    if (rc == cvSuccess) {
        printf("GetOutputConf: source=%d polarity=%s LED_polarity=%s\n",
               (int)source,
               polarity == cvDirect ? "direct" :
               polarity == cvInverted ? "inverted" : "unknown",
               led == cvActiveHigh ? "active-high" :
               led == cvActiveLow ? "active-low" : "unknown");
    } else {
        fprintf(stderr, "GetOutputConf failed: %d (%s)\n",
                (int)rc, CAENVME_DecodeError(rc));
        ok = 0;
    }

    for (size_t i = 0; i < sizeof reg / sizeof reg[0]; ++i) {
        rc = CAENVME_ReadRegister(handle, (CVRegisters)reg[i].offset,
                                  &reg[i].value);
        if (rc == cvSuccess) {
            printf("%-18s [0x%02X] = 0x%04X\n", reg[i].name, reg[i].offset,
                   reg[i].value & 0xFFFFu);
        } else {
            fprintf(stderr, "%s read failed: %d (%s)\n", reg[i].name,
                    (int)rc, CAENVME_DecodeError(rc));
            ok = 0;
        }
    }
    if (!ok)
        return 0;

    printf("OUT0: source=%s; polarity=%s; software_control=%s\n",
           out0_source(reg[0].value),
           reg[1].value & 1u ? "inverted" : "direct",
           (reg[0].value & 0xFu) == 9u ? "enabled" : "disabled");
    printf("Levels: selection=%s; configured=%s; status=%s\n",
           reg[2].value & 0x40u ? "software" : "hardware",
           reg[2].value & 0x80u ? "TTL" : "NIM",
           reg[3].value & 0x2000u ? "TTL" : "NIM");
    printf("OUT0: io_status=%u; software_set_status=%u\n",
           reg[4].value & 1u, reg[5].value & 1u);
    return 1;
}

int main(int argc, char **argv)
{
    enum command command;
    uint32_t link = 0;
    int32_t handle = -1;
    int ok;
    CVErrorCodes rc;

    /* Parse before opening hardware; invalid input cannot cause a write. */
    if (argc != 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "status") == 0)
        command = COMMAND_STATUS;
    else if (strcmp(argv[1], "set") == 0)
        command = COMMAND_SET;
    else if (strcmp(argv[1], "clear") == 0)
        command = COMMAND_CLEAR;
    else {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (command == COMMAND_STATUS)
        puts("V3718 OUT0 status: read-only; no output configuration or level write.");
    else
        printf("V3718 OUT0 %s: configure OUT0 as direct/manual software, "
               "LED active-high; then %s V3718 IO_STATUS_SET bit 0 only.\n",
               argv[1], command == COMMAND_SET ? "SET" : "CLEAR");
    puts("No OUT1-OUT3, common NIM/TTL, or VME module operation is requested.");
    fflush(stdout);

    rc = CAENVME_Init2(cvUSB_V3718, &link, 0, &handle);
    if (rc != cvSuccess) {
        fprintf(stderr, "V3718 open failed: %d (%s)\n",
                (int)rc, CAENVME_DecodeError(rc));
        return EXIT_FAILURE;
    }

    puts("Before operation:");
    ok = print_status(handle);
    if (ok && command != COMMAND_STATUS) {
        /* Match global_busy::set_global_busy(), without touching IO_LEVEL. */
        rc = CAENVME_SetOutputConf(handle, cvOutput0, cvDirect,
                                   cvActiveHigh, cvManualSW);
        if (rc != cvSuccess) {
            fprintf(stderr, "OUT0 configuration failed: %d (%s)\n",
                    (int)rc, CAENVME_DecodeError(rc));
            ok = 0;
        } else {
            rc = command == COMMAND_SET
                ? CAENVME_SetOutputRegister(handle, V3718_OUT0_STATUS_BIT)
                : CAENVME_ClearOutputRegister(handle, V3718_OUT0_STATUS_BIT);
            if (rc != cvSuccess) {
                fprintf(stderr, "OUT0 %s failed: %d (%s)\n", argv[1],
                        (int)rc, CAENVME_DecodeError(rc));
                ok = 0;
            }
        }
        puts("After operation:");
        if (!print_status(handle))
            ok = 0;
    }

    rc = CAENVME_End(handle);
    if (rc != cvSuccess) {
        fprintf(stderr, "V3718 close failed: %d (%s)\n",
                (int)rc, CAENVME_DecodeError(rc));
        ok = 0;
    }
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
