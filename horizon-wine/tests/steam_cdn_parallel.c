/* Verified localhost TLS and real libcurl multi. Server refuses /pair unless
 * two requests reach it simultaneously; no account or external network. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static int use_ca = 1;
static void reset_fixture(CURL *curl) {
  curl_easy_reset(curl);
  if (use_ca)
    assert(curl_easy_setopt(curl, CURLOPT_CAINFO, getenv("STEAM_TEST_CA")) ==
           CURLE_OK);
}
#define curl_easy_reset reset_fixture
#include "../source/steam/steam_cdn.c"
static void complete(struct steam_cdn *p, unsigned int count, unsigned int http,
                     int valid) {
  struct steam_cdn_result r;
  uint64_t started = now();
  unsigned int seen = 0;
  while (seen < count) {
    assert(now() - started < 5000);
    assert(steam_cdn_pump(p, 10));
    while (steam_cdn_take(p, &r)) {
      assert(r.http == http);
      assert(valid ? !r.transport : r.transport != 0);
      ++seen;
    }
  }
}
int main(int argc, char **argv) {
  struct steam_cdn *p;
  struct steam_cdn_metrics m;
  unsigned char buffers[2][65536];
  char url[256];
  unsigned int i;
  assert(argc == 2 && getenv("STEAM_TEST_CA"));
  assert(!curl_global_init(CURL_GLOBAL_DEFAULT));
  p = steam_cdn_create(2);
  assert(p);
  snprintf(url, sizeof(url), "https://localhost:%s/pair", argv[1]);
  for (i = 0; i < 2; ++i)
    assert(steam_cdn_start(p, i, url, buffers[i], sizeof(buffers[i])));
  complete(p, 2, 200, 1);
  steam_cdn_metrics(p, &m);
  assert(m.peak_active == 2 && m.requests == 2 && m.connections == 2 &&
         m.bytes == 2 * sizeof(buffers[0]));
  assert(buffers[0][0] == 'x' && buffers[1][65535] == 'x');
  for (i = 0; i < 2; ++i)
    assert(steam_cdn_start(p, i, url, buffers[i], sizeof(buffers[i])));
  complete(p, 2, 200, 1);
  steam_cdn_metrics(p, &m);
  assert(m.requests == 4 && m.connections == 2);
  snprintf(url, sizeof(url), "https://localhost:%s/redirect", argv[1]);
  assert(steam_cdn_start(p, 0, url, buffers[0], sizeof(buffers[0])));
  complete(p, 1, 302, 1);
  snprintf(url, sizeof(url), "https://localhost:%s/fixture", argv[1]);
  assert(steam_cdn_start(p, 0, url, buffers[0], 1024));
  complete(p, 1, 200, 0);
  assert(steam_cdn_start(p, 0, url, buffers[0], sizeof(buffers[0])));
  steam_cdn_cancel(p);
  assert(steam_cdn_start(p, 0, url, buffers[0], sizeof(buffers[0])));
  complete(p, 1, 200, 1);
  steam_cdn_destroy(p);
  use_ca = 0;
  p = steam_cdn_create(1);
  assert(p);
  assert(steam_cdn_start(p, 0, url, buffers[0], sizeof(buffers[0])));
  complete(p, 1, 0, 0);
  steam_cdn_destroy(p);
  assert(!steam_cdn_create(STEAM_CDN_MAX_REQUESTS + 1));
  p = steam_cdn_create(1);
  assert(p);
  assert(!steam_cdn_start(p, 0, "ftp://localhost/", buffers[0],
                          sizeof(buffers[0])));
  steam_cdn_destroy(p);
  curl_global_cleanup();
  puts("Real HTTPS parallel overlap, 2 reused connections, bounded buffers, "
       "cancellation, no redirects and TLS refusal passed");
  return 0;
}
