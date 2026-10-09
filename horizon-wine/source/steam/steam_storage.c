#define _POSIX_C_SOURCE 200809L
#include "steam_storage.h"
#include "steam_content.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int dir(const char *p, int create) {
  struct stat s;
  if (!lstat(p, &s))
    return S_ISDIR(s.st_mode) && !S_ISLNK(s.st_mode);
  return create && errno == ENOENT && !mkdir(p, 0777);
}
static int base(const char *root, char out[512], int create) {
  char p[512];
  int n = snprintf(p, sizeof(p), "%s/drive_c", root);
  if (n < 0 || (size_t)n >= sizeof(p) || !dir(p, create))
    return 0;
  n = snprintf(out, 512, "%s/Steam", p);
  return n > 0 && n < 512 && dir(out, create);
}
static int steam_dir(const char *root, const char *sub, char out[512]) {
  char p[512];
  int n = snprintf(p, sizeof(p), "%s/steam", root);
  if (n < 0 || (size_t)n >= sizeof(p) || !dir(p, 1))
    return 0;
  n = snprintf(out, 512, "%s/%s", p, sub);
  return n > 0 && n < 512 && dir(out, 1);
}
static int leaf(const char *p) {
  char copy[256];
  size_t n = strlen(p);
  if (!n || n >= sizeof(copy) || strchr(p, '/') || strchr(p, '\\'))
    return 0;
  memcpy(copy, p, n + 1);
  return steam_content_path(copy);
}
static int component(const char *p) { return strlen(p) <= 180 && leaf(p); }
static int marker(const char *directory, uint32_t app) {
  char p[1024];
  struct stat s;
  FILE *f;
  unsigned int id = 0;
  int version = 0, ok;
  if (!dir(directory, 0) ||
      snprintf(p, sizeof(p), "%s/autorun-steam-install.ini", directory) >=
          (int)sizeof(p) ||
      lstat(p, &s) || !S_ISREG(s.st_mode))
    return 0;
  f = fopen(p, "r");
  if (!f)
    return 0;
  ok = fscanf(f, "autorun_steam_install=%d\nappid=%u", &version, &id) == 2 &&
       version == 1 && id == app;
  fclose(f);
  return ok;
}
static int location(const char *root, uint32_t app, char out[512],
                    int installed) {
  char b[512], p[768], name[192] = {0};
  struct stat s;
  FILE *f;
  int n;
  out[0] = 0;
  if (!app || !base(root, b, 0))
    return 0;
  n = snprintf(p, sizeof(p), "%s/steam/installed/%u.path", root, app);
  if (n < 0 || (size_t)n >= sizeof(p))
    return 0;
  if (!lstat(p, &s) && S_ISREG(s.st_mode) && (f = fopen(p, "r"))) {
    int ok = fgets(name, sizeof(name), f) != NULL;
    fclose(f);
    name[strcspn(name, "\r\n")] = 0;
    if (ok && component(name)) {
      n = snprintf(out, 512, "%s/%s", b, name);
      if (n > 0 && n < 512 && (!installed || marker(out, app)))
        return 1;
    }
  }
  n = snprintf(out, 512, "%s/%u", b, app);
  if (n > 0 && n < 512 && (!installed || marker(out, app)))
    return 1;
  out[0] = 0;
  return 0;
}
int steam_storage_directory(const char *root, uint32_t app, char out[512]) {
  return location(root, app, out, 1);
}
int steam_storage_pending(const char *root, uint32_t app, char out[512]) {
  char p[768], d[512];
  if (snprintf(d, sizeof(d), "%s/steam", root) >= (int)sizeof(d) ||
      !dir(d, 0) || snprintf(p, sizeof(p), "%s/trash", d) >= (int)sizeof(p) ||
      !dir(p, 0))
    return 0;
  if (snprintf(p, sizeof(p), "%s/trash/%u", d, app) >= (int)sizeof(p) ||
      !marker(p, app))
    return 0;
  return location(root, app, out, 0);
}
int steam_storage_plan(const char *root, uint32_t app, const char *title,
                       char out[1024]) {
  char b[512], name[160];
  size_t i, n = 0;
  struct stat s;
  if (!app || !base(root, b, 1))
    return 0;
  for (i = 0; title && title[i] && n < 120; ++i) {
    unsigned char c = (unsigned char)title[i];
    name[n++] = c < 32 || strchr("<>:\"/\\|?*", c) ? '_' : (char)c;
  }
  /* Drop a partial UTF-8 character at the byte limit. */
  if (title && title[i])
    while (n && ((unsigned char)title[i] & 0xc0) == 0x80) {
      --n;
      --i;
    }
  while (n && (name[n - 1] == ' ' || name[n - 1] == '.'))
    --n;
  name[n] = 0;
  if (!component(name))
    snprintf(name, sizeof(name), "Steam game %u", app);
  /* Prefix DOS device names, including forms such as CON.ext. */
  {
    char upper[8] = {0};
    size_t k;
    for (k = 0; k < 7 && name[k] && name[k] != '.'; ++k)
      upper[k] = name[k] >= 'a' && name[k] <= 'z' ? name[k] - 32 : name[k];
    if (!strcmp(upper, "CON") || !strcmp(upper, "PRN") ||
        !strcmp(upper, "AUX") || !strcmp(upper, "NUL") ||
        (!strncmp(upper, "COM", 3) && upper[3] >= '1' && upper[3] <= '9' &&
         !upper[4]) ||
        (!strncmp(upper, "LPT", 3) && upper[3] >= '1' && upper[3] <= '9' &&
         !upper[4])) {
      memmove(name + 1, name, strlen(name) + 1);
      name[0] = '_';
    }
  }
  if (snprintf(out, 1024, "%s/%s", b, name) >= 1024)
    return 0;
  if (lstat(out, &s) && errno == ENOENT)
    return 1;
  if (snprintf(out, 1024, "%s/%s (%u)", b, name, app) >= 1024)
    return 0;
  return lstat(out, &s) && errno == ENOENT;
}
int steam_storage_record(const char *root, uint32_t app,
                         const char *directory) {
  char b[512], d[512], p[768], tmp[780];
  const char *name;
  struct stat s;
  FILE *f;
  int ok;
  if (!base(root, b, 0) || strncmp(directory, b, strlen(b)) ||
      directory[strlen(b)] != '/')
    return 0;
  name = directory + strlen(b) + 1;
  if (!component(name) || !steam_dir(root, "installed", d))
    return 0;
  snprintf(p, sizeof(p), "%s/%u.path", d, app);
  snprintf(tmp, sizeof(tmp), "%s.tmp", p);
  if ((!lstat(p, &s) && !S_ISREG(s.st_mode)) ||
      (!lstat(tmp, &s) && !S_ISREG(s.st_mode)))
    return 0;
  f = fopen(tmp, "wb");
  if (!f)
    return 0;
  ok = fprintf(f, "%s\n", name) > 0 && !fflush(f) && !fsync(fileno(f));
  if (fclose(f))
    ok = 0;
  if (!ok)
    return 0;
  /* The card does not rename over a file that is there, and a record of an
   * installation that is gone may be: an install that reached its last step
   * failed there with error 3101. */
  if (!rename(tmp, p))
    return 1;
  if (unlink(p) && errno != ENOENT)
    return 0;
  return !rename(tmp, p);
}
static int tree(const char *p, unsigned int depth, int erase,
                int (*tick)(void *), void *context) {
  DIR *d;
  struct dirent *e;
  struct stat s;
  char child[1024];
  int ok = 1;
  if ((tick && !tick(context)) || depth > 24 || lstat(p, &s) ||
      S_ISLNK(s.st_mode))
    return 0;
  if (S_ISREG(s.st_mode))
    return !erase || !unlink(p);
  if (!S_ISDIR(s.st_mode) || (d = opendir(p)) == NULL)
    return 0;
  for (;;) {
    errno = 0;
    e = readdir(d);
    if (!e) {
      if (errno)
        ok = 0;
      break;
    }
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    if (erase && !depth && !strcmp(e->d_name, "autorun-steam-install.ini"))
      continue;
    if (!leaf(e->d_name) ||
        snprintf(child, sizeof(child), "%s/%s", p, e->d_name) >=
            (int)sizeof(child) ||
        !tree(child, depth + 1, erase, tick, context)) {
      ok = 0;
      break;
    }
  }
  closedir(d);
  if (ok && erase && !depth) {
    if (snprintf(child, sizeof(child), "%s/autorun-steam-install.ini", p) >=
            (int)sizeof(child) ||
        unlink(child))
      ok = 0;
  }
  return ok && (!erase || !rmdir(p));
}
unsigned int steam_storage_uninstall(const char *root, uint32_t app,
                                     int (*tick)(void *), void *context) {
  char source[512], d[512], trash[768], p[768];
  struct stat s;
  if (!app || !steam_dir(root, "trash", d))
    return 3101;
  snprintf(trash, sizeof(trash), "%s/%u", d, app);
  if (steam_storage_directory(root, app, source)) {
    if (!lstat(trash, &s) || errno != ENOENT ||
        !tree(source, 0, 0, tick, context))
      return 3104;
    if (rename(source, trash))
      return 3101;
  } else if (!marker(trash, app))
    return 3104;
  if (!tree(trash, 0, 0, tick, context) || !tree(trash, 0, 1, tick, context))
    return errno == ENOSPC ? 3102 : 3101;
  snprintf(p, sizeof(p), "%s/steam/installed/%u.path", root, app);
  if (unlink(p) && errno != ENOENT)
    return 3101;
  snprintf(p, sizeof(p), "%s/steam/selected/%u.exe", root, app);
  if (unlink(p) && errno != ENOENT)
    return 3101;
  return 0;
}
static int discard_tree(const char *p, unsigned int depth) {
  struct stat s;
  struct dirent *e;
  DIR *d;
  char child[1024];
  int error = 0;
  if (depth > 24) return ELOOP;
  if (lstat(p, &s)) return errno == ENOENT ? 0 : (errno ? errno : EIO);
  /* Unlink owned leaf links without opening their targets. Ancestor links are
   * refused below, before any deletion, and all producers are already joined. */
  if (S_ISREG(s.st_mode) || S_ISLNK(s.st_mode))
    return unlink(p) ? (errno ? errno : EIO) : 0;
  if (!S_ISDIR(s.st_mode)) return EINVAL;
  d = opendir(p);
  if (!d) return errno ? errno : EIO;
  for (;;) {
    errno = 0;
    e = readdir(d);
    if (!e) {error = errno; break;}
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if (!leaf(e->d_name)) {error = EINVAL; break;}
    if (snprintf(child, sizeof(child), "%s/%s", p, e->d_name) >= (int)sizeof(child)) {
      error = ENAMETOOLONG; break;
    }
    error = discard_tree(child, depth + 1);
    if (error) break;
  }
  if (closedir(d) && !error) error = errno ? errno : EIO;
  if (!error && rmdir(p)) error = errno ? errno : EIO;
  return error;
}
unsigned int steam_storage_discard(const char *root, uint32_t app,
                                   unsigned int *filesystem_error) {
  char directories[3][768], p[1024];
  static const char *suffixes[] = {"", ".plan", ".plan.tmp", ".plan.previous"};
  struct stat s;
  unsigned int i;
  int error = app ? 0 : EINVAL;
  *filesystem_error = 0;
  if (!error) {
    if (lstat(root, &s)) error = errno ? errno : EIO;
    else if (!S_ISDIR(s.st_mode) || S_ISLNK(s.st_mode)) error = EINVAL;
  }
  if (snprintf(directories[0], sizeof(directories[0]), "%s/steam", root) >= (int)sizeof(directories[0]) ||
      snprintf(directories[1], sizeof(directories[1]), "%s/steam/staging", root) >= (int)sizeof(directories[1]) ||
      snprintf(directories[2], sizeof(directories[2]), "%s/steam/manifests", root) >= (int)sizeof(directories[2])) error = ENAMETOOLONG;
  /* Check every ancestor before touching any AppID leaf. Never create folders
   * while cancelling a game that has not written anything yet. */
  for (i = 0; !error && i < 3; ++i) {
    if (lstat(directories[i], &s)) {
      if (errno != ENOENT) error = errno ? errno : EIO;
    } else if (!S_ISDIR(s.st_mode) || S_ISLNK(s.st_mode)) error = EINVAL;
  }
  for (i = 0; !error && i < sizeof(suffixes)/sizeof(*suffixes); ++i) {
    if (snprintf(p, sizeof(p), "%s/%u%s", directories[1], app, suffixes[i]) >= (int)sizeof(p)) error = ENAMETOOLONG;
    else error = discard_tree(p, 0);
  }
  if (!error) {
    if (snprintf(p, sizeof(p), "%s/%u", directories[2], app) >= (int)sizeof(p)) error = ENAMETOOLONG;
    else error = discard_tree(p, 0);
  }
  *filesystem_error = (unsigned int)error;
  return error ? 3112 : 0;
}
