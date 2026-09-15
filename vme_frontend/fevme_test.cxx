#include <stdio.h>

#include "midas.h"
#include "mfe.h"
#include "mvmestd.h"
#include "vme/v792.h"

#define V792_BASE 0x00600000  // V792 VME base address

const char *frontend_name = "fe_vme_test";      // MIDAS frontend name
const char *frontend_file_name = __FILE__;      // Frontend source file name

BOOL frontend_call_loop = FALSE;                // Disable periodic call to frontend_loop()
BOOL equipment_common_overwrite = FALSE;        // Preserve equipment Common settings stored in ODB
INT display_period = 1000;                      // MIDAS status display update period [ms]
INT max_event_size = 1024 * 1024;               // Maximum event size [bytes]
INT max_event_size_frag = 5 * 1024 * 1024;      // Maximum fragmented event size [bytes]
INT event_buffer_size = 10 * 1024 * 1024;       // MIDAS event buffer size [bytes]

static MVME_INTERFACE *gVme = NULL;              // MIDAS VME interface handle


/* Open the MIDAS VME interface and verify communication with the V792 without changing module settings. */
INT frontend_init()
{
    printf("Opening VME interface...\n");

    INT status = mvme_open(&gVme, 0);

    if (status != MVME_SUCCESS) {
        cm_msg(MERROR, frontend_name, "mvme_open() failed: %d", status);
        return FE_ERR_HW;
    }

    mvme_set_am(gVme, MVME_AM_A24_ND);
    mvme_set_dmode(gVme, MVME_DMODE_D16);

    printf("VME interface opened.\n");
    printf("Checking V792 at 0x%08X...\n", V792_BASE);

    if (!v792_isPresent(gVme, V792_BASE)) {
        cm_msg(MERROR, frontend_name,
               "V792 not found at 0x%08X", V792_BASE);

        mvme_close(gVme);
        gVme = NULL;

        return FE_ERR_HW;
    }

    printf("V792 detected.\n");

    v792_Status(gVme, V792_BASE);

    return SUCCESS;
}


/* Close the MIDAS VME interface when the frontend terminates. */
INT frontend_exit()
{
    if (gVme) {
        mvme_close(gVme);
        gVme = NULL;
    }

    printf("VME interface closed.\n");

    return SUCCESS;
}


/* Handle the beginning of a MIDAS run without changing V792 configuration. */
INT begin_of_run(INT run_number, char *error)
{
    printf("Begin run %d\n", run_number);

    return SUCCESS;
}


/* Handle the end of a MIDAS run. */
INT end_of_run(INT run_number, char *error)
{
    printf("End run %d\n", run_number);

    return SUCCESS;
}


/* Handle a MIDAS run pause. */
INT pause_run(INT run_number, char *error)
{
    return SUCCESS;
}


/* Handle resuming a paused MIDAS run. */
INT resume_run(INT run_number, char *error)
{
    return SUCCESS;
}


/* Periodic frontend loop; disabled because frontend_call_loop is FALSE. */
INT frontend_loop()
{
    return SUCCESS;
}


/* Poll the hardware for an available event; event acquisition is not implemented yet. */
INT poll_event(INT source, INT count, BOOL test)
{
    return 0;
}


/* Configure interrupt-driven event acquisition; interrupt mode is not used in this test frontend. */
INT interrupt_configure(INT cmd, INT source, PTYPE adr)
{
    return SUCCESS;
}


/* Build a MIDAS event from VME data; no MIDAS banks are generated at this stage. */
INT read_vme_event(char *pevent, INT off)
{
    return 0;
}


/* Define the MIDAS equipment handled by this frontend. */
EQUIPMENT equipment[] = {
    {
        "VME",                    // Equipment name
        {
            1,                    // Event ID
            0,                    // Trigger mask
            "SYSTEM",             // Event buffer name
            EQ_PERIODIC,          // Equipment type
            0,                    // Event source
            "MIDAS",              // Data format
            TRUE,                 // Enable equipment
            RO_ALWAYS,            // Readout condition
            10000,                // Readout period [ms]
            0,                    // Event limit; 0 = no automatic stop
            0,                    // Number of sub-events
            0,                    // History logging period [s]
            "",                   // Frontend host name
            "",                   // Frontend name
            "",                   // Frontend source file name
            "",                   // Equipment status text
            "",                   // Equipment status color
            FALSE                 // Hidden flag
        },
        read_vme_event,           // Event readout function
    },

    {""}                          // End of equipment list
};
