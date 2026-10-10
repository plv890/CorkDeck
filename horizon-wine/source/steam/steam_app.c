#include "steam_app.h"
#include "steam_settings.h"
#include <limits.h>
#include <stdlib.h>
/* What a game lists is many times what is installed: every DLC and every
 * system has depots of its own (Borderlands 2 lists more than a hundred). All
 * are read; only those to install have to fit the application's own table. */
#define STEAM_DEPOT_SEEN 1024
#include <string.h>
struct kv {
  const unsigned char *p, *end;
  char path[12][128];
  struct steam_app *app;
  struct steam_depot *all;
  size_t all_count;
  const char *language;
  struct {
    uint64_t id;
    char exe[256];
    int windows;
  } launches[16];
  size_t launch_count;
};
static void white(struct kv *k) {
  for (;;) {
    while (k->p < k->end && *k->p <= 32)
      ++k->p;
    if (k->end - k->p >= 2 && k->p[0] == '/' && k->p[1] == '/') {
      while (k->p < k->end && *k->p != '\n')
        ++k->p;
    } else
      break;
  }
}
static int quoted(struct kv *k, char *out, size_t cap) {
  size_t n = 0;
  white(k);
  if (k->p == k->end || *k->p++ != '"')
    return 0;
  while (k->p < k->end) {
    unsigned char c = *k->p++;
    if (c == '"') {
      out[n] = 0;
      return 1;
    }
    if (c == '\\') {
      if (k->p == k->end)
        return 0;
      c = *k->p++;
      if (c == 'n')
        c = '\n';
      else if (c == 't')
        c = '\t';
    }
    if (!c || n + 1 >= cap)
      return 0;
    out[n++] = (char)c;
  }
  return 0;
}
static int number(const char *s, uint64_t *out) {
  uint64_t v = 0;
  if (!*s)
    return 0;
  for (; *s; ++s) {
    unsigned int d = (unsigned char)*s - '0';
    if (d > 9 || v > (UINT64_MAX - d) / 10)
      return 0;
    v = v * 10 + d;
  }
  *out = v;
  return 1;
}
static int token(const char *s, const char *wanted) {
  size_t n = strlen(wanted);
  const char *p = s;
  while (*p) {
    const char *end = strchr(p, ',');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    const char *stop = p + len;
    size_t j;
    while (p < stop && (unsigned char)*p <= 32) ++p;
    while (stop > p && (unsigned char)stop[-1] <= 32) --stop;
    len = (size_t)(stop - p);
    for (j = 0; len == n && j < n; ++j) {
      unsigned char c = (unsigned char)p[j];
      if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
      if (c != (unsigned char)wanted[j]) break;
    }
    if (len == n && j == n) return 1;
    if (!end)
      break;
    p = end + 1;
  }
  return 0;
}
static int leaf(struct kv *k, unsigned int depth, const char *value) {
  const char *key = k->path[depth];
  struct steam_app *a = k->app;
  uint64_t id, v;
  size_t i;
  struct steam_depot *d;
  if (!strcmp(key,"installscript") &&
      ((depth==2 && !strcmp(k->path[1],"extended")) ||
       (depth==3 && !strcmp(k->path[1],"common") && !strcmp(k->path[2],"extended")))) {
    if (strlen(value)<sizeof(a->installscript))strcpy(a->installscript,value);
    return 1;
  }
  if (depth >= 4 && !strcmp(k->path[1], "config") &&
      !strcmp(k->path[2], "launch") && number(k->path[3], &id)) {
    for (i = 0; i < k->launch_count && k->launches[i].id != id; ++i) {
    }
    if (i == k->launch_count) {
      if (i == 16)
        return 1;
      k->launches[i].id = id;
      k->launches[i].windows = 1;
      k->launch_count++;
    }
    if (depth == 4 && !strcmp(key, "executable") &&
        strlen(value) < sizeof(k->launches[i].exe))
      strcpy(k->launches[i].exe, value);
    if (depth == 5 && !strcmp(k->path[4], "config") && !strcmp(key, "oslist"))
      k->launches[i].windows = token(value, "windows");
    return 1;
  }
  if (depth == 2 && !strcmp(k->path[1], "common")) {
    if (!strcmp(key, "name")) {
      size_t n = strlen(value);
      if (n >= sizeof(a->name))
        n = sizeof(a->name) - 1;
      memcpy(a->name, value, n);
      a->name[n] = 0;
    }
    if (!strcmp(key, "oslist"))
      a->windows = token(value, "windows");
  }
  if (depth < 3 || strcmp(k->path[1], "depots") || !number(k->path[2], &id) ||
      !id || id > UINT32_MAX)
    return 1;
  for (i = 0; i < k->all_count && k->all[i].id != (uint32_t)id; ++i) {
  }
  if (i == k->all_count) {
    if (k->all_count == STEAM_DEPOT_SEEN)
      return 0;
    k->all[k->all_count++] = (struct steam_depot){
        .id = (uint32_t)id, .source_app = a->id, .windows = 1, .language = 1};
  }
  d = &k->all[i];
  if (depth == 3) {
    if (!strcmp(key, "depotfromapp")) {
      if (!number(value, &v) || v > UINT32_MAX)
        return 0;
      d->source_app = (uint32_t)v;
    }
    if (!strcmp(key, "dlcappid")) {
      if (!number(value, &v) || v > UINT32_MAX)
        return 0;
      d->dlc_app = (uint32_t)v;
    }
    /* An optional depot is one not every package of the game contains: a
     * regional or low-violence copy, or the game's own programs kept apart
     * from its data. Whether this account has it is the licence's answer,
     * which the installer asks for; here it is a candidate like the others,
     * listed before them so that the game's ordinary files win a name both
     * have. It counts neither for the size told beforehand nor for which
     * languages and architectures the game is said to have. */
    if (!strcmp(key, "optional"))
      d->optional = strcmp(value, "0") != 0;
  }
  if (depth == 4 && !strcmp(k->path[3], "config")) {
    if (!strcmp(key, "oslist"))
      d->windows = token(value, "windows");
    if (!strcmp(key, "osarch")) {
      d->arch32 = !strcmp(value, "32");
      d->arch64 = !strcmp(value, "64");
      if (!d->arch32 && !d->arch64)
        return 0;
    }
    if (!strcmp(key, "language")) {
      const char *p = value;
      while (*p && (unsigned char)*p <= 32) ++p;
      d->language_specific = *p != 0;
      d->language = !d->language_specific || token(value, k->language);
      d->languages = steam_language_mask(value);
      d->english = d->language_specific && token(value, "english");
    }
  }
  if (depth == 5 && !strcmp(k->path[3], "manifests") &&
      !strcmp(k->path[4], "public")) {
    if (!strcmp(key, "gid")) {
      if (d->manifest || !number(value, &d->manifest) || !d->manifest)
        return 0;
    }
    if (!strcmp(key, "size")) {
      if (!number(value, &d->size))
        return 0;
    }
  }
  return 1;
}
static int object(struct kv *k, unsigned int depth) {
  char value[2048];
  if (depth >= 12)
    return 0;
  for (;;) {
    white(k);
    if (k->p == k->end)
      return 0;
    if (*k->p == '}') {
      ++k->p;
      return 1;
    }
    if (!quoted(k, k->path[depth], sizeof(k->path[depth])))
      return 0;
    white(k);
    if (k->p == k->end)
      return 0;
    if (*k->p == '{') {
      ++k->p;
      if (!object(k, depth + 1))
        return 0;
    } else if (!quoted(k, value, sizeof(value)) || !leaf(k, depth, value))
      return 0;
  }
}
static int parse(const void *data, size_t size, uint32_t id,
                 const char *language, struct steam_app *out, struct steam_depot *all) {
  struct steam_app a = {.id = id, .windows = -1};
  struct kv k = {
      .p = data, .end = (const unsigned char *)data + size, .app = &a, .all = all, .language = language};
  size_t i, n = 0;
  int has64 = 0, language_depots = 0, selected_language = 0;
  memset(out, 0, sizeof(*out));
  if (!size || size > 1024 * 1024 || !id || !language || !*language)
    return 0;
  if (k.end[-1] == 0)
    --k.end;
  if (!quoted(&k, k.path[0], sizeof(k.path[0])) || strcmp(k.path[0], "appinfo"))
    return 0;
  white(&k);
  if (k.p == k.end || *k.p++ != '{' || !object(&k, 1))
    return 0;
  white(&k);
  if (k.p != k.end)
    return 0;
  for (i = 0; i < k.all_count; ++i)
    if (all[i].windows && all[i].language && !all[i].dlc_app &&
        !all[i].optional && all[i].arch64)
      has64 = 1;
  for (i = 0; i < 2 * k.all_count; ++i) {
    struct steam_depot *d = &all[i % k.all_count];
    /* Two passes over the depots: the optional ones, then the rest. */
    if ((i < k.all_count) != (d->optional != 0))
      continue;
    if(d->windows && !d->dlc_app && !d->optional && !(has64 && d->arch32) && (d->manifest || d->source_app != id)){
      if (d->language_specific) {
        ++language_depots;if(d->language)++selected_language;
        if(d->english)a.english_available=1;
        a.available_languages |= d->languages;
      } else a.available_languages |= STEAM_LANGUAGE_BASE;
    }
    if (!d->windows || !d->language || d->dlc_app || (has64 && d->arch32))
      continue;
    if (!d->manifest && d->source_app == id)
      continue;
    if (a.total_size > UINT64_MAX - d->size)
      return 0;
    if (!d->optional)
      a.total_size += d->size;
    if (n == STEAM_DEPOT_LIMIT)
      return 0;
    a.depots[n++] = *d;
  }
  a.count = n;
  a.language_missing = language_depots && !selected_language &&
      (strcmp(language, STEAM_LANGUAGE_BASE_ID) || !(a.available_languages & STEAM_LANGUAGE_BASE));
  {
    uint64_t best = UINT64_MAX;
    for (i = 0; i < k.launch_count; ++i)
      if (k.launches[i].windows && k.launches[i].exe[0] &&
          k.launches[i].id < best) {
        best = k.launches[i].id;
        strcpy(a.executable, k.launches[i].exe);
      }
  }
  *out = a;
  return 1;
}
int steam_app_parse_language(const void *data, size_t size, uint32_t id,
                    const char *language, struct steam_app *out) {
  struct steam_depot *all = malloc(sizeof(*all) * STEAM_DEPOT_SEEN);
  int ok;
  if (!all) {
    memset(out, 0, sizeof(*out));
    return 0;
  }
  ok = parse(data, size, id, language, out, all);
  free(all);
  return ok;
}
int steam_app_parse(const void *data, size_t size, uint32_t id, struct steam_app *out) {
  return steam_app_parse_language(data, size, id, "english", out);
}
