/* Bounded, per-cache-instance hot entries over Mesa's persistent backend.
 * Returned data is a copy owned by the caller, as disk_cache_get requires. */
#include "shader_ram_cache.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
/* Enough entries that the byte budget is what bounds the cache: at 512 a
 * game's thousands of small shaders evicted each other with a sixth of the
 * budget in use. A lookup walks them all, which at tens of lookups a second
 * costs nothing beside one read from the card. */
#define SLOTS 8192
#define MAX_ITEM (1024 * 1024)
static struct entry { const void *owner; unsigned char key[32]; size_t key_size, size;
                      void *data; uint64_t stamp; } entries[SLOTS];
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static struct nx_shader_cache_stats stats;
static uint64_t clock_stamp;
/* Set while the program is short of memory: the hot copies are a convenience
 * and are the first thing to go; the persistent cache keeps working. */
static int pressed;
static void evict(unsigned i)
{
    if (!entries[i].data) return;
    stats.bytes -= entries[i].size;
    free(entries[i].data); memset(entries+i,0,sizeof(entries[i])); stats.evictions++;
}
static unsigned oldest(void)
{
    unsigned i, best=0;
    for(i=0;i<SLOTS;i++) {
        if(!entries[i].data) return i;
        if(entries[i].stamp<entries[best].stamp) best=i;
    }
    return best;
}
void wine_nx_shader_ram_configure(size_t bytes)
{
    unsigned i;
    pthread_mutex_lock(&mutex);
    for(i=0;i<SLOTS;i++) evict(i);
    memset(&stats,0,sizeof(stats)); stats.budget=bytes; clock_stamp=0;
    pthread_mutex_unlock(&mutex);
}
void *wine_nx_shader_ram_get(const void *owner,const void *key,size_t key_size,size_t *size)
{
    void *data=NULL; unsigned i;
    if(size) *size=0;
    if(!key || !key_size || key_size>32) return NULL;
    pthread_mutex_lock(&mutex);
    if(stats.budget) {
        for(i=0;i<SLOTS;i++) if(entries[i].data && entries[i].owner==owner &&
            entries[i].key_size==key_size && !memcmp(entries[i].key,key,key_size)) {
            if((data=malloc(entries[i].size))) {
                memcpy(data,entries[i].data,entries[i].size);
                if(size) *size=entries[i].size;
                entries[i].stamp=++clock_stamp; stats.hits++;
            }
            break;
        }
        if(!data) stats.misses++;
    }
    pthread_mutex_unlock(&mutex); return data;
}
void wine_nx_shader_ram_put(const void *owner,const void *key,size_t key_size,const void *data,size_t size)
{
    unsigned i, slot; void *copy;
    if(!key || !key_size || key_size>32 || !data || !size || size>MAX_ITEM) return;
    pthread_mutex_lock(&mutex);
    if(!stats.budget || pressed || size>stats.budget) goto done;
    for(i=0;i<SLOTS;i++) if(entries[i].data && entries[i].owner==owner &&
        entries[i].key_size==key_size && !memcmp(entries[i].key,key,key_size)) { evict(i); break; }
    while(stats.bytes>stats.budget-size) {
        /* A free slot is not an eviction candidate for the byte limit. */
        unsigned best=SLOTS;
        for(i=0;i<SLOTS;i++) if(entries[i].data &&
            (best==SLOTS || entries[i].stamp<entries[best].stamp)) best=i;
        if(best==SLOTS) break;
        evict(best);
    }
    slot=oldest(); evict(slot);
    copy=malloc(size);
    if(!copy) goto done; /* Persistent backend still receives the original. */
    memcpy(copy,data,size); entries[slot].data=copy; entries[slot].size=size;
    entries[slot].owner=owner; entries[slot].key_size=key_size;
    memcpy(entries[slot].key,key,key_size); entries[slot].stamp=++clock_stamp;
    stats.bytes+=size; stats.puts++;
done:
    pthread_mutex_unlock(&mutex);
}
void wine_nx_shader_ram_forget(const void *owner)
{
    unsigned i;
    pthread_mutex_lock(&mutex);
    for(i=0;i<SLOTS;i++) if(entries[i].owner==owner) evict(i);
    pthread_mutex_unlock(&mutex);
}
size_t wine_nx_shader_ram_reclaim(size_t bytes)
{
    size_t released=0;
    if (!bytes || pthread_mutex_trylock(&mutex)) return 0;
    while (released < bytes) {
        unsigned best=SLOTS;
        for(unsigned i=0;i<SLOTS;i++) if(entries[i].data &&
            (best==SLOTS || entries[i].stamp<entries[best].stamp)) best=i;
        if(best==SLOTS) break;
        released+=entries[best].size; evict(best);
    }
    pthread_mutex_unlock(&mutex); return released;
}
void wine_nx_shader_ram_pressure(int low)
{
    unsigned i;
    pthread_mutex_lock(&mutex);
    if(low && !pressed) for(i=0;i<SLOTS;i++) evict(i);
    pressed=!!low;
    pthread_mutex_unlock(&mutex);
}
struct nx_shader_cache_stats wine_nx_shader_ram_stats(void)
{
    struct nx_shader_cache_stats result;
    pthread_mutex_lock(&mutex); result=stats; pthread_mutex_unlock(&mutex); return result;
}
