#include "../source/steam/steam_probe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int cancel(void *opaque) { (void)opaque; return 1; }
int main(int argc, char **argv)
{
    struct steam_probe_result result;
    steam_probe(&result, cancel, NULL);
    assert(result.code == STEAM_CANCELLED && !result.http_status && !result.transport_code);
    if (argc == 2 && !strcmp(argv[1], "--live")) {
        steam_probe(&result, NULL, NULL);
        printf("result=%u http=%ld transport=%u\n", result.code, result.http_status, result.transport_code);
        return result.code != STEAM_OK;
    }
    return 0;
}
