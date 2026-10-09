#ifndef AUTORUN_STEAM_HASH_H
#define AUTORUN_STEAM_HASH_H
#include <stddef.h>
/* SHA-1 of depot content. On the console this is the processor's own SHA
 * instructions through libnx, several times mbedtls's portable code; a known
 * answer is checked once and the portable code stays the fallback. */
#include <mbedtls/sha1.h>
#ifdef __SWITCH__
#include <switch.h>
int steam_content_hardware(void);
struct steam_sha1 {
  int hardware;
  Sha1Context fast;
  mbedtls_sha1_context portable;
};
#else
struct steam_sha1 {
  mbedtls_sha1_context portable;
};
#endif
static inline void steam_sha1_begin(struct steam_sha1 *s) {
#ifdef __SWITCH__
  s->hardware = steam_content_hardware();
  if (s->hardware) {
    sha1ContextCreate(&s->fast);
    return;
  }
#endif
  mbedtls_sha1_init(&s->portable);
  (void)mbedtls_sha1_starts_ret(&s->portable);
}
static inline void steam_sha1_update(struct steam_sha1 *s, const void *p,
                                     size_t n) {
#ifdef __SWITCH__
  if (s->hardware) {
    sha1ContextUpdate(&s->fast, p, n);
    return;
  }
#endif
  (void)mbedtls_sha1_update_ret(&s->portable, p, n);
}
static inline void steam_sha1_end(struct steam_sha1 *s, unsigned char out[20]) {
#ifdef __SWITCH__
  if (s->hardware) {
    sha1ContextGetHash(&s->fast, out);
    return;
  }
#endif
  (void)mbedtls_sha1_finish_ret(&s->portable, out);
  mbedtls_sha1_free(&s->portable);
}
static inline void steam_sha1(const void *p, size_t n, unsigned char out[20]) {
  struct steam_sha1 s;
  steam_sha1_begin(&s);
  steam_sha1_update(&s, p, n);
  steam_sha1_end(&s, out);
}
#endif
