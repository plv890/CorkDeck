#define _POSIX_C_SOURCE 200809L
#include "steam_cdn.h"
#include "steam_wire.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
struct request {
  CURL *curl;
  struct steam_cdn *pool;
  unsigned char *data;
  size_t capacity, used;
  uint64_t started;
  unsigned int slot;
  int active;
};
struct steam_cdn {
  CURLM *multi;
  unsigned int count, active;
  struct request requests[STEAM_CDN_MAX_REQUESTS];
  struct steam_cdn_metrics metrics;
};
static uint64_t now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static size_t receive(char *bytes, size_t size, size_t count, void *opaque) {
  struct request *r = opaque;
  size_t n;
  if (size && count > SIZE_MAX / size)
    return 0;
  n = size * count;
  if (n > r->capacity - r->used)
    return 0;
  memcpy(r->data + r->used, bytes, n);
  r->used += n;
  r->pool->metrics.bytes += n;
  return n;
}
/* With the default 64 KiB a connection carries one window per round trip to
 * the server, about a megabyte a second from another country. Asked for
 * before the connection is made, so the larger window is negotiated. */
static int roomy_socket(void *opaque, curl_socket_t fd, curlsocktype purpose) {
  int size = 1024 * 1024;
  (void)opaque;
  if (purpose == CURLSOCKTYPE_IPCXN)
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
  return CURL_SOCKOPT_OK;
}
struct steam_cdn *steam_cdn_create(unsigned int concurrency) {
  struct steam_cdn *p;
  unsigned int i;
  if (!concurrency || concurrency > STEAM_CDN_MAX_REQUESTS)
    return NULL;
  p = calloc(1, sizeof(*p));
  if (!p)
    return NULL;
  p->count = concurrency;
  p->multi = curl_multi_init();
  if (!p->multi)
    goto failed;
  if (curl_multi_setopt(p->multi, CURLMOPT_MAX_TOTAL_CONNECTIONS,
                        (long)concurrency) != CURLM_OK ||
      curl_multi_setopt(p->multi, CURLMOPT_MAX_HOST_CONNECTIONS,
                        (long)concurrency) != CURLM_OK)
    goto failed;
  for (i = 0; i < concurrency; ++i) {
    p->requests[i].curl = curl_easy_init();
    p->requests[i].pool = p;
    p->requests[i].slot = i;
    if (!p->requests[i].curl)
      goto failed;
  }
  return p;
failed:
  steam_cdn_destroy(p);
  return NULL;
}
int steam_cdn_start(struct steam_cdn *p, unsigned int slot, const char *url,
                    unsigned char *buffer, size_t capacity) {
  struct request *r;
  if (!p || slot >= p->count || !buffer || !capacity ||
      capacity > 4u * 1024u * 1024u + 64 || !url ||
      (strncmp(url, "https://", 8) && strncmp(url, "http://", 7)) ||
      strlen(url) >= 2048)
    return 0;
  r = &p->requests[slot];
  if (r->active)
    return 0;
  curl_easy_reset(r->curl);
  r->data = buffer;
  r->capacity = capacity;
  r->used = 0;
#define OPT(k, v)                                                              \
  do {                                                                         \
    if (curl_easy_setopt(r->curl, k, v) != CURLE_OK)                           \
      goto failed;                                                             \
  } while (0)
  OPT(CURLOPT_URL, url);
  OPT(CURLOPT_PROXY, "");
  OPT(CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);
  /* Exactly the scheme asked for: never a redirect, never a downgrade. */
  OPT(CURLOPT_PROTOCOLS,
      (long)(url[4] == 's' ? CURLPROTO_HTTPS : CURLPROTO_HTTP));
  OPT(CURLOPT_FOLLOWLOCATION, 0L);
  OPT(CURLOPT_SSL_VERIFYPEER, 1L);
  OPT(CURLOPT_SSL_VERIFYHOST, 2L);
  OPT(CURLOPT_NOSIGNAL, 1L);
  OPT(CURLOPT_TCP_KEEPALIVE, 1L);
  OPT(CURLOPT_BUFFERSIZE, 128L * 1024L);
  OPT(CURLOPT_CONNECTTIMEOUT, 10L);
  OPT(CURLOPT_TIMEOUT, 120L);
  OPT(CURLOPT_LOW_SPEED_LIMIT, 128L);
  OPT(CURLOPT_LOW_SPEED_TIME, 30L);
  OPT(CURLOPT_SOCKOPTFUNCTION, roomy_socket);
  OPT(CURLOPT_WRITEFUNCTION, receive);
  OPT(CURLOPT_WRITEDATA, r);
  /* Easy handles may copy signed URLs. Reset on completion/cancellation. */
  if (curl_multi_add_handle(p->multi, r->curl) != CURLM_OK)
    goto failed;
  r->active = 1;
  r->started = now();
  ++p->active;
  if (p->active > p->metrics.peak_active)
    p->metrics.peak_active = p->active;
  return 1;
failed:
  curl_easy_reset(r->curl);
  r->data = NULL;
  return 0;
#undef OPT
}
int steam_cdn_pump(struct steam_cdn *p, unsigned int wait_ms) {
  int running = 0;
  if (!p || wait_ms > 20)
    return 0;
  if (curl_multi_perform(p->multi, &running) != CURLM_OK)
    return 0;
  if (running && wait_ms &&
      curl_multi_poll(p->multi, NULL, 0, (int)wait_ms, NULL) != CURLM_OK)
    return 0;
  return 1;
}
int steam_cdn_take(struct steam_cdn *p, struct steam_cdn_result *out) {
  CURLMsg *message;
  int left;
  unsigned int i;
  while ((message = curl_multi_info_read(p->multi, &left))) {
    if (message->msg != CURLMSG_DONE)
      continue;
    for (i = 0; i < p->count; ++i)
      if (p->requests[i].curl == message->easy_handle) {
        struct request *r = &p->requests[i];
        long status = 0, connections = 0;
        double first = 0, total = 0;
        *out = (struct steam_cdn_result){.slot = i,
                                         .transport =
                                             (unsigned int)message->data.result,
                                         .bytes = r->used};
        curl_easy_getinfo(r->curl, CURLINFO_RESPONSE_CODE, &status);
        out->http = (unsigned int)status;
        if (curl_easy_getinfo(r->curl, CURLINFO_NUM_CONNECTS, &connections) ==
                CURLE_OK &&
            connections > 0)
          p->metrics.connections += (unsigned int)connections;
        if (curl_easy_getinfo(r->curl, CURLINFO_STARTTRANSFER_TIME, &first) ==
                CURLE_OK &&
            curl_easy_getinfo(r->curl, CURLINFO_TOTAL_TIME, &total) ==
                CURLE_OK &&
            first >= 0 && total >= first) {
          p->metrics.first_byte_ms += (uint64_t)(first * 1000);
          p->metrics.body_ms += (uint64_t)((total - first) * 1000);
        }
        ++p->metrics.requests;
        p->metrics.request_ms += now() - r->started;
        curl_multi_remove_handle(p->multi, r->curl);
        curl_easy_reset(r->curl);
        r->data = NULL;
        r->active = 0;
        --p->active;
        return 1;
      }
  }
  return 0;
}
void steam_cdn_metrics(struct steam_cdn *p, struct steam_cdn_metrics *out) {
  *out = p->metrics;
}
void steam_cdn_cancel(struct steam_cdn *p) {
  unsigned int i;
  if (!p)
    return;
  for (i = 0; i < p->count; ++i)
    if (p->requests[i].active) {
      curl_multi_remove_handle(p->multi, p->requests[i].curl);
      curl_easy_reset(p->requests[i].curl);
      p->requests[i].active = 0;
      p->requests[i].data = NULL;
    }
  p->active = 0;
}
void steam_cdn_destroy(struct steam_cdn *p) {
  unsigned int i;
  if (!p)
    return;
  steam_cdn_cancel(p);
  for (i = 0; i < p->count; ++i)
    if (p->requests[i].curl)
      curl_easy_cleanup(p->requests[i].curl);
  if (p->multi)
    curl_multi_cleanup(p->multi);
  steam_secret_clear(p, sizeof(*p));
  free(p);
}
