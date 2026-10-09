#include "steam_wire.h"
#include <string.h>

void steam_secret_clear(void *data, size_t size)
{
    volatile unsigned char *p = data;
    while (size--) *p++ = 0;
}
static int varint(struct steam_wire_reader *r, uint64_t *out)
{
    unsigned int i;
    uint64_t value = 0;
    for (i = 0; i < 10; ++i) {
        unsigned int byte;
        if (r->offset == r->size) return 0;
        byte = r->data[r->offset++];
        if (i == 9 && byte > 1) return 0;
        value |= (uint64_t)(byte & 127) << (i * 7);
        if (!(byte & 128)) { *out = value; return 1; }
    }
    return 0;
}
int steam_wire_next(struct steam_wire_reader *r, struct steam_wire_field *f)
{
    uint64_t tag, length;
    size_t count, i;
    memset(f, 0, sizeof(*f));
    if (r->failed || r->offset > r->size || (!r->data && r->size)) goto bad;
    if (r->offset == r->size) return 0;
    if (!varint(r, &tag) || !(tag >> 3) || (tag >> 3) > 0x1fffffff) goto bad;
    f->number = (uint32_t)(tag >> 3);
    f->type = (unsigned int)(tag & 7);
    switch (f->type) {
    case 0: if (!varint(r, &f->integer)) goto bad; return 1;
    case 1: count = 8; break;
    case 5: count = 4; break;
    case 2:
        if (!varint(r, &length) || length > r->size - r->offset) goto bad;
        f->data = r->data + r->offset; f->size = (size_t)length;
        r->offset += f->size;
        return 1;
    default: goto bad;
    }
    if (count > r->size - r->offset) goto bad;
    for (i = 0; i < count; ++i) f->integer |= (uint64_t)r->data[r->offset++] << (i * 8);
    return 1;
bad:
    r->failed = 1;
    return -1;
}
static int append(struct steam_wire_writer *w, const void *data, size_t size)
{
    if (w->failed || w->size > w->capacity || size > w->capacity - w->size ||
        (!data && size) || (!w->data && size)) { w->failed = 1; return 0; }
    if (size) memcpy(w->data + w->size, data, size);
    w->size += size;
    return 1;
}
static int put_uint(struct steam_wire_writer *w, uint64_t value)
{
    unsigned char bytes[10];
    size_t n = 0;
    do { bytes[n] = value & 127; value >>= 7; if (value) bytes[n] |= 128; ++n; } while (value);
    return append(w, bytes, n);
}
static int tag(struct steam_wire_writer *w, uint32_t field, unsigned int type)
{
    if (!field || field > 0x1fffffff) { w->failed = 1; return 0; }
    return put_uint(w, ((uint64_t)field << 3) | type);
}
int steam_wire_uint(struct steam_wire_writer *w, uint32_t field, uint64_t value)
{
    return tag(w, field, 0) && put_uint(w, value);
}
int steam_wire_fixed64(struct steam_wire_writer *w, uint32_t field, uint64_t value)
{
    unsigned char data[8];
    size_t i;
    for (i = 0; i < 8; ++i) data[i] = (unsigned char)(value >> (i * 8));
    return tag(w, field, 1) && append(w, data, sizeof(data));
}
int steam_wire_bytes(struct steam_wire_writer *w, uint32_t field, const void *data, size_t size)
{
    return tag(w, field, 2) && put_uint(w, size) && append(w, data, size);
}
