#ifndef AUTORUN_STEAM_SETTINGS_H
#define AUTORUN_STEAM_SETTINGS_H
#include <stddef.h>
#include <stdint.h>
#define STEAM_LANGUAGE_BASE (UINT64_C(1) << 63)
/* Explicit per-install choice; never a global language setting. */
#define STEAM_LANGUAGE_BASE_ID "base"
struct steam_language { const char *id, *name; };
extern const struct steam_language steam_languages[];
extern const size_t steam_language_count;
int steam_language_valid(const char *);
uint64_t steam_language_mask(const char *comma_separated);
/* Returns 1 for an explicit Steam language, 2 for system inheritance,
 * or 0 for absent/invalid settings. Output is always the resolved language. */
int steam_settings_load(const char *root, char language[32]);
int steam_settings_save(const char *root, const char *language);
/* Simultaneous CDN connections: 1, 2, 4, 8, 12 or 16; 16 unless chosen. */
#define STEAM_CONNECTIONS_DEFAULT 16u
static inline int steam_connections_valid(unsigned int n){return n==1||n==2||n==4||n==8||n==12||n==16;}
unsigned int steam_settings_concurrency(const char *root);
int steam_settings_save_concurrency(const char *root,unsigned int concurrency);
#endif
