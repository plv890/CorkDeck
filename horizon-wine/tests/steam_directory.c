#include "../source/steam/steam_probe.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    const char *good = "{\"response\":{\"success\":true,\"serverlist\":[]}}";
    const char *bad[] = {
        "", "{}", "{\"response\":{\"success\":false}}",
        "{\"response\":{\"success\":true,\"success\":false}}",
        "{\"response\":{\"success\":true},\"response\":{\"success\":true}}",
        "{\"response\":{\"success\":true}}garbage",
        "{\"response\":{\"success\":\"true\"}}",
        "{\"response\":{\"success\":true,}}",
        "{\"message\":\"success:true\"}",
        "{\"response\":{\"success\":true,\"unknown\":[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[0]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]}}"
    };
    size_t i;
    struct steam_servers servers;
    const char *list="{\"response\":{\"success\":true,\"serverlist\":[{\"endpoint\":\"cmp1.steamserver.net:443\",\"type\":\"websockets\",\"realm\":\"steamglobal\"}]}}";
    assert(steam_directory_servers((const unsigned char *)list,strlen(list),&servers));
    assert(servers.count==1&&!strcmp(servers.endpoint[0],"cmp1.steamserver.net:443"));
    assert(steam_endpoint_valid("cmp1.steamserver.net:443"));
    assert(!steam_endpoint_valid("steamserver.net.evil.com:443"));
    assert(!steam_endpoint_valid("user@cmp1.steamserver.net:443"));
    assert(!steam_endpoint_valid("cmp1.steamserver.net:65536"));
    assert(!steam_endpoint_valid("cmp1.steamserver.net:443/path"));
    assert(steam_directory_valid((const unsigned char *)good, strlen(good)));
    for (i = 0; i < strlen(good); ++i)
        assert(!steam_directory_valid((const unsigned char *)good, i));
    for (i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i)
        assert(!steam_directory_valid((const unsigned char *)bad[i], strlen(bad[i])));
    return 0;
}
