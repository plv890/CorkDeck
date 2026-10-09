#define _POSIX_C_SOURCE 200809L
#include "steam_artwork.h"
#include "../launcher_image.h"
#include <curl/curl.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define ART_SLOTS 12
#define ART_BYTES (2u * 1024u * 1024u)
struct cover {
  uint32_t appid;
  unsigned int use;
  int state, width, height;
  struct launcher_icon decoded;
  SDL_Texture *texture;
};
struct steam_artwork {
  SDL_Thread *thread;
  SDL_mutex *mutex;
  SDL_cond *condition;
  SDL_atomic_t stop, paused;
  struct cover covers[ART_SLOTS];
  unsigned int use;
  char cache[512];
  uint64_t cache_bytes;
  unsigned int cache_count;
};
struct response {
  struct steam_artwork *owner;
  unsigned char *data;
  size_t size;
};
static int dir(const char *path) {
  struct stat st;
  if (!lstat(path, &st))
    return S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode);
  return errno == ENOENT && !mkdir(path, 0777);
}
static int regular(const char *path) {
  struct stat st;
  return !lstat(path, &st) && S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode) &&
         st.st_size > 0 && st.st_size <= ART_BYTES;
}
static size_t receive(char *data, size_t size, size_t count, void *opaque) {
  struct response *r = opaque;
  size_t n;
  if (size && count > SIZE_MAX / size)
    return 0;
  n = size * count;
  if (n > ART_BYTES - r->size)
    return 0;
  memcpy(r->data + r->size, data, n);
  r->size += n;
  return n;
}
static int cancelled(void *opaque, curl_off_t a, curl_off_t b, curl_off_t c,
                     curl_off_t d) {
  struct response *r = opaque;
  (void)a;
  (void)b;
  (void)c;
  (void)d;
  return SDL_AtomicGet(&r->owner->stop) || SDL_AtomicGet(&r->owner->paused);
}
static int run(void *opaque) {
  struct steam_artwork *p = opaque;
  CURL *curl = curl_easy_init();
  unsigned char *data = malloc(ART_BYTES);
  if (!curl || !data)
    goto done;
  while (!SDL_AtomicGet(&p->stop)) {
    unsigned int index, kind;
    uint32_t appid = 0;
    char path[560];
    struct launcher_icon icon = {0};
    int loaded = 0;
    SDL_LockMutex(p->mutex);
    for (;;) {
      if (SDL_AtomicGet(&p->stop))
        break;
      for (index = 0; index < ART_SLOTS; ++index)
        if (p->covers[index].state == 1) {
          if (SDL_AtomicGet(&p->paused)) {
            char cached[560];
            if (!p->cache[0])
              continue;
            snprintf(cached, sizeof(cached), "%s/%u.jpg", p->cache,
                     p->covers[index].appid);
            if (!regular(cached))
              continue;
          }
          appid = p->covers[index].appid;
          p->covers[index].state = 2;
          break;
        }
      if (appid)
        break;
      SDL_CondWaitTimeout(p->condition, p->mutex, 100);
    }
    SDL_UnlockMutex(p->mutex);
    if (!appid)
      break;
    path[0] = 0;
    if (p->cache[0])
      snprintf(path, sizeof(path), "%s/%u.jpg", p->cache, appid);
    if (path[0] && regular(path))
      loaded = launcher_image_load(path, &icon);
    for (kind = 0; kind < 2 && !loaded && !SDL_AtomicGet(&p->stop) &&
                   !SDL_AtomicGet(&p->paused);
         ++kind) {
      char url[256];
      long http = 0;
      struct response r = {p, data, 0};
      CURLcode code;
      snprintf(url, sizeof(url),
               "https://shared.fastly.steamstatic.com/store_item_assets/steam/"
               "apps/%u/%s",
               appid, kind ? "header.jpg" : "library_600x900.jpg");
      curl_easy_reset(curl);
#define OPT(k, v)                                                              \
  do {                                                                         \
    if (curl_easy_setopt(curl, k, v) != CURLE_OK)                              \
      goto request_failed;                                                     \
  } while (0)
      OPT(CURLOPT_URL, url);
      OPT(CURLOPT_PROXY, "");
      OPT(CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);
      OPT(CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
      OPT(CURLOPT_FOLLOWLOCATION, 0L);
      OPT(CURLOPT_SSL_VERIFYPEER, 1L);
      OPT(CURLOPT_SSL_VERIFYHOST, 2L);
      OPT(CURLOPT_NOSIGNAL, 1L);
      OPT(CURLOPT_CONNECTTIMEOUT, 5L);
      OPT(CURLOPT_TIMEOUT, 15L);
      OPT(CURLOPT_WRITEFUNCTION, receive);
      OPT(CURLOPT_WRITEDATA, &r);
      OPT(CURLOPT_NOPROGRESS, 0L);
      OPT(CURLOPT_XFERINFOFUNCTION, cancelled);
      OPT(CURLOPT_XFERINFODATA, &r);
      code = curl_easy_perform(curl);
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
      if (!code && http == 200 && r.size &&
          launcher_image_decode(data, r.size, &icon)) {
        loaded = 1;
        if (path[0] && !regular(path) &&
            p->cache_bytes + r.size <= 64u * 1024u * 1024u &&
            p->cache_count < 512) {
          char temporary[600];
          struct stat st;
          FILE *f;
          snprintf(temporary, sizeof(temporary), "%s.tmp", path);
          /* Existing malformed/unsafe entries are never overwritten. */
          if (lstat(path, &st) && errno == ENOENT && lstat(temporary, &st) &&
              errno == ENOENT && (f = fopen(temporary, "wb"))) {
            int ok = fwrite(data, 1, r.size, f) == r.size;
            if (fclose(f))
              ok = 0;
            if (ok && !rename(temporary, path)) {
              p->cache_bytes += r.size;
              ++p->cache_count;
            } else
              unlink(temporary);
          }
        }
      }
    request_failed:
      curl_easy_reset(curl);
#undef OPT
    }
    if (loaded && !launcher_icon_fit(&icon, 384)) {
      launcher_icon_free(&icon);
      loaded = 0;
    }
    SDL_LockMutex(p->mutex);
    if (p->covers[index].appid == appid) {
      if (loaded) {
        p->covers[index].decoded = icon;
        memset(&icon, 0, sizeof(icon));
        p->covers[index].state = 3;
      } else
        p->covers[index].state = SDL_AtomicGet(&p->paused) ? 1 : 4;
    }
    SDL_UnlockMutex(p->mutex);
    launcher_icon_free(&icon);
  }
done:
  if (curl)
    curl_easy_cleanup(curl);
  free(data);
  return 0;
}
struct steam_artwork *steam_artwork_create(const char *root) {
  struct steam_artwork *p = calloc(1, sizeof(*p));
  char steam[512];
  DIR *directory;
  struct dirent *entry;
  int n;
  if (!p)
    return NULL;
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
    free(p);
    return NULL;
  }
  p->mutex = SDL_CreateMutex();
  p->condition = SDL_CreateCond();
  n = snprintf(steam, sizeof(steam), "%s/steam", root);
  if (n > 0 && (size_t)n < sizeof(steam) && dir(steam)) {
    n = snprintf(p->cache, sizeof(p->cache), "%s/artwork", steam);
    if (n < 0 || (size_t)n >= sizeof(p->cache) || !dir(p->cache))
      p->cache[0] = 0;
  }
  if (p->cache[0] && (directory = opendir(p->cache))) {
    while ((entry = readdir(directory))) {
      char path[560], *end;
      unsigned long appid;
      struct stat st;
      errno = 0;
      appid = strtoul(entry->d_name, &end, 10);
      if (errno || !appid || appid > UINT32_MAX || end == entry->d_name ||
          strcmp(end, ".jpg"))
        continue;
      n = snprintf(path, sizeof(path), "%s/%s", p->cache, entry->d_name);
      if (n > 0 && (size_t)n < sizeof(path) && regular(path) &&
          !lstat(path, &st)) {
        p->cache_bytes += st.st_size;
        ++p->cache_count;
      }
    }
    closedir(directory);
  }
  if (p->mutex && p->condition)
    p->thread =
        SDL_CreateThreadWithStackSize(run, "steam-art", 256u * 1024u, p);
  if (!p->thread) {
    steam_artwork_destroy(p);
    return NULL;
  }
  return p;
}
void steam_artwork_request(struct steam_artwork *p, uint32_t appid) {
  unsigned int i;
  int chosen = -1;
  unsigned int oldest = ~0u;
  if (!p || !appid)
    return;
  SDL_LockMutex(p->mutex);
  for (i = 0; i < ART_SLOTS; ++i) {
    if (p->covers[i].appid == appid) {
      p->covers[i].use = ++p->use;
      SDL_UnlockMutex(p->mutex);
      return;
    }
    if (p->covers[i].state != 2 && p->covers[i].use < oldest) {
      oldest = p->covers[i].use;
      chosen = (int)i;
    }
  }
  if (chosen >= 0) {
    struct cover *c = &p->covers[chosen];
    if (c->texture)
      SDL_DestroyTexture(c->texture);
    launcher_icon_free(&c->decoded);
    *c = (struct cover){.appid = appid, .use = ++p->use, .state = 1};
    SDL_CondSignal(p->condition);
  }
  SDL_UnlockMutex(p->mutex);
}
void steam_artwork_pause(struct steam_artwork *p, int paused) {
  if (p) {
    SDL_AtomicSet(&p->paused, paused);
    SDL_CondSignal(p->condition);
  }
}
void steam_artwork_tick(struct steam_artwork *p, SDL_Renderer *renderer) {
  unsigned int i;
  if (!p)
    return;
  for (i = 0; i < ART_SLOTS; ++i) {
    struct launcher_icon icon = {0};
    SDL_Surface *surface;
    SDL_LockMutex(p->mutex);
    if (p->covers[i].decoded.data) {
      icon = p->covers[i].decoded;
      memset(&p->covers[i].decoded, 0, sizeof(icon));
    }
    SDL_UnlockMutex(p->mutex);
    if (!icon.data)
      continue;
    surface = SDL_CreateRGBSurfaceWithFormatFrom(
        icon.data, icon.width, icon.height, 32, icon.width * 4,
        SDL_PIXELFORMAT_RGBA32);
    if (surface) {
      p->covers[i].texture = SDL_CreateTextureFromSurface(renderer, surface);
      SDL_FreeSurface(surface);
    }
    p->covers[i].width = icon.width;
    p->covers[i].height = icon.height;
    launcher_icon_free(&icon);
  }
}
SDL_Texture *steam_artwork_texture(struct steam_artwork *p, uint32_t appid,
                                   int *w, int *h) {
  unsigned int i;
  if (!p)
    return NULL;
  for (i = 0; i < ART_SLOTS; ++i)
    if (p->covers[i].appid == appid && p->covers[i].texture) {
      *w = p->covers[i].width;
      *h = p->covers[i].height;
      return p->covers[i].texture;
    }
  return NULL;
}
void steam_artwork_destroy(struct steam_artwork *p) {
  unsigned int i;
  if (!p)
    return;
  SDL_AtomicSet(&p->stop, 1);
  if (p->condition)
    SDL_CondSignal(p->condition);
  if (p->thread)
    SDL_WaitThread(p->thread, NULL);
  for (i = 0; i < ART_SLOTS; ++i) {
    if (p->covers[i].texture)
      SDL_DestroyTexture(p->covers[i].texture);
    launcher_icon_free(&p->covers[i].decoded);
  }
  if (p->condition)
    SDL_DestroyCond(p->condition);
  if (p->mutex)
    SDL_DestroyMutex(p->mutex);
  free(p);
}
