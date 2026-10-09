#ifndef AUTORUN_STEAM_WIRE_H
#define AUTORUN_STEAM_WIRE_H
#include <stddef.h>
#include <stdint.h>

/* Allocation-free protobuf subset used by Steam CM. Borrowed slices must not
 * outlive their packet. No recursive parsing, groups, logging or secret copies. */
struct steam_wire_reader { const unsigned char *data; size_t size, offset; int failed; };
struct steam_wire_field {
    uint32_t number;
    unsigned int type;
    uint64_t integer;
    const unsigned char *data;
    size_t size;
};
struct steam_wire_writer { unsigned char *data; size_t capacity, size; int failed; };
/* 1 field, 0 clean end, -1 invalid. Unknown supported fields can be skipped. */
int steam_wire_next(struct steam_wire_reader *, struct steam_wire_field *);
int steam_wire_uint(struct steam_wire_writer *, uint32_t field, uint64_t value);
int steam_wire_fixed64(struct steam_wire_writer *, uint32_t field, uint64_t value);
int steam_wire_bytes(struct steam_wire_writer *, uint32_t field, const void *, size_t);
void steam_secret_clear(void *, size_t);
#endif
