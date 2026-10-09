#include <switch.h>
#include <stdio.h>
#ifndef PROBE_VERSION
#define PROBE_VERSION 1
#endif

/* Independent small Horizon homebrew. No Wine, account or system data access. */
volatile unsigned long probe_frames;
int main(void) {
    consoleInit(NULL);
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    printf("Autorun remote bench probe v%d\n", PROBE_VERSION);
    printf("Environment: real Nintendo Switch / Horizon\n");
    printf("A: write local test marker. PLUS: exit normally.\n");
    printf("No game/runtime/settings files are changed.\n");
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        if (down & HidNpadButton_A) {
            FILE *file = fopen("sdmc:/switch/autorun-remote-test/probe-result.txt", "w");
            if (file) {
                fprintf(file, "probe_version=%d\nframes=%lu\nenvironment=Horizon\n",
                        PROBE_VERSION, probe_frames);
                int rc = fclose(file);
                printf("Test marker written, close result=%d\n", rc);
            } else printf("Test marker unavailable\n");
        }
        ++probe_frames;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
