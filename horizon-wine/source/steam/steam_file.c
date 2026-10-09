#define _POSIX_C_SOURCE 200809L
#include "steam_file.h"
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
static int kind(const char *p) {
  struct stat st;
  if (lstat(p, &st))
    return errno == ENOENT ? 0 : -1;
  return S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode) ? 1 : -1;
}
static int backup(const char *p, char out[1024]) {
  int n = snprintf(out, 1024, "%s.previous", p);
  return n > 0 && n < 1024;
}
int steam_file_recover(const char *p) {
  char old[1024];
  int k = kind(p);
  if (k < 0 || !backup(p, old))
    return 0;
  if (k)
    return 1;
  k = kind(old);
  if (k < 0)
    return 0;
  return !k || !rename(old, p);
}
int steam_file_replace(const char *tmp, const char *p) {
  char old[1024];
  int k;
  if (kind(tmp) != 1 || !steam_file_recover(p) || !backup(p, old))
    return 0;
  if (!rename(tmp, p))
    return 1;
  k = kind(p);
  if (k != 1 || kind(old) < 0)
    return 0;
  if (unlink(old) && errno != ENOENT)
    return 0;
  if (rename(p, old))
    return 0;
  if (rename(tmp, p)) {
    int error = errno;
    rename(old, p);
    errno = error;
    return 0;
  }
  /* New closed/fsynced metadata is published; keep backup if deletion fails. */
  unlink(old);
  return 1;
}
