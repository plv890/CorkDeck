#ifndef AUTORUN_STEAM_ENGINE_H
#define AUTORUN_STEAM_ENGINE_H
#include "steam_manifest.h"
/* Kept beside the staged files; never part of an installed game. */
#define STEAM_ENGINE_JOURNAL "autorun-steam-verified.journal"
#define STEAM_ENGINE_DECODERS 4u
struct steam_engine;
struct steam_engine_file;
struct steam_engine_view {
  uint64_t written, verified, queued_bytes, decode_ms, write_ms, sync_ms,
      verify_ms;
  unsigned int error, integrity_detail, chunk_format, files, active_files;
  char file[128];
};
struct steam_engine_failure {
  struct steam_engine_file *file;
  struct steam_chunk chunk;
  unsigned int attempts, detail, format;
};
/* Everything behind the network for a whole installation: several decoders,
 * one SD writer and one finisher, so a download never waits at the end of a
 * file. Chunks of many files are in memory at once; the caller bounds them by
 * queued_bytes. Each chunk is decrypted, unpacked and checked against the
 * manifest's Adler-32 and SHA-1 before it is written, as in the Steam client;
 * a file whose chunks are all written is synchronized and entered in the
 * journal, which lets a later attempt skip it without reading it. */
struct steam_engine *steam_engine_create(const char *stage,
                                         unsigned int decoders);
/* 1 when an earlier attempt verified exactly this file. */
int steam_engine_verified(struct steam_engine *, const char *relative,
                          uint64_t size, const unsigned char sha[20]);
void steam_engine_note(struct steam_engine *, const char *relative,
                       uint64_t size, const unsigned char sha[20]);
/* 1 while a file of this name is still being written or verified. */
int steam_engine_busy(struct steam_engine *, const char *relative);
/* chunks: how many will be submitted. Zero still creates, sizes and verifies
 * the file. existing: the file is already on the card and keeps its bytes. */
struct steam_engine_file *steam_engine_open(struct steam_engine *,
                                            const char *relative, uint64_t size,
                                            const unsigned char sha[20],
                                            const unsigned char key[32],
                                            unsigned int chunks, int existing);
/* Takes the malloc'ed encrypted chunk. */
int steam_engine_submit(struct steam_engine *, struct steam_engine_file *,
                        const struct steam_chunk *, unsigned char *encrypted,
                        unsigned int attempts);
/* A chunk that did not decode or match; the caller downloads it again. */
int steam_engine_failed(struct steam_engine *, struct steam_engine_failure *);
void steam_engine_status(struct steam_engine *, struct steam_engine_view *);
/* Stops the workers; data already written stays for the next attempt. */
void steam_engine_destroy(struct steam_engine *);
#endif
