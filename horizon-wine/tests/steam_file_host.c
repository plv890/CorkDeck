#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int (*posix_rename)(const char *, const char *) = rename;
static int fail_publish;
static int horizon_rename(const char *from, const char *to) {
  if (!access(to, F_OK)) {
    errno = EEXIST;
    return -1;
  }
  if (fail_publish && strstr(from, ".tmp")) {
    errno = EIO;
    return -1;
  }
  return posix_rename(from, to);
}
#define rename horizon_rename
#include "../source/steam/steam_file.c"
static void put(const char *p, const char *text) {
  FILE *f = fopen(p, "wb");
  assert(f);
  assert(fputs(text, f) >= 0 && !fclose(f));
}
static void check(const char *p, const char *text) {
  char b[64];
  FILE *f = fopen(p, "rb");
  assert(f && fgets(b, sizeof(b), f));
  fclose(f);
  assert(!strcmp(b, text));
}
int main(void) {
  char root[] = "/tmp/steam-file-XXXXXX", path[256], tmp[256], old[256];
  assert(mkdtemp(root));
  snprintf(path, sizeof(path), "%s/settings", root);
  snprintf(tmp, sizeof(tmp), "%s/settings.tmp", root);
  snprintf(old, sizeof(old), "%s/settings.previous", root);
  put(path, "old");
  put(tmp, "new");
  assert(steam_file_replace(tmp, path));
  check(path, "new");
  put(tmp, "failed");
  fail_publish = 1;
  assert(!steam_file_replace(tmp, path));
  check(path, "new");
  fail_publish = 0;
  assert(!posix_rename(path, old));
  assert(steam_file_recover(path));
  check(path, "new");
  assert(!unlink(path));
  assert(!symlink(tmp, path));
  assert(!steam_file_replace(tmp, path));
  assert(!unlink(path) && !unlink(tmp) && !rmdir(root));
  puts("Horizon-style rename EEXIST replacement, publish failure rollback, "
       "recovery and symlink refusal passed");
  return 0;
}
