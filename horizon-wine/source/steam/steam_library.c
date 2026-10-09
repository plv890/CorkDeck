#include "steam_library.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void steam_library_clear(struct steam_library *l) {
  free(l->games);
  memset(l, 0, sizeof(*l));
}
static int game(const struct steam_wire_field *outer, struct steam_game *g, int family) {
  struct steam_wire_reader r = {outer->data, outer->size, 0, 0};
  struct steam_wire_field f;
  int n;
  unsigned int seen = 0;
  if (outer->type != 2 || outer->size > 4096)
    return 0;
  memset(g, 0, sizeof(*g));
  g->shared = family;
  while ((n = steam_wire_next(&r, &f)) == 1) {
    if (family && f.number == 2) {
      size_t i;
      if (f.type == 1) { if (f.integer && !g->lender) g->lender = f.integer; }
      else if (f.type == 2 && f.size % 8 == 0 && f.size <= 32 * 8) {
        for (i = 0; i < f.size; i += 8) {
          uint64_t owner = 0; unsigned int j;
          for (j = 0; j < 8; ++j) owner |= (uint64_t)f.data[i+j] << (j*8);
          if (owner && !g->lender) g->lender = owner;
        }
      } else return 0;
      continue;
    }
    if (family && f.number == 10) {
      if (seen & 8 || f.type != 0 || f.integer > UINT32_MAX) return 0;
      seen |= 8; g->excluded = (unsigned int)f.integer; continue;
    }
    unsigned int bit = f.number == 1   ? 1
                       : f.number == (family ? 6u : 2u) ? 2
                       : f.number == (family ? 9u : 5u) ? 4
                                       : 0;
    size_t i, len;
    if (!bit)
      continue;
    if (seen & bit)
      return 0;
    seen |= bit;
    if (bit == 1) {
      if (f.type != 0 || !f.integer || f.integer > INT32_MAX)
        return 0;
      g->app_id = (uint32_t)f.integer;
    } else if (bit == 2) {
      if (f.type != 2 || memchr(f.data, 0, f.size))
        return 0;
      len = f.size < sizeof(g->name) - 1 ? f.size : sizeof(g->name) - 1;
      if (len < f.size)
        while (len && (f.data[len] & 0xc0) == 0x80)
          --len;
      memcpy(g->name, f.data, len);
      g->name[len] = 0;
      for (i = 0; i < len; ++i)
        if ((unsigned char)g->name[i] < 32)
          g->name[i] = ' ';
    } else {
      if (f.type != 2 || (f.size != 0 && f.size != 40))
        return 0;
      for (i = 0; i < f.size; ++i)
        if (!((f.data[i] >= '0' && f.data[i] <= '9') ||
              (f.data[i] >= 'a' && f.data[i] <= 'f')))
          return 0;
      memcpy(g->icon_hash, f.data, f.size);
    }
  }
  if (!g->name[0])
    snprintf(g->name, sizeof(g->name), "App %u", g->app_id);
  return n == 0 && (seen & 1);
}
static int by_id(const void *a, const void *b) {
  uint32_t x = ((const struct steam_game *)a)->app_id,
           y = ((const struct steam_game *)b)->app_id;
  return (x > y) - (x < y);
}
static int by_name(const void *a, const void *b) {
  return strcmp(((const struct steam_game *)a)->name,
                ((const struct steam_game *)b)->name);
}
enum steam_library_error steam_library_read(const struct steam_cm_message *m,
                                            uint64_t account,
                                            struct steam_library *out) {
  struct steam_wire_reader r;
  struct steam_wire_field f;
  struct steam_library l = {.account = account};
  size_t count = 0, i = 0;
  uint64_t declared = 0;
  int n, seen = 0;
  if (m->type != 147 || m->result != 1 || !account)
    return STEAM_LIBRARY_PROTOCOL;
  r = (struct steam_wire_reader){m->body, m->body_size, 0, 0};
  while ((n = steam_wire_next(&r, &f)) == 1) {
    if (f.number == 1) {
      if (seen++ || f.type != 0)
        return STEAM_LIBRARY_PROTOCOL;
      declared = f.integer;
    }
    if (f.number == 2) {
      if (f.type != 2)
        return STEAM_LIBRARY_PROTOCOL;
      if (++count > STEAM_LIBRARY_LIMIT)
        return STEAM_LIBRARY_LIMIT_ERROR;
    }
  }
  if (n < 0 || declared != count)
    return STEAM_LIBRARY_PROTOCOL;
  if (count) {
    l.games = calloc(count, sizeof(*l.games));
    if (!l.games)
      return STEAM_LIBRARY_MEMORY;
  }
  l.count = count;
  r = (struct steam_wire_reader){m->body, m->body_size, 0, 0};
  while (steam_wire_next(&r, &f) == 1)
    if (f.number == 2 && !game(&f, &l.games[i++], 0)) {
      steam_library_clear(&l);
      return STEAM_LIBRARY_PROTOCOL;
    }
  if (count > 1) {
    qsort(l.games, count, sizeof(*l.games), by_id);
    for (i = 1; i < count; ++i)
      if (l.games[i - 1].app_id == l.games[i].app_id) {
        steam_library_clear(&l);
        return STEAM_LIBRARY_PROTOCOL;
      }
    qsort(l.games, count, sizeof(*l.games), by_name);
  }
  steam_library_clear(out);
  *out = l;
  return STEAM_LIBRARY_OK;
}
enum steam_library_error steam_library_merge_family(const struct steam_cm_message *m, struct steam_library *owned) {
  struct steam_wire_reader r;
  struct steam_wire_field f;
  struct steam_library family = {0}, merged = {.account = owned->account};
  size_t count = 0, i = 0, j = 0, used = 0, capacity;
  int n;
  if (m->type != 147 || m->result != 1 || !owned->account) return STEAM_LIBRARY_PROTOCOL;
  r = (struct steam_wire_reader){m->body, m->body_size, 0, 0};
  while ((n = steam_wire_next(&r, &f)) == 1) if (f.number == 1) {
    if (f.type != 2) return STEAM_LIBRARY_PROTOCOL;
    if (++count > STEAM_LIBRARY_LIMIT) return STEAM_LIBRARY_LIMIT_ERROR;
  }
  if (n < 0) return STEAM_LIBRARY_PROTOCOL;
  family.games = calloc(count ? count : 1, sizeof(*family.games));
  if (!family.games) return STEAM_LIBRARY_MEMORY;
  r = (struct steam_wire_reader){m->body, m->body_size, 0, 0};
  while (steam_wire_next(&r, &f) == 1) if (f.number == 1) {
    struct steam_game g;
    if (!game(&f, &g, 1)) { steam_library_clear(&family); return STEAM_LIBRARY_PROTOCOL; }
    if (!g.excluded && g.lender) family.games[family.count++] = g;
  }
  qsort(family.games, family.count, sizeof(*family.games), by_id);
  for (i = 1; i < family.count; ++i) if (family.games[i-1].app_id == family.games[i].app_id) {
    steam_library_clear(&family); return STEAM_LIBRARY_PROTOCOL;
  }
  /* Allocate a bounded temporary result. Preserve the caller on any failure. */
  capacity=owned->count+family.count;
  if(capacity>STEAM_LIBRARY_LIMIT)capacity=STEAM_LIBRARY_LIMIT;
  merged.games = calloc(capacity?capacity:1, sizeof(*merged.games));
  if (!merged.games) { steam_library_clear(&family); return STEAM_LIBRARY_MEMORY; }
  for (i = 0; i < owned->count; ++i) merged.games[used++] = owned->games[i];
  qsort(merged.games, used, sizeof(*merged.games), by_id);
  for (i = 0; i < family.count; ++i) {
    struct steam_game *g = &family.games[i];
    while (j < owned->count && merged.games[j].app_id < g->app_id) ++j;
    if (j < owned->count && merged.games[j].app_id == g->app_id) continue;
    if (used == STEAM_LIBRARY_LIMIT) {
      steam_library_clear(&family); steam_library_clear(&merged); return STEAM_LIBRARY_LIMIT_ERROR;
    }
    merged.games[used++] = *g;
  }
  merged.count = used;
  qsort(merged.games, used, sizeof(*merged.games), by_name);
  steam_library_clear(&family); steam_library_clear(owned); *owned = merged;
  return STEAM_LIBRARY_OK;
}
static unsigned char lower(unsigned char c) {
  return c >= 'A' && c <= 'Z' ? c + 32 : c;
}
int steam_library_matches(const struct steam_game *g, const char *search) {
  const char *p;
  char id[16];
  size_t i, n = strlen(search);
  if (!n)
    return 1;
  snprintf(id, sizeof(id), "%u", g->app_id);
  if (strstr(id, search))
    return 1;
  for (p = g->name; *p; ++p) {
    for (i = 0; i < n && p[i] &&
                lower((unsigned char)p[i]) == lower((unsigned char)search[i]);
         ++i) {
    }
    if (i == n)
      return 1;
  }
  return 0;
}
size_t steam_library_page(const struct steam_library *l, const char *search,
                          size_t offset, struct steam_game *out,
                          size_t capacity, size_t *total) {
  size_t i, n = 0, found = 0;
  for (i = 0; i < l->count; ++i)
    if (steam_library_matches(&l->games[i], search)) {
      if (found++ >= offset && n < capacity)
        out[n++] = l->games[i];
    }
  *total = found;
  return n;
}
