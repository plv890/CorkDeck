#define _POSIX_C_SOURCE 200809L
#include "steam_engine.h"
#include "steam_content.h"
#include "steam_wire.h"
#include <SDL.h>
#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
struct steam_engine_file {
  struct steam_engine_file *next;
  char path[1024], relative[384];
  uint64_t size;
  unsigned char sha[20], key[32];
  unsigned int remaining;
  int existing;
  FILE *stream;
};
struct node {
  struct node *next;
  struct steam_engine_file *file;
  struct steam_chunk chunk;
  unsigned char *data;
  unsigned int attempts, detail, format;
};
struct queue {
  struct node *head, *tail;
};
struct entry {
  uint64_t name, size;
  unsigned char sha[20];
};
struct steam_engine {
  SDL_mutex *mutex, *journal_mutex;
  SDL_cond *decode_ready, *write_ready, *verify_ready;
  SDL_Thread *decoders[STEAM_ENGINE_DECODERS], *writer, *verifier;
  unsigned int decoder_count;
  struct queue decode, write, verify, failed;
  struct steam_engine_file *files;
  int stop;
  char stage[1024];
  FILE *journal;
  Uint32 journal_flushed;
  struct entry *entries;
  size_t entry_count;
  struct steam_engine_view view;
};
struct worker {
  struct steam_engine *engine;
  unsigned int core;
};
static unsigned int io_error(void) { return errno == ENOSPC ? 3102 : 3101; }
/* Without this every thread of a homebrew application stays on the core it was
 * created on, and the decoders would share one core with the network. */
static void prefer_core(unsigned int core) {
#ifdef __SWITCH__
  if (R_FAILED(svcSetThreadCoreMask(CUR_THREAD_HANDLE, (s32)(core % 4), 15)))
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, (s32)(core % 3), 7);
#else
  (void)core;
#endif
}
static void push(struct queue *q, struct node *n) {
  n->next = NULL;
  if (q->tail)
    q->tail->next = n;
  else
    q->head = n;
  q->tail = n;
}
static struct node *pop(struct queue *q) {
  struct node *n = q->head;
  if (n) {
    q->head = n->next;
    if (!q->head)
      q->tail = NULL;
  }
  return n;
}
/* Chunks arrive in whatever order the connections finish. The card is much
 * faster when a file is written front to back, so the writer stays with the
 * file it has open and takes that file's lowest waiting offset. */
static struct node *pop_next(struct queue *q,
                             const struct steam_engine_file *current) {
  struct node *n, *before = NULL, *best = NULL, *best_before = NULL;
  for (n = q->head; n; before = n, n = n->next)
    if (n->file == current &&
        (!best || n->chunk.offset < best->chunk.offset)) {
      best = n;
      best_before = before;
    }
  if (!best)
    return pop(q);
  if (best_before)
    best_before->next = best->next;
  else
    q->head = best->next;
  if (q->tail == best)
    q->tail = best_before;
  return best;
}
static void fail(struct steam_engine *e, unsigned int error, unsigned int detail,
                 unsigned int format) {
  if (!e->view.error) {
    e->view.error = error;
    e->view.integrity_detail = detail;
    e->view.chunk_format = format;
  }
  e->stop = 1;
  SDL_CondBroadcast(e->decode_ready);
  SDL_CondBroadcast(e->write_ready);
  SDL_CondBroadcast(e->verify_ready);
}
static uint64_t name_key(const char *s) {
  uint64_t h = 1469598103934665603ull;
  for (; *s; ++s) {
    unsigned char c = (unsigned char)*s;
    if (c >= 'A' && c <= 'Z')
      c += 32;
    h = (h ^ c) * 1099511628211ull;
  }
  return h;
}
static int by_name(const void *a, const void *b) {
  const struct entry *x = a, *y = b;
  return x->name < y->name ? -1 : x->name > y->name;
}
static int hex(const char *s, unsigned char out[20]) {
  unsigned int i;
  for (i = 0; i < 40; ++i) {
    unsigned char c = (unsigned char)s[i];
    unsigned int v = c >= '0' && c <= '9'   ? (unsigned int)(c - '0')
                     : c >= 'a' && c <= 'f' ? (unsigned int)(c - 'a') + 10
                                            : 16;
    if (v > 15)
      return 0;
    out[i / 2] = (unsigned char)(i % 2 ? out[i / 2] | v : v << 4);
  }
  return 1;
}
/* A line cut short by a power loss is simply not an entry. */
static void journal_load(struct steam_engine *e, const char *name) {
  FILE *f = fopen(name, "rb");
  char line[512];
  size_t capacity = 0;
  if (!f)
    return;
  while (fgets(line, sizeof(line), f)) {
    struct entry one;
    char *end, *size = line + 41;
    size_t n = strlen(line);
    if (n < 45 || line[n - 1] != '\n' || line[40] != ' ' || !hex(line, one.sha))
      continue;
    line[n - 1] = 0;
    errno = 0;
    one.size = strtoull(size, &end, 10);
    if (errno || end == size || *end != ' ' || !end[1])
      continue;
    one.name = name_key(end + 1);
    if (e->entry_count == capacity) {
      size_t larger = capacity ? capacity * 2 : 1024;
      struct entry *grown = realloc(e->entries, larger * sizeof(*grown));
      if (!grown)
        break;
      e->entries = grown;
      capacity = larger;
    }
    e->entries[e->entry_count++] = one;
  }
  fclose(f);
  if (e->entry_count)
    qsort(e->entries, e->entry_count, sizeof(*e->entries), by_name);
}
int steam_engine_verified(struct steam_engine *e, const char *relative,
                          uint64_t size, const unsigned char sha[20]) {
  uint64_t name = name_key(relative);
  size_t low = 0, high = e->entry_count;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (e->entries[middle].name < name)
      low = middle + 1;
    else
      high = middle;
  }
  for (; low < e->entry_count && e->entries[low].name == name; ++low)
    if (e->entries[low].size == size && !memcmp(e->entries[low].sha, sha, 20))
      return 1;
  return 0;
}
void steam_engine_note(struct steam_engine *e, const char *relative,
                       uint64_t size, const unsigned char sha[20]) {
  char text[41];
  unsigned int i;
  for (i = 0; i < 20; ++i)
    snprintf(text + i * 2, 3, "%02x", sha[i]);
  SDL_LockMutex(e->journal_mutex);
  if (e->journal) {
    fprintf(e->journal, "%s %llu %s\n", text, (unsigned long long)size,
            relative);
    /* Losing the last second of entries only costs reading those files again. */
    if (SDL_GetTicks() - e->journal_flushed >= 1000) {
      fflush(e->journal);
      e->journal_flushed = SDL_GetTicks();
    }
  }
  SDL_UnlockMutex(e->journal_mutex);
}
static int decode(void *opaque) {
  struct worker *w = opaque;
  struct steam_engine *e = w->engine;
  prefer_core(w->core);
  free(w);
  for (;;) {
    struct node *n;
    unsigned char *plain;
    unsigned int detail, format = 0;
    Uint32 started;
    SDL_LockMutex(e->mutex);
    while (!e->decode.head && !e->stop)
      SDL_CondWait(e->decode_ready, e->mutex);
    if (e->stop) {
      SDL_UnlockMutex(e->mutex);
      return 0;
    }
    n = pop(&e->decode);
    SDL_UnlockMutex(e->mutex);
    started = SDL_GetTicks();
    /* A full heap is the card being slower than the network: wait for it. */
    while (!(plain = memalign(4096, n->chunk.original))) {
      int stop;
      SDL_Delay(20);
      SDL_LockMutex(e->mutex);
      stop = e->stop;
      SDL_UnlockMutex(e->mutex);
      if (stop)
        break;
    }
    detail = plain ? steam_content_chunk_check(
                         n->file->key, n->data, n->chunk.compressed, plain,
                         n->chunk.original, n->chunk.checksum, n->chunk.sha,
                         &format)
                   : STEAM_INTEGRITY_MEMORY;
    free(n->data);
    n->data = NULL;
    SDL_LockMutex(e->mutex);
    e->view.decode_ms += SDL_GetTicks() - started;
    e->view.queued_bytes -= n->chunk.compressed;
    if (detail == STEAM_INTEGRITY_MEMORY) {
      free(plain);
      free(n);
      fail(e, 5, detail, format);
    } else if (detail) {
      free(plain);
      n->detail = detail;
      n->format = format;
      push(&e->failed, n);
    } else {
      n->data = plain;
      e->view.queued_bytes += n->chunk.original;
      push(&e->write, n);
      SDL_CondSignal(e->write_ready);
    }
    SDL_UnlockMutex(e->mutex);
  }
}
static int write_chunks(void *opaque) {
  struct worker *w = opaque;
  struct steam_engine *e = w->engine;
  const struct steam_engine_file *current = NULL;
  prefer_core(w->core);
  free(w);
  for (;;) {
    struct node *n;
    struct steam_engine_file *f;
    unsigned int error = 0;
    int last, several;
    Uint32 started;
    SDL_LockMutex(e->mutex);
    while (!e->write.head && !e->stop)
      SDL_CondWait(e->write_ready, e->mutex);
    if (e->stop) {
      SDL_UnlockMutex(e->mutex);
      return 0;
    }
    n = pop_next(&e->write, current);
    f = n->file;
    several = f->remaining > 1;
    snprintf(e->view.file, sizeof(e->view.file), "%.127s", f->relative);
    SDL_UnlockMutex(e->mutex);
    started = SDL_GetTicks();
    if (!f->stream) {
      f->stream = fopen(f->path, f->existing ? "r+b" : "w+b");
      if (!f->stream && f->existing && errno == ENOENT)
        f->stream = fopen(f->path, "w+b");
      if (!f->stream)
        error = io_error();
      /* Chunks of a large file arrive in any order; giving it its full size
       * first keeps every write inside the file and its clusters together. */
      else if (several && ftruncate(fileno(f->stream), (off_t)f->size))
        error = io_error();
    }
    if (!error && n->data &&
        (fseek(f->stream, (long)n->chunk.offset, SEEK_SET) ||
         fwrite(n->data, 1, n->chunk.original, f->stream) != n->chunk.original))
      error = io_error();
    SDL_LockMutex(e->mutex);
    if (n->data) {
      e->view.queued_bytes -= n->chunk.original;
      if (!error)
        e->view.written += n->chunk.original;
      --f->remaining;
    }
    last = !f->remaining;
    current = last ? NULL : f;
    SDL_UnlockMutex(e->mutex);
    free(n->data);
    n->data = NULL;
    if (!error && last &&
        (fflush(f->stream) || ftruncate(fileno(f->stream), (off_t)f->size)))
      error = io_error();
    SDL_LockMutex(e->mutex);
    e->view.write_ms += SDL_GetTicks() - started;
    if (error) {
      free(n);
      fail(e, error, 0, 0);
    } else if (last) {
      push(&e->verify, n);
      SDL_CondSignal(e->verify_ready);
    } else
      free(n);
    SDL_UnlockMutex(e->mutex);
  }
}
/* Every chunk was checked against the manifest before it was written, which
 * is all the Steam client does; what is left is to make the file durable. */
static int finish(void *opaque) {
  struct worker *w = opaque;
  struct steam_engine *e = w->engine;
  prefer_core(w->core);
  free(w);
  for (;;) {
    struct node *n;
    struct steam_engine_file *f, **link;
    unsigned int error = 0;
    Uint32 started;
    FILE *stream;
    SDL_LockMutex(e->mutex);
    while (!e->verify.head && !e->stop)
      SDL_CondWait(e->verify_ready, e->mutex);
    if (e->stop) {
      SDL_UnlockMutex(e->mutex);
      return 0;
    }
    n = pop(&e->verify);
    SDL_UnlockMutex(e->mutex);
    f = n->file;
    free(n);
    started = SDL_GetTicks();
    stream = f->stream;
    f->stream = NULL;
    if (fsync(fileno(stream)))
      error = io_error();
    if (fclose(stream) && !error)
      error = io_error();
    if (!error)
      steam_engine_note(e, f->relative, f->size, f->sha);
    SDL_LockMutex(e->mutex);
    e->view.sync_ms += SDL_GetTicks() - started;
    if (error)
      fail(e, error, 0, 0);
    else {
      e->view.verified += f->size;
      ++e->view.files;
      --e->view.active_files;
      for (link = &e->files; *link != f; link = &(*link)->next) {
      }
      *link = f->next;
      steam_secret_clear(f->key, sizeof(f->key));
      free(f);
    }
    SDL_UnlockMutex(e->mutex);
  }
}
static SDL_Thread *start(struct steam_engine *e, SDL_ThreadFunction entry,
                         const char *name, unsigned int core, size_t stack) {
  struct worker *w = malloc(sizeof(*w));
  SDL_Thread *thread;
  if (!w)
    return NULL;
  *w = (struct worker){e, core};
  thread = SDL_CreateThreadWithStackSize(entry, name, stack, w);
  if (!thread)
    free(w);
  return thread;
}
struct steam_engine *steam_engine_create(const char *stage,
                                         unsigned int decoders) {
  struct steam_engine *e;
  char name[1100];
  unsigned int i;
  if (!decoders || decoders > STEAM_ENGINE_DECODERS || strlen(stage) >= 1024)
    return NULL;
  e = calloc(1, sizeof(*e));
  if (!e)
    return NULL;
  strcpy(e->stage, stage);
  snprintf(name, sizeof(name), "%s/%s", stage, STEAM_ENGINE_JOURNAL);
  journal_load(e, name);
  e->journal = fopen(name, "ab");
  e->journal_flushed = SDL_GetTicks();
  e->mutex = SDL_CreateMutex();
  e->journal_mutex = SDL_CreateMutex();
  e->decode_ready = SDL_CreateCond();
  e->write_ready = SDL_CreateCond();
  e->verify_ready = SDL_CreateCond();
  if (!e->journal || !e->mutex || !e->journal_mutex || !e->decode_ready ||
      !e->write_ready || !e->verify_ready)
    goto failed;
  /* The caller's network thread keeps core 1 and the interface core 0. */
  e->writer = start(e, write_chunks, "steam-write", 0, 128u * 1024u);
  e->verifier = start(e, finish, "steam-finish", 1, 128u * 1024u);
  if (!e->writer || !e->verifier)
    goto failed;
  for (i = 0; i < decoders; ++i) {
    static const unsigned int cores[STEAM_ENGINE_DECODERS] = {2, 3, 0, 1};
    e->decoders[i] = start(e, decode, "steam-decode", cores[i], 256u * 1024u);
    if (!e->decoders[i])
      goto failed;
    ++e->decoder_count;
  }
  return e;
failed:
  steam_engine_destroy(e);
  return NULL;
}
int steam_engine_busy(struct steam_engine *e, const char *relative) {
  struct steam_engine_file *f;
  int busy = 0;
  SDL_LockMutex(e->mutex);
  for (f = e->files; f && !busy; f = f->next)
    busy = !strcasecmp(f->relative, relative);
  SDL_UnlockMutex(e->mutex);
  return busy;
}
struct steam_engine_file *steam_engine_open(struct steam_engine *e,
                                            const char *relative, uint64_t size,
                                            const unsigned char sha[20],
                                            const unsigned char key[32],
                                            unsigned int chunks, int existing) {
  struct steam_engine_file *f = calloc(1, sizeof(*f));
  struct node *n = NULL;
  if (!f || strlen(relative) >= sizeof(f->relative) ||
      snprintf(f->path, sizeof(f->path), "%s/%s", e->stage, relative) >=
          (int)sizeof(f->path) ||
      (!chunks && !(n = calloc(1, sizeof(*n))))) {
    free(f);
    return NULL;
  }
  strcpy(f->relative, relative);
  f->size = size;
  f->remaining = chunks;
  f->existing = existing;
  memcpy(f->sha, sha, 20);
  memcpy(f->key, key, 32);
  SDL_LockMutex(e->mutex);
  f->next = e->files;
  e->files = f;
  ++e->view.active_files;
  if (n) {
    n->file = f;
    push(&e->write, n);
    SDL_CondSignal(e->write_ready);
  }
  SDL_UnlockMutex(e->mutex);
  return f;
}
int steam_engine_submit(struct steam_engine *e, struct steam_engine_file *f,
                        const struct steam_chunk *chunk,
                        unsigned char *encrypted, unsigned int attempts) {
  struct node *n = calloc(1, sizeof(*n));
  if (!n) {
    free(encrypted);
    return 0;
  }
  n->file = f;
  n->chunk = *chunk;
  n->data = encrypted;
  n->attempts = attempts;
  SDL_LockMutex(e->mutex);
  e->view.queued_bytes += chunk->compressed;
  push(&e->decode, n);
  SDL_CondSignal(e->decode_ready);
  SDL_UnlockMutex(e->mutex);
  return 1;
}
int steam_engine_failed(struct steam_engine *e,
                        struct steam_engine_failure *out) {
  struct node *n;
  SDL_LockMutex(e->mutex);
  n = pop(&e->failed);
  SDL_UnlockMutex(e->mutex);
  if (!n)
    return 0;
  *out = (struct steam_engine_failure){n->file, n->chunk, n->attempts,
                                       n->detail, n->format};
  free(n);
  return 1;
}
void steam_engine_status(struct steam_engine *e, struct steam_engine_view *out) {
  SDL_LockMutex(e->mutex);
  *out = e->view;
  SDL_UnlockMutex(e->mutex);
}
static void drop(struct queue *q) {
  struct node *n;
  while ((n = pop(q))) {
    free(n->data);
    free(n);
  }
}
void steam_engine_destroy(struct steam_engine *e) {
  struct steam_engine_file *f;
  unsigned int i;
  if (!e)
    return;
  if (e->mutex) {
    SDL_LockMutex(e->mutex);
    e->stop = 1;
    if (e->decode_ready)
      SDL_CondBroadcast(e->decode_ready);
    if (e->write_ready)
      SDL_CondBroadcast(e->write_ready);
    if (e->verify_ready)
      SDL_CondBroadcast(e->verify_ready);
    SDL_UnlockMutex(e->mutex);
  }
  for (i = 0; i < e->decoder_count; ++i)
    SDL_WaitThread(e->decoders[i], NULL);
  if (e->writer)
    SDL_WaitThread(e->writer, NULL);
  if (e->verifier)
    SDL_WaitThread(e->verifier, NULL);
  drop(&e->decode);
  drop(&e->write);
  drop(&e->verify);
  drop(&e->failed);
  /* Chunks already written are kept for the next attempt, which checks each
   * one before trusting it. */
  while ((f = e->files)) {
    e->files = f->next;
    if (f->stream) {
      if (!fflush(f->stream))
        fsync(fileno(f->stream));
      fclose(f->stream);
    }
    steam_secret_clear(f->key, sizeof(f->key));
    free(f);
  }
  if (e->journal) {
    if (!fflush(e->journal))
      fsync(fileno(e->journal));
    fclose(e->journal);
  }
  free(e->entries);
  if (e->decode_ready)
    SDL_DestroyCond(e->decode_ready);
  if (e->write_ready)
    SDL_DestroyCond(e->write_ready);
  if (e->verify_ready)
    SDL_DestroyCond(e->verify_ready);
  if (e->journal_mutex)
    SDL_DestroyMutex(e->journal_mutex);
  if (e->mutex)
    SDL_DestroyMutex(e->mutex);
  free(e);
}
