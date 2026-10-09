#ifndef AUTORUN_STEAM_PROBE_H
#define AUTORUN_STEAM_PROBE_H
#include <stddef.h>

/* No credentials or raw network text cross this boundary. Not an auth result. */
enum steam_result { STEAM_OK, STEAM_CANCELLED, STEAM_NETWORK, STEAM_TLS,
    STEAM_HTTP, STEAM_RESPONSE_LIMIT, STEAM_PROTOCOL, STEAM_MEMORY };
struct steam_probe_result {
    enum steam_result code;
    long http_status;
    unsigned int transport_code;
};
typedef int (*steam_cancel_fn)(void *context);
#define STEAM_CM_SERVERS 8
struct steam_servers { char endpoint[STEAM_CM_SERVERS][128]; unsigned int count; };
int steam_directory_servers(const unsigned char *, size_t, struct steam_servers *);
int steam_endpoint_valid(const char *);
void steam_discover(struct steam_probe_result *, struct steam_servers *, steam_cancel_fn, void *);
void steam_probe(struct steam_probe_result *result, steam_cancel_fn cancel, void *context);
int steam_directory_valid(const unsigned char *data, size_t size);
const char *steam_result_message(enum steam_result result);
#endif
