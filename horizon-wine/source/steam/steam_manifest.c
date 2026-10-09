#include "steam_manifest.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static uint32_t u32(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static int read32(FILE *f, uint32_t *v) {
  unsigned char b[4];
  if (fread(b, 1, 4, f) != 4)
    return 0;
  *v = u32(b);
  return 1;
}
void steam_manifest_close(struct steam_manifest *m) {
  if (m->file)
    fclose(m->file);
  free(m->mapping);
  steam_secret_clear(m, sizeof(*m));
}
int steam_manifest_open(struct steam_manifest *m, const char *path,
                        uint32_t depot, uint64_t manifest,
                        const unsigned char key[32]) {
  uint32_t magic, size;
  unsigned int seen = 0;
  unsigned char metadata[4096];
  int ok = 0;
  memset(m, 0, sizeof(*m));
  m->file = fopen(path, "rb");
  if (!m->file)
    return 0;
  for (;;) {
    if (!read32(m->file, &magic))
      goto done;
    if (magic == 0x32c415ab) {
      if (seen != 7 || fgetc(m->file) != EOF || ferror(m->file))
        goto done;
      break;
    }
    if (!read32(m->file, &size) || size > 128u * 1024u * 1024u)
      goto done;
    if (magic == 0x71f617d0) {
      if (seen & 1)
        goto done;
      seen |= 1;
      m->payload_offset = (uint64_t)ftell(m->file);
      m->payload_size = size;
      if (fseek(m->file, size, SEEK_CUR))
        goto done;
    } else if (magic == 0x1f4812be) {
      struct steam_wire_reader r;
      struct steam_wire_field f;
      unsigned int fields = 0;
      int n;
      if ((seen & 2) || size > sizeof(metadata) ||
          fread(metadata, 1, size, m->file) != size)
        goto done;
      seen |= 2;
      r = (struct steam_wire_reader){metadata, size, 0, 0};
      while ((n = steam_wire_next(&r, &f)) == 1) {
        if (f.number != 1 && f.number != 2 && f.number != 4 && f.number != 5)
          continue;
        if ((fields & (1u << f.number)) || f.type != 0)
          goto done;
        fields |= 1u << f.number;
        if (f.number == 1) {
          if (f.integer > UINT32_MAX)
            goto done;
          m->depot = (uint32_t)f.integer;
        }
        if (f.number == 2)
          m->manifest = f.integer;
        if (f.number == 4) {
          if (f.integer > 1)
            goto done;
          m->encrypted = (int)f.integer;
        }
        if (f.number == 5)
          m->total_size = f.integer;
      }
      if (n < 0 || m->depot != depot || m->manifest != manifest)
        goto done;
    } else if (magic == 0x1b81b817) {
      if (seen & 4 || size > 4096)
        goto done;
      seen |= 4;
      if (fseek(m->file, size, SEEK_CUR))
        goto done;
    } else
      goto done;
  }
  if (fseek(m->file, (long)m->payload_offset, SEEK_SET))
    goto done;
  m->remaining = m->payload_size;
  memcpy(m->key, key, 32);
  ok = 1;
done:
  steam_secret_clear(metadata, sizeof(metadata));
  if (!ok)
    steam_manifest_close(m);
  return ok;
}
static int var(FILE *f, uint64_t *remaining, uint64_t *v) {
  unsigned int i;
  *v = 0;
  for (i = 0; i < 10; ++i) {
    int b;
    if (!*remaining || (b = fgetc(f)) == EOF)
      return 0;
    --*remaining;
    if (i == 9 && b > 1)
      return 0;
    *v |= (uint64_t)(b & 127) << (i * 7);
    if (!(b & 128))
      return 1;
  }
  return 0;
}
int steam_manifest_next(struct steam_manifest *m,
                        struct steam_manifest_file *file) {
  uint64_t tag, size;
  struct steam_wire_reader r;
  struct steam_wire_field f;
  unsigned int seen = 0;
  int n;
  const unsigned char *name = NULL;
  size_t name_size = 0;
  memset(file, 0, sizeof(*file));
  if (!m->remaining)
    return 0;
  if (!var(m->file, &m->remaining, &tag) || tag != 10 ||
      !var(m->file, &m->remaining, &size) || size > m->remaining ||
      size > 4u * 1024u * 1024u)
    return -1;
  if (size > m->mapping_size) {
    unsigned char *p = realloc(m->mapping, (size_t)size);
    if (!p)
      return -1;
    m->mapping = p;
    m->mapping_size = (size_t)size;
  }
  if (fread(m->mapping, 1, (size_t)size, m->file) != size)
    return -1;
  m->remaining -= size;
  r = (struct steam_wire_reader){m->mapping, (size_t)size, 0, 0};
  while ((n = steam_wire_next(&r, &f)) == 1) {
    if (f.number == 6)
      continue; /* Repeated chunks are validated on iteration. */
    if (f.number > 7)
      continue;
    if (seen & (1u << f.number))
      return -1;
    seen |= 1u << f.number;
    if (f.number == 1) {
      if (f.type != 2 || !f.size)
        return -1;
      name = f.data;
      name_size = f.size;
    }
    if (f.number == 2) {
      if (f.type != 0 || f.integer > INT64_MAX)
        return -1;
      file->size = f.integer;
    }
    if (f.number == 3) {
      if (f.type != 0 || f.integer > UINT32_MAX)
        return -1;
      file->flags = (uint32_t)f.integer;
    }
    if (f.number == 5) {
      if (f.type != 2 || f.size != 20)
        return -1;
      memcpy(file->sha, f.data, 20);
    }
    if (f.number == 7 && f.size)
      return -1; /* No symlink writes on SD. */
  }
  if (n < 0 || !name || (file->flags & 512))
    return -1;
  if (m->encrypted) {
    if (!steam_content_filename(m->key, name, name_size, file->path))
      return -1;
  } else {
    if (name_size >= sizeof(file->path) || memchr(name, 0, name_size))
      return -1;
    memcpy(file->path, name, name_size);
    if (!steam_content_path(file->path))
      return -1;
  }
  /* Empty placeholder files may omit sha_content. Nonempty files must have it. */
  if (!(file->flags & 64) && file->size && !(seen & (1u << 5)))
    return -1;
  file->chunks = (struct steam_wire_reader){m->mapping, (size_t)size, 0, 0};
  return 1;
}
int steam_manifest_chunk(struct steam_manifest_file *file,
                         struct steam_chunk *chunk) {
  struct steam_wire_field f, item;
  struct steam_wire_reader r;
  unsigned int seen = 0;
  int n;
  memset(chunk, 0, sizeof(*chunk));
  while ((n = steam_wire_next(&file->chunks, &f)) == 1) {
    if (f.number != 6)
      continue;
    if (f.type != 2 || f.size > 128)
      return -1;
    r = (struct steam_wire_reader){f.data, f.size, 0, 0};
    while ((n = steam_wire_next(&r, &item)) == 1) {
      if (item.number > 5)
        continue;
      if (seen & (1u << item.number))
        return -1;
      seen |= 1u << item.number;
      if (item.number == 1) {
        if (item.type != 2 || item.size != 20)
          return -1;
        memcpy(chunk->sha, item.data, 20);
      } else if (item.number == 2) {
        if (item.type != 5)
          return -1;
        chunk->checksum = (uint32_t)item.integer;
      } else {
        if (item.type != 0)
          return -1;
        if (item.number == 3)
          chunk->offset = item.integer;
        if (item.number == 4) {
          if (item.integer > STEAM_CHUNK_LIMIT)
            return -1;
          chunk->original = (uint32_t)item.integer;
        }
        if (item.number == 5) {
          if (item.integer > STEAM_CHUNK_LIMIT + 64)
            return -1;
          chunk->compressed = (uint32_t)item.integer;
        }
      }
    }
    if (n < 0 || !(seen & 2) || !chunk->original || !chunk->compressed ||
        chunk->offset > file->size ||
        chunk->original > file->size - chunk->offset)
      return -1;
    return 1;
  }
  return n < 0 ? -1 : 0;
}
