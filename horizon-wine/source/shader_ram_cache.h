#ifndef WINE_NX_SHADER_RAM_CACHE_H
#define WINE_NX_SHADER_RAM_CACHE_H
#include <stddef.h>
#include <stdint.h>
struct nx_shader_cache_stats { size_t bytes, budget; uint64_t hits, misses, puts, evictions; };
void wine_nx_shader_ram_configure(size_t bytes);
void *wine_nx_shader_ram_get(const void *owner, const void *key, size_t key_size, size_t *size);
void wine_nx_shader_ram_put(const void *owner, const void *key, size_t key_size, const void *data, size_t size);
void wine_nx_shader_ram_forget(const void *owner);
struct nx_shader_cache_stats wine_nx_shader_ram_stats(void);
/* Drops only copied hot entries; never waits behind a cache user. */
size_t wine_nx_shader_ram_reclaim(size_t bytes);
/* Low memory: drop every hot copy and keep none until it is called with 0. */
void wine_nx_shader_ram_pressure(int low);
#endif
