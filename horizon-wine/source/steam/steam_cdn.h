#ifndef AUTORUN_STEAM_CDN_H
#define AUTORUN_STEAM_CDN_H
#include <stddef.h>
#include <stdint.h>
#define STEAM_CDN_MAX_REQUESTS 16
struct steam_cdn;
struct steam_cdn_result {
  unsigned int slot, transport, http;
  size_t bytes;
};
struct steam_cdn_metrics {
  uint64_t bytes, request_ms, first_byte_ms, body_ms;
  unsigned int requests, connections, peak_active;
};
/* Caller owns CM, URLs and bounded response buffers. No disk writes or auth
 * callbacks inside CURL. A single caller drives all simultaneous transfers. */
struct steam_cdn *steam_cdn_create(unsigned int concurrency);
int steam_cdn_start(struct steam_cdn *, unsigned int slot, const char *url,
                    unsigned char *buffer, size_t capacity);
int steam_cdn_pump(struct steam_cdn *, unsigned int wait_ms);
int steam_cdn_take(struct steam_cdn *, struct steam_cdn_result *);
void steam_cdn_metrics(struct steam_cdn *, struct steam_cdn_metrics *);
void steam_cdn_cancel(struct steam_cdn *);
void steam_cdn_destroy(struct steam_cdn *);
#endif
