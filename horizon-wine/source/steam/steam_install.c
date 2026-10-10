#define _POSIX_C_SOURCE 200809L
#include "steam_install.h"
#include "steam_manifest.h"
#include "steam_engine.h"
#include "steam_hash.h"
#include "steam_cdn.h"
#include "steam_diagnostics.h"
#include "steam_storage.h"
#include "steam_settings.h"
#include <SDL.h>
#include <curl/curl.h>
#include <errno.h>
#include <mbedtls/sha1.h>
#include <minizip/unzip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
enum {
  INSTALL_PROTOCOL = 3100,
  INSTALL_IO,
  INSTALL_DISK,
  INSTALL_CORRUPT,
  INSTALL_PATH,
  INSTALL_UNSUPPORTED,
  INSTALL_CDN,
  INSTALL_CANCELLED
};
/* 1000 + Steam's result 15: the account has no licence for what was asked. */
#define STEAM_ACCESS_DENIED 1015u
/* One worker owns this handle for an entire installation. Resetting options
 * retains libcurl's connection/DNS/TLS caches without retaining request data. */
struct cdn_session {
  CURL *curl;
  struct steam_cdn *pool;
  size_t preferred;
  uint32_t depot;
  uint64_t started, rate_tick, rate_bytes;
  char tokens[8][1024];
  unsigned int token_generation[8];
};
struct transfer {
  struct cdn_session *cdn;
  struct steam_backend *backend;
  uint32_t app, depot;
  const struct steam_install_callbacks *cb;
  struct steam_download_view *view;
  FILE *file;
  unsigned char *memory;
  size_t capacity, used;
  int io_error;
};
static int tick(const struct steam_install_callbacks *cb) {
  return !cb->tick || cb->tick(cb->context);
}
static void progress(const struct steam_install_callbacks *cb,
                     struct steam_download_view *v) {
  if (cb->progress)
    cb->progress(cb->context, v);
}
static uint64_t millis(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
/* The log on the card is written by its own thread: with the card busy a
 * write can take seconds, and for that long nothing would read the sockets. */
struct install_observer {
  const struct steam_install_callbacks *original;
  const char *root;
  uint64_t last;
  enum steam_download_state state;
  unsigned int error;
  SDL_mutex *mutex;
  SDL_cond *wake;
  SDL_Thread *thread;
  struct steam_download_view pending;
  int has, stop;
};
static int observe_tick(void *opaque){
  struct install_observer *o=opaque;return tick(o->original);
}
static int observe_log(void *opaque){
  struct install_observer *o=opaque;
  SDL_LockMutex(o->mutex);
  for(;;){
    struct steam_download_view view;unsigned int error;
    while(!o->has&&!o->stop)SDL_CondWait(o->wake,o->mutex);
    if(!o->has)break;
    view=o->pending;o->has=0;
    SDL_UnlockMutex(o->mutex);
    error=steam_download_diagnostic(o->root,&view);
    SDL_LockMutex(o->mutex);
    o->error=error;
  }
  SDL_UnlockMutex(o->mutex);
  return 0;
}
static void observe_start(struct install_observer *o){
  o->mutex=SDL_CreateMutex();o->wake=SDL_CreateCond();
  if(o->mutex&&o->wake)
    o->thread=SDL_CreateThreadWithStackSize(observe_log,"steam-log",64u*1024u,o);
}
/* After this the remaining, final states are written by the caller itself. */
static void observe_stop(struct install_observer *o){
  if(o->thread){
    SDL_LockMutex(o->mutex);o->stop=1;SDL_CondSignal(o->wake);SDL_UnlockMutex(o->mutex);
    SDL_WaitThread(o->thread,NULL);o->thread=NULL;
  }
  if(o->wake)SDL_DestroyCond(o->wake);
  if(o->mutex)SDL_DestroyMutex(o->mutex);
  o->wake=NULL;o->mutex=NULL;
}
static void observe_progress(void *opaque,const struct steam_download_view *v){
  struct install_observer *o=opaque;struct steam_download_view copy=*v;
  uint64_t time=millis();
  if(!o->last||time-o->last>=5000||
     (v->state>=STEAM_DOWNLOAD_COMPLETE&&o->state!=v->state)){
    if(o->thread){
      SDL_LockMutex(o->mutex);o->pending=*v;o->has=1;SDL_CondSignal(o->wake);
      copy.diagnostic_error=o->error;SDL_UnlockMutex(o->mutex);
    }else copy.diagnostic_error=o->error=steam_download_diagnostic(o->root,v);
    o->last=time;o->state=v->state;
  }else if(o->thread){
    SDL_LockMutex(o->mutex);copy.diagnostic_error=o->error;SDL_UnlockMutex(o->mutex);
  }else copy.diagnostic_error=o->error;
  if(o->original->progress)o->original->progress(o->original->context,&copy);
}
static int path(char *out, size_t cap, const char *root, const char *suffix) {
  int n = snprintf(out, cap, "%s/%s", root, suffix);
  return n > 0 && (size_t)n < cap;
}
static int mkdirs(const char *name) {
  char buf[1024];
  char *p;
  struct stat st;
  size_t n = strlen(name);
  if (n >= sizeof(buf))
    return 0;
  memcpy(buf, name, n + 1);
  /* A libnx mount prefix is not a directory to create. */
  p = strchr(buf, ':');
  p = p && p[1] == '/' ? p + 2 : buf + 1;
  for (;; ++p)
    if (*p == '/' || !*p) {
      char c = *p;
      *p = 0;
      if (lstat(buf, &st)) {
        if (errno != ENOENT || mkdir(buf, 0777))
          return 0;
      } else if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
        return 0;
      *p = c;
      if (!c)
        break;
    }
  return 1;
}
static int parents(const char *name) {
  char p[1024], *end;
  if (strlen(name) >= sizeof(p))
    return 0;
  strcpy(p, name);
  end = strrchr(p, '/');
  if (!end)
    return 0;
  *end = 0;
  return mkdirs(p);
}
static int regular_or_missing(const char *name) {
  struct stat st;
  if (!lstat(name, &st))
    return S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode);
  return errno == ENOENT;
}
static size_t write_data(char *data, size_t size, size_t count, void *opaque) {
  struct transfer *t = opaque;
  size_t n;
  if (size && count > SIZE_MAX / size)
    return 0;
  n = size * count;
  if (n > t->capacity - t->used)
    return 0;
  if (t->file) {
    if (fwrite(data, 1, n, t->file) != n) {
      t->io_error = errno ? errno : EIO;
      return 0;
    }
  } else
    memcpy(t->memory + t->used, data, n);
  t->used += n;
  t->view->network_bytes += n;
  return n;
}
static void sample_rate(struct transfer *t) {
    uint64_t now=millis(),elapsed=now-t->cdn->rate_tick;
    t->view->elapsed_ms=now-t->cdn->started;
    if(elapsed>=1000){
      t->view->bytes_per_second=(double)(t->view->network_bytes-t->cdn->rate_bytes)*1000/(double)elapsed;
      t->cdn->rate_tick=now;t->cdn->rate_bytes=t->view->network_bytes;
    }
}
static int transfer_tick(void *opaque, curl_off_t a, curl_off_t b, curl_off_t c,
                         curl_off_t d) {
  struct transfer *t = opaque;
  (void)a;(void)b;(void)c;(void)d;
  sample_rate(t);
  progress(t->cb, t->view);
  return !tick(t->cb);
}
static int fetch(const char hosts[8][128], size_t count, const char *resource,
                 struct transfer *t) {
  size_t i, host;
  unsigned int attempt;
  char url[2048] = {0}, token[1024] = {0};
  if (!count || count > 8 || !t->cdn) {
    t->view->error = INSTALL_CDN;
    return 0;
  }
  if (t->cdn->depot != t->depot) {
    steam_secret_clear(t->cdn->tokens, sizeof(t->cdn->tokens));
    t->cdn->depot = t->depot;
  }
  if (!t->cdn->curl) t->cdn->curl = curl_easy_init();
  if (!t->cdn->curl) { t->view->error = 5; return 0; }
#define RETURN(value)                                                          \
  do {                                                                         \
    steam_secret_clear(url, sizeof(url));                                      \
    steam_secret_clear(token, sizeof(token));                                  \
    return (value);                                                            \
  } while (0)
  for (attempt = 0; attempt < 2; ++attempt)
    for (i = 0; i < count; ++i) {
      CURL *curl = t->cdn->curl;
      CURLcode rc;
      uint64_t started;
      long status = 0, connections = 0;
      double first_byte = 0, total_time = 0;
      int n;
      int authorized = 0;
      host = (t->cdn->preferred + i) % count;
      memcpy(token, t->cdn->tokens[host], sizeof(token));
    retry_host:
      if (!tick(t->cb)) {
        t->view->error = INSTALL_CANCELLED;
        RETURN(0);
      }
      t->used = 0;
      t->io_error = 0;
      if (t->file && (fflush(t->file) || ftruncate(fileno(t->file), 0) ||
                      fseek(t->file, 0, SEEK_SET))) {
        t->view->error = INSTALL_IO;
        RETURN(0);
      }
      n = snprintf(url, sizeof(url), "https://%s/%s%s%s", hosts[host], resource,
                   token[0] ? "?" : "", token[0] == '?' ? token + 1 : token);
      steam_secret_clear(token, sizeof(token));
      if (n < 0 || (size_t)n >= sizeof(url)) {
        t->view->error = INSTALL_PATH;
        RETURN(0);
      }
      curl_easy_reset(curl);
#define OPT(k, v)                                                              \
  do {                                                                         \
    if (curl_easy_setopt(curl, k, v) != CURLE_OK) {                            \
      curl_easy_reset(curl);                                                   \
      t->view->error = INSTALL_CDN;                                            \
      RETURN(0);                                                               \
    }                                                                          \
  } while (0)
      OPT(CURLOPT_URL, url);
      OPT(CURLOPT_PROXY, "");
      OPT(CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);
      OPT(CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
      OPT(CURLOPT_SSL_VERIFYPEER, 1L);
      OPT(CURLOPT_SSL_VERIFYHOST, 2L);
      OPT(CURLOPT_FOLLOWLOCATION, 0L);
      OPT(CURLOPT_NOSIGNAL, 1L);
      OPT(CURLOPT_TCP_KEEPALIVE, 1L);
      OPT(CURLOPT_BUFFERSIZE, 128L * 1024L);
      OPT(CURLOPT_CONNECTTIMEOUT, 10L);
      OPT(CURLOPT_TIMEOUT, 120L);
      OPT(CURLOPT_LOW_SPEED_LIMIT, 128L);
      OPT(CURLOPT_LOW_SPEED_TIME, 30L);
      OPT(CURLOPT_WRITEFUNCTION, write_data);
      OPT(CURLOPT_WRITEDATA, t);
      OPT(CURLOPT_NOPROGRESS, 0L);
      OPT(CURLOPT_XFERINFOFUNCTION, transfer_tick);
      OPT(CURLOPT_XFERINFODATA, t);
      started = millis();
      rc = curl_easy_perform(curl);
      t->view->network_ms += millis() - started;
      ++t->view->requests;
      /* Numeric timings only; never collect effective URL or signed tokens. */
      if (curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &first_byte) == CURLE_OK &&
          curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time) == CURLE_OK &&
          first_byte >= 0 && total_time >= first_byte) {
        t->view->first_byte_ms += (uint64_t)(first_byte * 1000);
        t->view->body_ms += (uint64_t)((total_time - first_byte) * 1000);
      }
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
      if(curl_easy_getinfo(curl,CURLINFO_NUM_CONNECTS,&connections)==CURLE_OK&&connections>0)
        t->view->connections+=(unsigned int)connections;
      /* Clear copied signed URL and pointers to stack transfer before leaving. */
      curl_easy_reset(curl);
      steam_secret_clear(url, sizeof(url));
#undef OPT
      t->view->http_status = (unsigned int)status;
      t->view->transport = (unsigned int)rc;
      if (t->io_error) {
        t->view->error = t->io_error == ENOSPC ? INSTALL_DISK : INSTALL_IO;
        RETURN(0);
      }
      if (!tick(t->cb)) {
        t->view->error = INSTALL_CANCELLED;
        RETURN(0);
      }
      if (!rc && status == 200) {
        t->cdn->preferred = host;
        RETURN(1);
      }
      if (status == 401 || status == 403) {
        size_t j;
        if (authorized || !steam_backend_cdn_auth(t->backend, t->app, t->depot,
                                                  hosts[host], token)) {
          steam_secret_clear(t->cdn->tokens[host], sizeof(token));
          steam_secret_clear(token, sizeof(token));
          continue;
        }
        for (j = 0; token[j]; ++j)
          if ((unsigned char)token[j] <= 32 || (unsigned char)token[j] >= 127 ||
              token[j] == '#')
            break;
        if (token[j]) {
          steam_secret_clear(token, sizeof(token));
          continue;
        }
        authorized = 1;
        memcpy(t->cdn->tokens[host], token, sizeof(token));
        goto retry_host;
      }
    }
  t->view->error = t->view->http_status == 401 || t->view->http_status == 403
                       ? 1009
                       : INSTALL_CDN;
  RETURN(0);
#undef RETURN
}
static int unzip(const char *archive, const char *output,
                 const struct steam_install_callbacks *cb) {
  unzFile zip = unzOpen64(archive);
  unz_global_info64 info;
  unz_file_info64 entry;
  FILE *file = NULL;
  unsigned char buf[32768];
  int n, ok = 0, opened = 0;
  if (!zip)
    return 0;
  if (unzGetGlobalInfo64(zip, &info) != UNZ_OK || info.number_entry != 1 ||
      unzGoToFirstFile(zip) != UNZ_OK ||
      unzGetCurrentFileInfo64(zip, &entry, NULL, 0, NULL, 0, NULL, 0) !=
          UNZ_OK ||
      entry.uncompressed_size > 128u * 1024u * 1024u ||
      unzOpenCurrentFile(zip) != UNZ_OK)
    goto done;
  opened = 1;
  if (!regular_or_missing(output))
    goto done;
  file = fopen(output, "wb");
  if (!file)
    goto done;
  while ((n = unzReadCurrentFile(zip, buf, sizeof(buf))) > 0)
    if (!tick(cb) || fwrite(buf, 1, (size_t)n, file) != (size_t)n)
      goto done;
  if (n < 0 || fflush(file) || fsync(fileno(file)))
    goto done;
  ok = 1;
done:
  if (file && fclose(file))
    ok = 0;
  if (opened && unzCloseCurrentFile(zip) != UNZ_OK)
    ok = 0;
  unzClose(zip);
  return ok;
}
static int valid_chunk(FILE *file, const struct steam_chunk *chunk,
                       unsigned char *data) {
  unsigned char hash[20];
  if (!file || chunk->offset > LONG_MAX ||
      fseek(file, (long)chunk->offset, SEEK_SET) ||
      fread(data, 1, chunk->original, file) != chunk->original)
    return 0;
  steam_sha1(data, chunk->original, hash);
  return !memcmp(hash, chunk->sha, 20);
}
static int by_offset(const void *a, const void *b) {
  const struct steam_chunk *x = a, *y = b;
  return x->offset < y->offset ? -1 : x->offset > y->offset;
}
static int ordered_chunks(struct steam_manifest_file *mapping,
                          struct steam_chunk **out, size_t *count) {
  struct steam_manifest_file copy = *mapping;
  struct steam_chunk one;
  size_t n = 0, i;
  uint64_t covered = 0;
  int status;
  *out = NULL;
  *count = 0;
  while ((status = steam_manifest_chunk(&copy, &one)) == 1)
    if (++n > 65536)
      return 0;
  if (status < 0)
    return 0;
  if (n) {
    *out = calloc(n, sizeof(**out));
    if (!*out)
      return 0;
  }
  copy = *mapping;
  for (i = 0; i < n; ++i)
    if (steam_manifest_chunk(&copy, *out + i) != 1)
      goto fail;
  if (n)
    qsort(*out, n, sizeof(**out), by_offset);
  for (i = 0; i < n; ++i) {
    if ((*out)[i].offset != covered)
      goto fail;
    covered += (*out)[i].original;
  }
  if (covered != mapping->size)
    goto fail;
  *count = n;
  return 1;
fail:
  free(*out);
  *out = NULL;
  return 0;
}
/* Bind resume to the exact plan, preventing stale files after a branch changes.
 */
static int staging_plan(const struct steam_app *app, const char *stage,
                        int existed) {
  char name[1050], tmp[1056], expected[4096], actual[4096];
  size_t used = 0, i;
  int n;
  FILE *f;
  n = snprintf(name, sizeof(name), "%s.plan", stage);
  if (n < 0 || (size_t)n >= sizeof(name))
    return 0;
  for (i = 0; i < app->count; ++i) {
    n = snprintf(expected + used, sizeof(expected) - used, "%u %llu\n",
                 app->depots[i].id,
                 (unsigned long long)app->depots[i].manifest);
    if (n < 0 || (size_t)n >= sizeof(expected) - used)
      return 0;
    used += (size_t)n;
  }
  if (!regular_or_missing(name))
    return 0;
  f = fopen(name, "rb");
  if (f) {
    size_t got = fread(actual, 1, sizeof(actual), f);
    int valid = !ferror(f) && got == used && !memcmp(actual, expected, used);
    fclose(f);
    if (valid)
      return 1;
    /* Begun with another set of files: another language, or the game was
     * updated meanwhile. The download goes on with the set wanted now.
     * Every file already there is checked against the new manifests before
     * it is kept (size and SHA-1), so nothing of the old set can pass for
     * the new; refusing left the download unable to continue at all
     * (Call of Duty 4, begun with no language files, then in English). */
  } else if (errno != ENOENT)
    return 0;
  /* A folder of files with no plan beside it is taken up the same way. */
  (void)existed;
  n = snprintf(tmp, sizeof(tmp), "%s.tmp", name);
  if (n < 0 || (size_t)n >= sizeof(tmp))
    return 0;
  if (!regular_or_missing(tmp))
    return 0;
  f = fopen(tmp, "wb");
  if (!f)
    return 0;
  n = fwrite(expected, 1, used, f) == used && !fflush(f) && !fsync(fileno(f));
  if (fclose(f))
    n = 0;
  /* The card does not rename over a file that is there. */
  if (n && !lstat(name, &(struct stat){0}) && unlink(name))
    n = 0;
  return n && !rename(tmp, name);
}
int steam_install_present(const char *root, uint32_t app) {
  char directory[512];
  return steam_storage_directory(root, app, directory);
}
int steam_install_executable(const char *root, uint32_t app, char out[512]) {
  char marker[1024], suffix[128], line[512], relative[384] = {0}, base[512];
  FILE *f;
  struct stat st;
  out[0] = 0;
  if (!steam_install_present(root, app))
    return 0;
  if (!steam_storage_directory(root, app, base) ||
      !path(marker, sizeof(marker), base, "autorun-steam-install.ini"))
    return 0;
  snprintf(suffix,sizeof(suffix),"steam/selected/%u.exe",app);
  if(!path(marker,sizeof(marker),root,suffix))return 0;
  f=fopen(marker,"r");
  if(f){if(!fgets(relative,sizeof(relative),f)||ferror(f)){fclose(f);return 0;}fclose(f);relative[strcspn(relative,"\r\n")]=0;goto validate;}
  if(!path(marker,sizeof(marker),base,"autorun-steam-install.ini"))return 0;
  f = fopen(marker, "r");
  if (!f)
    return 0;
  while (fgets(line, sizeof(line), f))
    if (!strncmp(line, "executable=", 11)) {
      size_t n = strcspn(line + 11, "\r\n");
      if (n >= sizeof(relative)) {
        fclose(f);
        return 0;
      }
      memcpy(relative, line + 11, n);
      relative[n] = 0;
    }
  fclose(f);
validate:
  if (!steam_content_path(relative) || !path(out, 512, base, relative) ||
      lstat(out, &st) || !S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
    out[0] = 0;
    return 0;
  }
  return 1;
}
int steam_install_select_executable(const char *root,uint32_t app,const char *exe){
  char base[512],suffix[128],relative[384],name[1024],tmp[1030];int n,ok;size_t len;struct stat st;FILE *f;
  if(!steam_install_present(root,app))return 0;
  if (!steam_storage_directory(root, app, base))
    return 0;
  n = (int)strlen(base);
  if (n + 1 >= (int)sizeof(base))
    return 0;
  base[n++] = '/';
  base[n] = 0;
  if(n<0||(size_t)n>=sizeof(base)||strncmp(exe,base,(size_t)n)||strlen(exe+n)>=sizeof(relative))return 0;
  strcpy(relative,exe+n);len=strlen(relative);
  if(!steam_content_path(relative)||len<4||strcasecmp(relative+len-4,".exe")||lstat(exe,&st)||!S_ISREG(st.st_mode)||S_ISLNK(st.st_mode))return 0;
  snprintf(suffix,sizeof(suffix),"steam/selected/%u.exe",app);
  if(!path(name,sizeof(name),root,suffix)||!parents(name)||!regular_or_missing(name))return 0;
  n=snprintf(tmp,sizeof(tmp),"%s.tmp",name);if(n<0||(size_t)n>=sizeof(tmp)||!regular_or_missing(tmp))return 0;
  f=fopen(tmp,"wb");if(!f)return 0;ok=fprintf(f,"%s\n",relative)>0&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;if(!ok)return 0;if(!rename(tmp,name))return 1;
  /* The card does not rename over a file that is there. */
  if(unlink(name)&&errno!=ENOENT)return 0;
  return !rename(tmp,name);
}
static int remember_executable(const struct steam_app *app,const char *name,
                                char **one,size_t *count,int *launch){
  size_t n=strlen(name);
  if(n>4&&!strcasecmp(name+n-4,".exe")){
    if(app->executable[0]&&!strcasecmp(name,app->executable))*launch=1;
    if(!(*count)++){
      *one=malloc(n+1);if(!*one)return 0;strcpy(*one,name);
    }
  }
  return 1;
}
/* One installation's worth of work between the manifest and the engine: which
 * chunk goes to which CDN connection, what is retried and when. CM and CURL
 * stay on this thread; decoding, the card and verification do not. */
enum { NETWORK_ATTEMPTS = 10, INTEGRITY_ATTEMPTS = 3, OPEN_FILES = 48 };
/* Encrypted and decoded chunks waiting for the card. The launcher has the
 * whole application heap to itself while it downloads; should the heap be
 * smaller than this, a failed allocation simply waits for the card. */
#define DOWNLOAD_MEMORY (768u * 1024u * 1024u)
struct work {
  struct steam_engine_file *file;
  struct steam_chunk chunk;
  unsigned int network, integrity;
  size_t avoid; /* host + 1 that last failed this chunk */
  uint64_t not_before;
};
struct request {
  struct work work;
  unsigned char *buffer;
  size_t host;
  unsigned int token_generation;
  int active, authorized;
};
struct depot_run {
  struct transfer *t;
  struct steam_engine *engine;
  char (*hosts)[128];
  size_t host_count;
  const char *stage;
  const unsigned char *key;
  struct steam_manifest *manifest;
  const struct steam_app *app;
  unsigned char *plain;
  int fresh;
  struct steam_manifest_file mapping;
  int have_mapping, exhausted;
  struct steam_engine_file *file;
  struct steam_chunk *chunks;
  unsigned char *missing;
  size_t chunk_count, next_chunk;
  struct work *retries;
  size_t retry_count, retry_capacity;
  struct request requests[STEAM_CDN_MAX_REQUESTS];
  unsigned int concurrency, active;
  uint64_t in_flight, bad_until[8], *reused;
  char parent[1024];
  char **exe;
  size_t *exe_count;
  int *launch;
  struct steam_cdn_metrics before;
  struct steam_download_view base;
};
static void run_metrics(struct depot_run *r) {
  struct steam_cdn_metrics m;
  struct steam_download_view *v = r->t->view;
  steam_cdn_metrics(r->t->cdn->pool, &m);
  v->network_bytes = r->base.network_bytes + m.bytes - r->before.bytes;
  v->network_ms = r->base.network_ms + m.request_ms - r->before.request_ms;
  v->first_byte_ms =
      r->base.first_byte_ms + m.first_byte_ms - r->before.first_byte_ms;
  v->body_ms = r->base.body_ms + m.body_ms - r->before.body_ms;
  v->requests = r->base.requests + m.requests - r->before.requests;
  v->connections = r->base.connections + m.connections - r->before.connections;
  if (m.peak_active > v->peak_requests)
    v->peak_requests = m.peak_active;
}
static void run_publish(struct depot_run *r, struct steam_engine_view *e) {
  struct steam_download_view *v = r->t->view;
  steam_engine_status(r->engine, e);
  run_metrics(r);
  v->downloaded = *r->reused + e->written;
  v->decode_ms = e->decode_ms;
  v->write_ms = e->write_ms;
  v->sync_ms = e->sync_ms;
  v->verify_ms = e->verify_ms;
  v->batch_files = e->files;
  if (e->active_files > v->batch_peak_files)
    v->batch_peak_files = e->active_files;
  if (e->file[0])
    snprintf(v->file, sizeof(v->file), "%s", e->file);
  sample_rate(r->t);
  progress(r->t->cb, v);
}
static int run_retry(struct depot_run *r, const struct work *w) {
  if (r->retry_count == r->retry_capacity) {
    size_t larger = r->retry_capacity ? r->retry_capacity * 2 : 16;
    struct work *grown = realloc(r->retries, larger * sizeof(*grown));
    if (!grown) {
      r->t->view->error = 5;
      return 0;
    }
    r->retries = grown;
    r->retry_capacity = larger;
  }
  r->retries[r->retry_count++] = *w;
  return 1;
}
/* A directory is checked once, not once per file in it: every check is a
 * round trip to the card. */
static int run_parent(struct depot_run *r, const char *target) {
  const char *end = strrchr(target, '/');
  size_t n = end ? (size_t)(end - target) : 0;
  if (!n || n >= sizeof(r->parent))
    return 0;
  if (strlen(r->parent) == n && !memcmp(r->parent, target, n))
    return 1;
  if (!parents(target))
    return 0;
  memcpy(r->parent, target, n);
  r->parent[n] = 0;
  return 1;
}
/* 1: the mapping is dealt with (r->chunks set when chunks are wanted);
 * 0: it has to wait for the engine; -1: failed. */
static int run_file(struct depot_run *r) {
  struct steam_manifest_file *mapping = &r->mapping;
  struct steam_download_view *v = r->t->view;
  struct steam_engine_view e;
  struct steam_chunk *chunks = NULL;
  unsigned char *missing = NULL;
  char target[1024];
  struct stat st;
  size_t count = 0, i, pending;
  int existing = 0;
  if (!path(target, sizeof(target), r->stage, mapping->path)) {
    v->error = INSTALL_PATH;
    return -1;
  }
  if (mapping->flags & 64) {
    if (!mkdirs(target)) {
      v->error = errno == ENOSPC ? INSTALL_DISK : INSTALL_IO;
      return -1;
    }
    r->parent[0] = 0;
    return 1;
  }
  steam_engine_status(r->engine, &e);
  /* A second entry for a name waits for the first: the later one wins, as it
   * did when files were written one after another. */
  if (e.active_files >= OPEN_FILES ||
      steam_engine_busy(r->engine, mapping->path))
    return 0;
  if (!run_parent(r, target)) {
    v->error = errno == ENOSPC ? INSTALL_DISK : INSTALL_IO;
    return -1;
  }
  /* A staging folder made by this very attempt holds nothing to look for. */
  if (!r->fresh) {
    if (!lstat(target, &st)) {
      if (!S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
        v->error = INSTALL_PATH;
        return -1;
      }
      existing = 1;
    } else if (errno != ENOENT) {
      v->error = INSTALL_IO;
      return -1;
    }
  }
  if (!remember_executable(r->app, mapping->path, r->exe, r->exe_count,
                           r->launch)) {
    v->error = 5;
    return -1;
  }
  if (existing && (uint64_t)st.st_size == mapping->size &&
      steam_engine_verified(r->engine, mapping->path, mapping->size,
                            mapping->sha)) {
    *r->reused += mapping->size;
    return 1;
  }
  if (!ordered_chunks(mapping, &chunks, &count)) {
    v->error = INSTALL_CORRUPT;
    v->integrity_detail = STEAM_INTEGRITY_LAYOUT;
    return -1;
  }
  for (i = 0; i < count; ++i)
    if (!chunks[i].original || chunks[i].original > STEAM_CHUNK_LIMIT ||
        !chunks[i].compressed ||
        chunks[i].compressed > STEAM_CHUNK_LIMIT + 64) {
      free(chunks);
      v->error = INSTALL_CORRUPT;
      v->integrity_detail = STEAM_INTEGRITY_LAYOUT;
      return -1;
    }
  missing = malloc(count ? count : 1);
  if (!missing) {
    free(chunks);
    v->error = 5;
    return -1;
  }
  memset(missing, 1, count ? count : 1);
  pending = count;
  if (existing && count) {
    /* Bytes left by an interrupted attempt: keep every chunk that still
     * matches, and if all of them do, the file's own hash comes out of the
     * same single read. */
    FILE *f = fopen(target, "rb");
    struct steam_sha1 whole;
    unsigned char digest[20];
    uint64_t measured = millis(), kept = 0;
    int contiguous = 1;
    if (!f) {
      free(chunks);
      free(missing);
      v->error = INSTALL_IO;
      return -1;
    }
    steam_sha1_begin(&whole);
    for (i = 0; i < count; ++i) {
      if (!tick(r->t->cb)) {
        steam_sha1_end(&whole, digest);
        fclose(f);
        free(chunks);
        free(missing);
        v->error = INSTALL_CANCELLED;
        return -1;
      }
      if (chunks[i].offset + (uint64_t)chunks[i].original <=
              (uint64_t)st.st_size &&
          valid_chunk(f, chunks + i, r->plain)) {
        missing[i] = 0;
        --pending;
        kept += chunks[i].original;
        if (contiguous)
          steam_sha1_update(&whole, r->plain, chunks[i].original);
      } else
        contiguous = 0;
    }
    steam_sha1_end(&whole, digest);
    fclose(f);
    v->resume_ms += millis() - measured;
    *r->reused += kept;
    if (!pending && (uint64_t)st.st_size == mapping->size &&
        !memcmp(digest, mapping->sha, 20)) {
      steam_engine_note(r->engine, mapping->path, mapping->size, mapping->sha);
      free(chunks);
      free(missing);
      return 1;
    }
  }
  r->file = steam_engine_open(r->engine, mapping->path, mapping->size,
                              mapping->sha, r->key, (unsigned int)pending,
                              existing);
  if (!r->file) {
    free(chunks);
    free(missing);
    v->error = 5;
    return -1;
  }
  if (!pending) {
    free(chunks);
    free(missing);
    return 1;
  }
  r->chunks = chunks;
  r->missing = missing;
  r->chunk_count = count;
  r->next_chunk = 0;
  return 1;
}
/* 1: a chunk to download; 0: nothing can start right now; -1: failed. */
static int run_next(struct depot_run *r, struct work *out) {
  uint64_t now = millis();
  size_t i;
  for (i = 0; i < r->retry_count; ++i)
    if (r->retries[i].not_before <= now) {
      *out = r->retries[i];
      r->retries[i] = r->retries[--r->retry_count];
      return 1;
    }
  for (;;) {
    int n;
    if (r->chunks) {
      while (r->next_chunk < r->chunk_count && !r->missing[r->next_chunk])
        ++r->next_chunk;
      if (r->next_chunk < r->chunk_count) {
        *out = (struct work){.file = r->file,
                             .chunk = r->chunks[r->next_chunk++]};
        return 1;
      }
      free(r->chunks);
      free(r->missing);
      r->chunks = NULL;
      r->missing = NULL;
    }
    if (r->exhausted)
      return 0;
    if (!r->have_mapping) {
      n = steam_manifest_next(r->manifest, &r->mapping);
      if (n < 0) {
        r->t->view->error = INSTALL_CORRUPT;
        return -1;
      }
      if (!n) {
        r->exhausted = 1;
        return 0;
      }
      r->have_mapping = 1;
    }
    n = run_file(r);
    if (n <= 0)
      return n;
    r->have_mapping = 0;
  }
}
/* Connections are spread over the nearest few servers; one that just failed
 * is left alone for a while. */
static size_t run_host(struct depot_run *r, unsigned int slot, size_t avoid) {
  size_t spread = r->host_count < 4 ? r->host_count : 4, i, host;
  uint64_t now = millis();
  for (i = 0; i < r->host_count; ++i) {
    host = (slot % spread + i) % r->host_count;
    if (r->bad_until[host] <= now && host + 1 != avoid)
      return host;
  }
  for (i = 0; i < r->host_count; ++i) {
    host = (slot % spread + i) % r->host_count;
    if (host + 1 != avoid)
      return host;
  }
  return slot % spread;
}
static int run_start(struct depot_run *r, unsigned int slot) {
  struct request *q = &r->requests[slot];
  struct transfer *t = r->t;
  char url[2048] = {0}, hash[41];
  const char *token = t->cdn->tokens[q->host];
  unsigned int i;
  int n, ok = 0;
  for (i = 0; i < 20; ++i)
    snprintf(hash + i * 2, 3, "%02x", q->work.chunk.sha[i]);
  n = snprintf(url, sizeof(url), "%s://%s/depot/%u/chunk/%s%s%s",
               t->backend->plain_hosts >> q->host & 1 ? "http" : "https",
               r->hosts[q->host], t->depot, hash, token[0] ? "?" : "",
               token[0] == '?' ? token + 1 : token);
  if (n > 0 && (size_t)n < sizeof(url))
    ok = steam_cdn_start(t->cdn->pool, slot, url, q->buffer,
                         q->work.chunk.compressed);
  q->token_generation = t->cdn->token_generation[q->host];
  steam_secret_clear(url, sizeof(url));
  if (!ok) {
    t->view->error = INSTALL_CDN;
    return 0;
  }
  q->active = 1;
  ++r->active;
  r->in_flight += q->work.chunk.compressed;
  return 1;
}
static int run_result(struct depot_run *r,
                      const struct steam_cdn_result *result) {
  struct request *q = &r->requests[result->slot];
  struct transfer *t = r->t;
  struct steam_download_view *v = t->view;
  uint64_t now = millis();
  unsigned int delay;
  v->http_status = result->http;
  v->transport = result->transport;
  q->active = 0;
  --r->active;
  r->in_flight -= q->work.chunk.compressed;
  if (!result->transport && result->http == 200) {
    unsigned char *buffer = q->buffer;
    q->buffer = NULL;
    if (result->bytes == q->work.chunk.compressed) {
      t->cdn->preferred = q->host;
      if (!steam_engine_submit(r->engine, q->work.file, &q->work.chunk, buffer,
                               q->work.integrity |
                                   (unsigned int)(q->host + 1) << 8)) {
        v->error = 5;
        return 0;
      }
      return 1;
    }
    free(buffer);
    v->integrity_detail = STEAM_INTEGRITY_RESPONSE_SIZE;
    if (++q->work.integrity >= INTEGRITY_ATTEMPTS) {
      v->error = INSTALL_CORRUPT;
      return 0;
    }
    ++v->integrity_retries;
    v->integrity_detail = 0;
    q->work.avoid = q->host + 1;
    return run_retry(r, &q->work);
  }
  if ((result->http == 401 || result->http == 403) && !q->authorized) {
    char token[1024] = {0};
    size_t n;
    q->authorized = 1;
    /* Whoever sees the refusal first asks Steam; the others reuse its answer. */
    if (q->token_generation == t->cdn->token_generation[q->host] &&
        steam_backend_cdn_auth(t->backend, t->app, t->depot, r->hosts[q->host],
                               token)) {
      for (n = 0; token[n]; ++n)
        if ((unsigned char)token[n] <= 32 || (unsigned char)token[n] >= 127 ||
            token[n] == '#')
          break;
      if (!token[n]) {
        memcpy(t->cdn->tokens[q->host], token, sizeof(token));
        ++t->cdn->token_generation[q->host];
      }
    }
    steam_secret_clear(token, sizeof(token));
    return run_start(r, result->slot);
  }
  free(q->buffer);
  q->buffer = NULL;
  r->bad_until[q->host] = now + 15000;
  q->work.avoid = q->host + 1;
  q->work.network += result->http == 401 || result->http == 403 ? 3 : 1;
  if (q->work.network >= NETWORK_ATTEMPTS) {
    v->error = result->http == 401 || result->http == 403 ? 1009 : INSTALL_CDN;
    return 0;
  }
  /* A short loss of Wi-Fi is waited out rather than failing the installation:
   * half a second, doubling to eight. */
  delay = 250u << (q->work.network < 6 ? q->work.network : 5);
  q->work.not_before = now + (delay > 8000 ? 8000 : delay);
  return run_retry(r, &q->work);
}
static int run_depot(struct depot_run *r) {
  struct transfer *t = r->t;
  struct steam_download_view *v = t->view;
  unsigned int i;
  int ok = 0;
  if (!t->cdn->pool)
    t->cdn->pool = steam_cdn_create(r->concurrency);
  if (!t->cdn->pool) {
    v->error = 5;
    return 0;
  }
  if (t->cdn->depot != t->depot) {
    steam_secret_clear(t->cdn->tokens, sizeof(t->cdn->tokens));
    memset(t->cdn->token_generation, 0, sizeof(t->cdn->token_generation));
    t->cdn->depot = t->depot;
  }
  r->base = *v;
  steam_cdn_metrics(t->cdn->pool, &r->before);
  for (;;) {
    struct steam_engine_view e;
    struct steam_engine_failure failure;
    struct steam_cdn_result result;
    uint64_t measured;
    int idle;
    idle = !r->active && r->exhausted && !r->chunks && !r->have_mapping &&
           !r->retry_count;
    v->state = idle ? STEAM_DOWNLOAD_VERIFYING : STEAM_DOWNLOAD_FILES;
    run_publish(r, &e);
    if (e.error) {
      v->error = e.error;
      v->integrity_detail = e.integrity_detail;
      v->chunk_format = e.chunk_format;
      goto done;
    }
    if (!tick(t->cb)) {
      v->error = INSTALL_CANCELLED;
      goto done;
    }
    while (steam_engine_failed(r->engine, &failure)) {
      struct work w = {.file = failure.file,
                       .chunk = failure.chunk,
                       .integrity = (failure.attempts & 255) + 1,
                       .avoid = failure.attempts >> 8};
      if (w.integrity >= INTEGRITY_ATTEMPTS) {
        v->error = INSTALL_CORRUPT;
        v->integrity_detail = failure.detail;
        v->chunk_format = failure.format;
        goto done;
      }
      ++v->integrity_retries;
      if (!run_retry(r, &w))
        goto done;
      idle = 0;
    }
    if (idle && !e.active_files) {
      ok = 1;
      goto done;
    }
    for (i = 0; i < r->concurrency; ++i)
      if (!r->requests[i].active) {
        struct request *q = &r->requests[i];
        int got;
        if (e.queued_bytes + r->in_flight >= DOWNLOAD_MEMORY)
          break;
        got = run_next(r, &q->work);
        if (got < 0)
          goto done;
        if (!got)
          break;
        q->buffer = malloc(q->work.chunk.compressed);
        if (!q->buffer) {
          if (!r->active && !e.queued_bytes) {
            v->error = 5;
            goto done;
          }
          if (!run_retry(r, &q->work))
            goto done;
          break;
        }
        q->host = run_host(r, i, q->work.avoid);
        q->authorized = 0;
        if (!run_start(r, i))
          goto done;
      }
    measured = millis();
    if (!steam_cdn_pump(t->cdn->pool, r->active ? 10 : 0)) {
      v->error = INSTALL_CDN;
      goto done;
    }
    if (!r->active) {
      SDL_Delay(2);
      v->writer_wait_ms += millis() - measured;
    }
    while (steam_cdn_take(t->cdn->pool, &result))
      if (!run_result(r, &result))
        goto done;
  }
done:
  /* CURL lets go of the buffers before they are freed. */
  steam_cdn_cancel(t->cdn->pool);
  for (i = 0; i < STEAM_CDN_MAX_REQUESTS; ++i) {
    free(r->requests[i].buffer);
    r->requests[i].buffer = NULL;
  }
  free(r->chunks);
  free(r->missing);
  free(r->retries);
  r->chunks = NULL;
  r->missing = NULL;
  r->retries = NULL;
  run_metrics(r);
  return ok;
}
/* While an installation runs the console is not let fall asleep in the middle.
 * The clocks are left alone: the system's boost mode is a clock setting like
 * any other, and the clocks are the owner's (tests/check_no_clock_control.py). */
static void console_for_download(int on) {
#ifdef __SWITCH__
  appletSetAutoSleepDisabled(on != 0);
  if (on && R_FAILED(svcSetThreadCoreMask(CUR_THREAD_HANDLE, 1, 15)))
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, 1, 7);
#else
  (void)on;
#endif
}
/* A depot shared with another application has its manifest listed there. */
static int shared_manifests(struct steam_backend *backend, uint32_t appid,
                            struct steam_app *app) {
  size_t i, j;
  for (i = 0; i < app->count; ++i)
    if (!app->depots[i].manifest && app->depots[i].source_app != appid) {
      struct steam_app shared;
      if (!steam_backend_app(backend, app->depots[i].source_app, &shared))
        return 0;
      for (j = 0; j < shared.count; ++j)
        if (shared.depots[j].id == app->depots[i].id) {
          app->depots[i].manifest = shared.depots[j].manifest;
          app->depots[i].size = shared.depots[j].size;
          break;
        }
    }
  return 1;
}
/* An application lists every depot it has, and an account's licence covers
 * some of them: the files of an add-on sold separately, a language sold in
 * one region. Steam installs what the licence covers, and so does this: a
 * depot whose key Steam refuses is left out. What is left has to hold some of
 * the game itself: the application most of the bytes come from (the game's
 * own, or the one it shares its files with) must have a depot among them, or
 * the game is not this account's. Returns 0 with the error set, or 1 with
 * *language set when a refused depot held the chosen language. */
static int licensed_depots(struct steam_backend *backend, uint32_t appid,
                           struct steam_app *app,
                           const struct steam_install_callbacks *cb,
                           unsigned int *error, int *language) {
  size_t i, j, kept = 0, own = 0;
  uint32_t main_app = 0;
  uint64_t most = 0;
  *language = 0;
  for (i = 0; i < app->count; ++i) {
    uint64_t bytes = 0;
    for (j = 0; j < app->count; ++j)
      if (app->depots[j].source_app == app->depots[i].source_app)
        bytes += app->depots[j].size;
    if (!i || bytes > most) {
      most = bytes;
      main_app = app->depots[i].source_app;
    }
  }
  for (i = 0; i < app->count; ++i) {
    unsigned char key[32];
    uint64_t code;
    if (!tick(cb)) {
      *error = INSTALL_CANCELLED;
      return 0;
    }
    if (!app->depots[i].manifest) {
      *error = INSTALL_UNSUPPORTED;
      return 0;
    }
    if (steam_backend_depot(backend, appid, &app->depots[i], key, &code)) {
      memset(key, 0, sizeof(key));
      own += app->depots[i].source_app == main_app;
      app->depots[kept++] = app->depots[i];
    } else if (backend->error != STEAM_ACCESS_DENIED) {
      *error = backend->error;
      return 0;
    } else if (app->depots[i].language_specific && !app->depots[i].optional)
      *language = 1;
  }
  app->count = kept;
  if (!own) {
    *error = STEAM_ACCESS_DENIED;
    return 0;
  }
  return 1;
}
int steam_install_run(struct steam_backend *backend, const char *root,
                      uint32_t appid,
                      const struct steam_install_callbacks *original) {
  struct install_observer observer={.original=original,.root=root};
  struct steam_install_callbacks observed={.context=&observer,.tick=observe_tick,.progress=observe_progress};
  const struct steam_install_callbacks *cb=&observed;
  struct steam_app app;
  struct steam_download_view v = {.state = STEAM_DOWNLOAD_PLANNING,
                                  .app_id = appid,
                                  .parallelism=backend->concurrency?backend->concurrency:1};
  char hosts[8][128], stage[1024], final[1024], cache[1024], suffix[128],
      zip[1024];
  char(*manifest_paths)[1024] = calloc(STEAM_DEPOT_LIMIT, 1024);
  unsigned char keys[STEAM_DEPOT_LIMIT][32], *plain = NULL;
  struct steam_engine *engine = NULL;
  uint64_t reused = 0;
  int fresh = 0, boosted = 0;
  size_t hosts_count = 0, i;
  uint64_t start = millis();
  int ok = 0;
  struct stat st;
  struct statvfs disk;
  struct steam_manifest m = {0};
  struct cdn_session cdn = {.started=start,.rate_tick=start};
  FILE *file = NULL;
  char *one_exe = NULL;
  size_t exe_count = 0;
  int launch_exe_found = 0;
  snprintf(v.language, sizeof(v.language), "%s", backend->language[0] ? backend->language : "english");
  memset(keys, 0, sizeof(keys));
  observe_start(&observer);
  if (!manifest_paths) {
    v.error = 5;
    goto done;
  }
  progress(cb, &v);
  if (!tick(cb)) {
    v.error = INSTALL_CANCELLED;
    goto done;
  }
  if (!steam_backend_app(backend, appid, &app)) {
    v.error = backend->error;
    goto done;
  }
  /* A game with no files in the chosen language is installed in English when
   * it has them, as Steam itself does; the marker then says English, and so
   * does the local Steam library when the game asks its language. */
  if (app.windows != 0 && strcmp(backend->language, "english") &&
      (app.language_missing ||
       /* "Common files only", for a game that has language files, is how
        * such a game was left without them before this was understood. */
       (!strcmp(backend->language, STEAM_LANGUAGE_BASE_ID) && (app.available_languages & ~STEAM_LANGUAGE_BASE))) &&
      app.english_available) {
    snprintf(backend->language, sizeof(backend->language), "english");
    snprintf(v.language, sizeof(v.language), "english");
    if (!steam_backend_app(backend, appid, &app)) {
      v.error = backend->error;
      goto done;
    }
  }
  v.available_languages = app.available_languages;
  if(app.windows != 0 && app.language_missing){v.error=3111;goto done;}
  if (app.windows == 0 || !app.count) {
    v.error = INSTALL_UNSUPPORTED;
    goto done;
  }
  if (app.executable[0] && !steam_content_path(app.executable))
    app.executable[0] = 0;
  if (!shared_manifests(backend, appid, &app)) {
    v.error = backend->error;
    goto done;
  }
  snprintf(suffix, sizeof(suffix), "steam/staging/%u", appid);
  if (!path(stage, sizeof(stage), root, suffix))
    goto path_error;
  char pending_directory[512];
  if (steam_install_present(root, appid) ||
      steam_storage_pending(root, appid, pending_directory)) {
    v.error = INSTALL_UNSUPPORTED;
    goto done;
  }
  if (!steam_storage_plan(root, appid, app.name, final))
    goto path_error;
  if (!lstat(final, &st)) {
    v.error = INSTALL_UNSUPPORTED;
    goto done;
  } /* Updates require a separate validated transaction. */
  {
    int language;
    if (!licensed_depots(backend, appid, &app, cb, &v.error, &language))
      goto done;
    /* The chosen language is not in the licence: English, as when the game
     * has no files in that language at all. */
    if (language && strcmp(backend->language, "english") &&
        app.english_available) {
      snprintf(backend->language, sizeof(backend->language), "english");
      snprintf(v.language, sizeof(v.language), "english");
      if (!steam_backend_app(backend, appid, &app) ||
          !shared_manifests(backend, appid, &app)) {
        v.error = backend->error;
        goto done;
      }
      if (!licensed_depots(backend, appid, &app, cb, &v.error, &language))
        goto done;
    }
  }
  snprintf(suffix, sizeof(suffix), "steam/manifests/%u", appid);
  if (!path(cache, sizeof(cache), root, suffix))
    goto path_error;
  {
    int existed = !lstat(stage, &st);
    fresh = !existed;
    if (!mkdirs(stage) || !mkdirs(cache))
      goto io_error;
    if (!staging_plan(&app, stage, existed)) {
      v.error = 3110;
      goto done;
    }
  }
  if (!steam_backend_servers(backend, hosts, &hosts_count)) {
    v.error = backend->error;
    goto done;
  }
  for (i = 0; i < hosts_count; ++i)
    if (backend->tls_hosts >> i & 1) {
      cdn.preferred = i;
      break;
    }
  v.state = STEAM_DOWNLOAD_MANIFESTS;
  progress(cb, &v);
  for (i = 0; i < app.count; ++i) {
    char resource[256];
    uint64_t code;
    struct transfer t = {.cdn = &cdn, .backend = backend,
                         .app = appid,
                         .depot = app.depots[i].id,
                         .cb = cb,
                         .view = &v,
                         .capacity = 32u * 1024u * 1024u};
    if (!tick(cb)) {
      v.error = INSTALL_CANCELLED;
      goto done;
    }
    if (!app.depots[i].manifest) {
      v.error = INSTALL_UNSUPPORTED;
      goto done;
    }
    if (!steam_backend_depot(backend, appid, &app.depots[i], keys[i], &code)) {
      v.error = backend->error;
      goto done;
    }
    snprintf(suffix, sizeof(suffix), "%u-%llu.manifest", app.depots[i].id,
             (unsigned long long)app.depots[i].manifest);
    if (!path(manifest_paths[i], sizeof(manifest_paths[i]), cache, suffix) ||
        !path(zip, sizeof(zip), cache, "download.zip"))
      goto path_error;
    if (!regular_or_missing(manifest_paths[i]) || !regular_or_missing(zip))
      goto path_error;
    if (!steam_manifest_open(&m, manifest_paths[i], app.depots[i].id,
                             app.depots[i].manifest, keys[i])) {
      t.file = fopen(zip, "w+b");
      if (!t.file)
        goto io_error;
      snprintf(resource, sizeof(resource), "depot/%u/manifest/%llu/5/%llu",
               app.depots[i].id, (unsigned long long)app.depots[i].manifest,
               (unsigned long long)code);
      ok = fetch(hosts, hosts_count, resource, &t);
      if (fclose(t.file))
        ok = 0;
      if (!ok)
        goto done;
      if (!unzip(zip, manifest_paths[i], cb)) {
        v.error = errno == ENOSPC ? INSTALL_DISK : INSTALL_CORRUPT;
        goto done;
      }
      if (!steam_manifest_open(&m, manifest_paths[i], app.depots[i].id,
                               app.depots[i].manifest, keys[i])) {
        v.error = INSTALL_CORRUPT;
        goto done;
      }
    }
    if (v.total > UINT64_MAX - m.total_size) {
      v.error = INSTALL_PROTOCOL;
      goto done;
    }
    v.total += m.total_size;
    steam_manifest_close(&m);
  }
  {
    uint64_t needed = 0;
    v.total = 0;
    for (i = 0; i < app.count; ++i) {
      struct steam_manifest_file mapping;
      int n;
      if (!steam_manifest_open(&m, manifest_paths[i], app.depots[i].id,
                               app.depots[i].manifest, keys[i])) {
        v.error = INSTALL_CORRUPT;
        goto done;
      }
      while ((n = steam_manifest_next(&m, &mapping)) == 1) {
        char target[1024];
        uint64_t allocated = 0;
        if (!tick(cb)) {
          v.error = INSTALL_CANCELLED;
          goto done;
        }
        if (mapping.flags & 64)
          continue;
        if (!strcasecmp(mapping.path, "autorun-steam-install.ini") ||
            !strcasecmp(mapping.path, STEAM_ENGINE_JOURNAL)) {
          v.error = INSTALL_PATH;
          goto done;
        }
        if (!path(target, sizeof(target), stage, mapping.path))
          goto path_error;
        if (!fresh && !lstat(target, &st)) {
          if (!S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))
            goto path_error;
          allocated = (uint64_t)st.st_size;
          if (allocated > mapping.size)
            allocated = mapping.size;
        }
        if (v.total > UINT64_MAX - mapping.size ||
            needed > UINT64_MAX - (mapping.size - allocated)) {
          v.error = INSTALL_PROTOCOL;
          goto done;
        }
        v.total += mapping.size;
        needed += mapping.size - allocated;
      }
      if (n < 0) {
        v.error = INSTALL_CORRUPT;
        goto done;
      }
      steam_manifest_close(&m);
    }
    if (statvfs(root, &disk))
      goto io_error;
    v.free_known = 1;
    v.free_bytes = (uint64_t)disk.f_bavail * disk.f_frsize;
    if (needed > UINT64_MAX - 32u * 1024u * 1024u ||
        v.free_bytes < needed + 32u * 1024u * 1024u) {
      v.error = INSTALL_DISK;
      goto done;
    }
  }
  plain = malloc(STEAM_CHUNK_LIMIT);
  if (!plain) {
    v.error = 5;
    goto done;
  }
#ifdef __SWITCH__
  engine = steam_engine_create(stage, 3);
#else
  engine = steam_engine_create(stage, 2);
#endif
  if (!engine) {
    v.error = errno == ENOSPC ? INSTALL_DISK : 5;
    goto done;
  }
  console_for_download(1);
  boosted = 1;
  v.state = STEAM_DOWNLOAD_FILES;
  cdn.rate_tick=millis();cdn.rate_bytes=v.network_bytes;v.bytes_per_second=0;
  progress(cb, &v);
  for (i = 0; i < app.count; ++i) {
    struct transfer t = {.cdn = &cdn, .backend = backend, .app = appid,
                         .depot = app.depots[i].id, .cb = cb, .view = &v};
    struct depot_run *run = calloc(1, sizeof(*run));
    int finished;
    if (!run) {
      v.error = 5;
      goto done;
    }
    v.depot_id = app.depots[i].id;
    if (!steam_manifest_open(&m, manifest_paths[i], app.depots[i].id,
                             app.depots[i].manifest, keys[i])) {
      free(run);
      v.error = INSTALL_CORRUPT;
      goto done;
    }
    run->t = &t;
    run->engine = engine;
    run->hosts = hosts;
    run->host_count = hosts_count;
    run->stage = stage;
    run->key = keys[i];
    run->manifest = &m;
    run->app = &app;
    run->plain = plain;
    run->fresh = fresh;
    run->concurrency = !backend->concurrency ? 1
                       : backend->concurrency > STEAM_CDN_MAX_REQUESTS
                           ? STEAM_CDN_MAX_REQUESTS
                           : backend->concurrency;
    run->reused = &reused;
    run->exe = &one_exe;
    run->exe_count = &exe_count;
    run->launch = &launch_exe_found;
    /* Every file of a depot is verified before the next depot starts: a later
     * depot may replace a file of an earlier one. */
    finished = run_depot(run);
    free(run);
    if (!finished)
      goto done;
    steam_manifest_close(&m);
  }
  v.state = STEAM_DOWNLOAD_COMMITTING;
  progress(cb, &v);
  /* Publish the non-cancellable boundary, then honor any request accepted
   * before it. This closes the race after the last chunk/file verification. */
  if (!tick(cb)) { v.error = INSTALL_CANCELLED; goto done; }
  /* The journal belongs to the transfer, not to the installed game. */
  steam_engine_destroy(engine);
  engine = NULL;
  if (!path(zip, sizeof(zip), stage, STEAM_ENGINE_JOURNAL))
    goto path_error;
  if (unlink(zip) && errno != ENOENT)
    goto io_error;
  if (!path(zip, sizeof(zip), stage, "autorun-steam-install.ini"))
    goto path_error;
  if (!regular_or_missing(zip))
    goto path_error;
  file = fopen(zip, "wb");
  if (!file)
    goto io_error;
  if (fprintf(file,
              "autorun_steam_install=1\nappid=%u\naccount=%llu\ncompatibility="
              "unknown\n",
              appid, (unsigned long long)backend->account) < 0)
    goto io_error;
  if (fprintf(file, "language=%s\n", backend->language[0] ? backend->language : "english") < 0)
    goto io_error;
  /* What the files take, for the Storage page: it is not counted again. */
  if (fprintf(file, "size=%llu\n", (unsigned long long)v.total) < 0)
    goto io_error;
  if ((launch_exe_found || exe_count == 1) &&
      fprintf(file, "executable=%s\n",
              launch_exe_found ? app.executable : one_exe) < 0)
    goto io_error;
  for (i = 0; i < app.count; ++i)
    if (fprintf(file, "depot.%u=%llu\n", app.depots[i].id,
                (unsigned long long)app.depots[i].manifest) < 0)
      goto io_error;
  if (fflush(file) || fsync(fileno(file)))
    goto io_error;
  if (fclose(file)) {
    file = NULL;
    goto io_error;
  }
  file = NULL;
  if (!parents(final) || !steam_storage_record(root, appid, final) ||
      rename(stage, final))
    goto io_error;
  /* The plan was for the download; left behind, it stood in the way of
   * installing the game again with another set of files. */
  {
    char plan[1100];
    if (snprintf(plan, sizeof(plan), "%s.plan", stage) < (int)sizeof(plan))
      unlink(plan);
  }
  if ((launch_exe_found || exe_count == 1) &&
      !path(v.executable, sizeof(v.executable), final,
            launch_exe_found ? app.executable : one_exe))
    v.executable[0] = 0;
  v.state = STEAM_DOWNLOAD_COMPLETE;
  v.error = 0;
  ok = 1;
  goto done;
path_error:
  v.error = INSTALL_PATH;
  goto done;
io_error:
  v.error = errno == ENOSPC ? INSTALL_DISK : INSTALL_IO;
done:
  steam_cdn_destroy(cdn.pool);
  steam_engine_destroy(engine);
  observe_stop(&observer);
  if (boosted)
    console_for_download(0);
  if (cdn.curl) curl_easy_cleanup(cdn.curl);
  steam_secret_clear(&cdn, sizeof(cdn));
  v.elapsed_ms = millis() - start;
  if(v.error==INSTALL_CORRUPT && !v.integrity_detail)v.integrity_detail=STEAM_INTEGRITY_MANIFEST;
  if (file)
    fclose(file);
  steam_manifest_close(&m);
  free(one_exe);
  free(manifest_paths);
  if (plain) {
    steam_secret_clear(plain, STEAM_CHUNK_LIMIT);
    free(plain);
  }
  steam_secret_clear(keys, sizeof(keys));
  if (v.state != STEAM_DOWNLOAD_COMPLETE && original->discard_requested &&
      original->discard_requested(original->context)) {
    steam_install_discard(root, &v, cb);
    return 0;
  }
  if (v.state != STEAM_DOWNLOAD_COMPLETE) {
    v.failed_stage=(unsigned int)v.state;
    if (!tick(cb)) {
      v.error = steam_cm_error(backend->cm) ? 2000 + steam_cm_error(backend->cm)
                                            : INSTALL_CANCELLED;
    }
    v.state = v.error == INSTALL_CANCELLED ? STEAM_DOWNLOAD_CANCELLED
                                           : STEAM_DOWNLOAD_ERROR;
    if (!v.error)
      v.error = INSTALL_PROTOCOL;
    ok = 0;
  }
  progress(cb, &v);
  return ok;
}

void steam_install_discard(const char *root, struct steam_download_view *v,
                           const struct steam_install_callbacks *cb) {
  uint64_t started = millis();
  struct statvfs disk;
  /* No network tick here: cancellation may coincide with logout or Wi-Fi loss.
   * The caller has joined every writer and released every CDN buffer. */
  v->state = STEAM_DOWNLOAD_CLEANING;
  v->error = v->cleanup_errno = 0;
  v->bytes_per_second = 0;
  v->file[0] = v->executable[0] = 0;
  progress(cb, v);
  v->error = steam_storage_discard(root, v->app_id, &v->cleanup_errno);
  v->elapsed_ms += millis() - started;
  v->free_known = !statvfs(root, &disk);
  if (v->free_known) v->free_bytes = (uint64_t)disk.f_bavail * disk.f_frsize;
  if (v->error) {
    v->state = STEAM_DOWNLOAD_ERROR;
    v->failed_stage = STEAM_DOWNLOAD_CLEANING;
  } else {
    v->state = STEAM_DOWNLOAD_DISCARDED;
    v->failed_stage = 0;
    v->downloaded = v->total = 0;
  }
  progress(cb, v);
}
