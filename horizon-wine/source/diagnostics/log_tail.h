/* Bounded in-memory log tail. Caller serializes append/copy. */
#ifndef WINE_NX_LOG_TAIL_H
#define WINE_NX_LOG_TAIL_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define WINE_NX_LOG_TAIL_CAPACITY 16384
struct wine_nx_log_tail {
    unsigned char data[WINE_NX_LOG_TAIL_CAPACITY];
    size_t end, used;
    uint64_t sequence;
};
static inline void wine_nx_log_tail_append( struct wine_nx_log_tail *tail, const void *data, size_t size )
{
    const unsigned char *bytes = data;
    size_t first;
    if (!size) return;
    if (size > sizeof(tail->data)) { bytes += size - sizeof(tail->data); size = sizeof(tail->data); }
    first = sizeof(tail->data) - tail->end;
    if (first > size) first = size;
    memcpy( tail->data + tail->end, bytes, first );
    memcpy( tail->data, bytes + first, size - first );
    tail->end = (tail->end + size) % sizeof(tail->data);
    tail->used += size;
    if (tail->used > sizeof(tail->data)) tail->used = sizeof(tail->data);
    tail->sequence++;
}
static inline size_t wine_nx_log_tail_copy( const struct wine_nx_log_tail *tail, void *out, size_t capacity )
{
    size_t size = tail->used < capacity ? tail->used : capacity;
    size_t start = (tail->end + sizeof(tail->data) - size) % sizeof(tail->data);
    size_t first = sizeof(tail->data) - start;
    if (first > size) first = size;
    memcpy( out, tail->data + start, first );
    memcpy( (unsigned char *)out + first, tail->data, size - first );
    return size;
}
#endif
