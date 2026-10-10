#define _POSIX_C_SOURCE 200809L
#include "steam_prerequisites.h"
#include "../launcher_pe.h"
#include "steam_content.h"
#include "steam_file.h"
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
static const struct steam_prereq_catalog catalog[2] = {
    {"Visual C++ v14 (2015-2022), x86", "VC14-x86.exe",
     "https://download.visualstudio.microsoft.com/download/pr/"
     "bd1c8d9d-ba95-4eee-bc6e-df1fcc876373/"
     "0C09F2611660441084CE0DF425C51C11E147E6447963C3690F97E0B25C55ED64/"
     "VC_redist.x86.exe",
     "0c09f2611660441084ce0df425c51c11e147e6447963c3690f97e0b25c55ed64",
     13953392, 0x14c},
    {"Visual C++ v14 (2015-2022), x64", "VC14-x64.exe",
     "https://download.visualstudio.microsoft.com/download/pr/"
     "bd1c8d9d-ba95-4eee-bc6e-df1fcc876373/"
     "CC0FF0EB1DC3F5188AE6300FAEF32BF5BEEBA4BDD6E8E445A9184072096B713B/"
     "VC_redist.x64.exe",
     "cc0ff0eb1dc3f5188ae6300faef32bf5beeba4bdd6e8e445a9184072096b713b",
     25635768, 0x14c}};
const struct steam_prereq_catalog *steam_prereq_catalog(unsigned int arch) {
  return arch < 2 ? catalog + arch : NULL;
}
static int regular(const char *p) {
  struct stat s;
  return !lstat(p, &s) && S_ISREG(s.st_mode) && !S_ISLNK(s.st_mode);
}
static int directory(const char *p, int create) {
  struct stat s;
  if (!lstat(p, &s))
    return S_ISDIR(s.st_mode) && !S_ISLNK(s.st_mode);
  return create && errno == ENOENT && !mkdir(p, 0700);
}
static int base(const char *root, char out[512], int create) {
  char p[512];
  int n;
  n = snprintf(p, sizeof(p), "%s/drive_c", root);
  if (n < 1 || n >= 512 || !directory(p, 0))
    return 0;
  n = snprintf(out, 512, "%s/windows", p);
  if (n < 1 || n >= 512 || !directory(out, 0))
    return 0;
  strcpy(p, out);
  n = snprintf(out, 512, "%s/AutorunPrerequisites", p);
  return n > 0 && n < 512 && directory(out, create);
}
static int path(const char *root, const char *leaf, char out[512], int create) {
  char p[512];
  int n;
  if (!base(root, p, create))
    return 0;
  n = snprintf(out, 512, "%s/%s", p, leaf);
  return n > 0 && n < 512;
}
/* The libraries of Microsoft's Visual C++ 2015-2022 package, MFC among them. */
static int vc14_library(const char *name) {
  return !strncasecmp(name, "vcruntime140", 12) ||
         !strncasecmp(name, "msvcp140", 8) || !strncasecmp(name, "mfc140", 6) ||
         !strncasecmp(name, "mfcm140", 7) || !strcasecmp(name, "concrt140.dll") ||
         !strcasecmp(name, "vcomp140.dll") || !strcasecmp(name, "vccorlib140.dll") ||
         !strcasecmp(name, "vcamp140.dll");
}
static unsigned int need(const char *name) {
  /* An adjacent API DLL alone does not provide the Steam client interfaces.
   * This is an advisory dependency, not an ownership or readiness verdict. */
  if (!strcasecmp(name, "steam_api.dll") ||
      !strcasecmp(name, "steam_api64.dll"))
    return STEAM_PREREQ_STEAMWORKS;
  if (!strcasecmp(name, "vcruntime140.dll") ||
      !strncasecmp(name, "vcruntime140_", 13) ||
      !strcasecmp(name, "msvcp140.dll") || !strncasecmp(name, "msvcp140_", 9) ||
      !strcasecmp(name, "concrt140.dll") || vc14_library(name))
    return STEAM_PREREQ_VC14;
  if (!strncasecmp(name, "msvcr", 5) || !strncasecmp(name, "msvcp", 5))
    return STEAM_PREREQ_LEGACY_VC;
  if (!strncasecmp(name, "d3dx", 4) || !strncasecmp(name, "xinput1_3", 9) ||
      !strncasecmp(name, "xaudio2_7", 9))
    return STEAM_PREREQ_DX_HELPERS;
  if (!strcasecmp(name, "mscoree.dll"))
    return STEAM_PREREQ_DOTNET;
  return 0;
}
/* steam_prereq_missing listens to the names pe_imports reads. Called from the
 * interface's thread only. */
static void (*import_seen)(void *, const char *);
static void *import_context;
static int pe_imports(const char *file, struct steam_prereq_requirements *v,
                      int main) {
  struct launcher_pe pe;
  unsigned char dos[64], nt[264], desc[32];
  unsigned int dir, magic, count, machine, i, k;
  uint32_t off, rva, size;
  if (!regular(file) || !launcher_pe_open(&pe, file))
    return 0;
  if (!launcher_pe_read_at(&pe, 0, dos, sizeof(dos)))
    goto fail;
  off = launcher_u32(dos + 60);
  if (!launcher_pe_read_at(&pe, off, nt, 26))
    goto fail;
  magic = launcher_u16(nt + 24);
  dir = magic == 0x10b ? 96 : 112;
  machine = launcher_u16(nt + 4);
  if ((machine != 0x14c && machine != 0x8664) ||
      launcher_u16(nt + 20) < dir + 8 ||
      !launcher_pe_read_at(&pe, off, nt, 24 + dir + 8))
    goto fail;
  count = launcher_u32(nt + 24 + dir - 4);
  if (main)
    v->architecture = machine == 0x8664 ? 1 : 0;
  ++v->files;
  for (k = 0; k < 2; ++k) {
    unsigned int entry = k ? 13 : 1, step = k ? 32 : 20;
    if (count <= entry || launcher_u16(nt + 20) < dir + (entry + 1) * 8)
      continue;
    if (!launcher_pe_read_at(&pe, (uint64_t)off + 24 + dir + entry * 8, desc,
                             8))
      goto fail;
    rva = launcher_u32(desc);
    size = launcher_u32(desc + 4);
    if (!rva)
      continue;
    if (size > 512 * step) {
      v->limited = 1;
      size = 512 * step;
    }
    for (i = 0; i < size / step; ++i) {
      char name[128];
      uint32_t nr;
      unsigned int j;
      if ((uint64_t)rva + i * step > UINT32_MAX ||
          !launcher_pe_read_rva(&pe, rva + i * step, desc, step))
        goto fail;
      nr = launcher_u32(desc + (k ? 4 : 12));
      if (!nr)
        break;
      /* Only RVA-form delay import descriptors. */
      if (k && launcher_u32(desc) != 1) {
        v->limited = 1;
        continue;
      }
      for (j = 0; j < sizeof(name); ++j) {
        if ((uint64_t)nr + j > UINT32_MAX ||
            !launcher_pe_read_rva(&pe, nr + j, name + j, 1))
          goto fail;
        if (!name[j])
          break;
      }
      if (j == sizeof(name))
        goto fail;
      v->needs |= need(name);
      if (import_seen)
        import_seen(import_context, name);
    }
  }
  launcher_pe_close(&pe);
  return 1;
fail:
  launcher_pe_close(&pe);
  v->limited = 1;
  return 0;
}
/* Recognize prerequisite hints only; arbitrary script operations and arguments
 * are never interpreted. Steam's signed-script execution is not implemented. */
static void script_hints(const char *file,
                         struct steam_prereq_requirements *v) {
  FILE *f;
  char text[32769];
  struct stat s;
  size_t n, i;
  if (lstat(file, &s) || !S_ISREG(s.st_mode) || s.st_size > 32768) {
    v->limited = 1;
    return;
  }
  if (!(f = fopen(file, "rb"))) {
    v->limited = 1;
    return;
  }
  n = fread(text, 1, sizeof(text) - 1, f);
  fclose(f);
  text[n] = 0;
  for (i = 0; i < n; ++i)
    if (text[i] >= 'A' && text[i] <= 'Z')
      text[i] += 'a' - 'A';
  v->script = 1;
  if (strstr(text, "vcredist") || strstr(text, "vc_redist")) {
    if (strstr(text, "2015") || strstr(text, "2017") || strstr(text, "2019") ||
        strstr(text, "2022"))
      v->needs |= STEAM_PREREQ_VC14;
    else
      v->needs |= STEAM_PREREQ_LEGACY_VC;
  }
  if (strstr(text, "dxsetup") || strstr(text, "directx"))
    v->needs |= STEAM_PREREQ_DX_HELPERS;
  if (strstr(text, "dotnet") || strstr(text, ".net"))
    v->needs |= STEAM_PREREQ_DOTNET;
  if (strstr(text, "physx") || strstr(text, "openal") ||
      strstr(text, "ue4prereq") || strstr(text, "ueprereq"))
    v->needs |= STEAM_PREREQ_CUSTOM;
}
struct missing_scan {
  struct steam_prereq_missing *out;
  char folder[512], system[512];
};
static void missing_seen(void *opaque, const char *name) {
  struct missing_scan *m = opaque;
  char p[768];
  unsigned int i;
  /* API sets are names the loader resolves itself, not files. */
  if (!strncasecmp(name, "api-ms-", 7) || !strncasecmp(name, "ext-ms-", 7) ||
      strchr(name, '/') || strchr(name, '\\'))
    return;
  if (snprintf(p, sizeof(p), "%s/%s", m->folder, name) < (int)sizeof(p) && regular(p))
    return;
  if (snprintf(p, sizeof(p), "%s/%s", m->system, name) < (int)sizeof(p) && regular(p))
    return;
  for (i = 0; i < m->out->count; ++i)
    if (!strcasecmp(m->out->names[i], name))
      return;
  if (vc14_library(name))
    m->out->vc14 = 1;
  if (m->out->count == sizeof(m->out->names) / sizeof(m->out->names[0]) ||
      strlen(name) >= sizeof(m->out->names[0])) {
    m->out->limited = 1;
    return;
  }
  strcpy(m->out->names[m->out->count++], name);
}
int steam_prereq_missing(const char *root, const char *exe,
                         struct steam_prereq_missing *out) {
  struct steam_prereq_requirements v = {0};
  struct missing_scan m = {.out = out};
  char *slash;
  int read;
  memset(out, 0, sizeof(*out));
  if (strlen(exe) >= sizeof(m.folder))
    return 0;
  strcpy(m.folder, exe);
  if (!(slash = strrchr(m.folder, '/')))
    return 0;
  *slash = 0;
  /* The file says which of the two it is; the folder follows from that. */
  v.architecture = 2;
  read = pe_imports(exe, &v, 1);
  if (!read || v.architecture > 1)
    return 0;
  out->architecture = v.architecture;
  if (snprintf(m.system, sizeof(m.system), "%s/drive_c/windows/%s", root,
               v.architecture ? "system32" : "syswow64") >= (int)sizeof(m.system) ||
      !directory(m.system, 0))
    return 0;
  memset(&v, 0, sizeof(v));
  import_seen = missing_seen;
  import_context = &m;
  read = pe_imports(exe, &v, 1);
  import_seen = NULL;
  out->limited |= v.limited;
  return read;
}
/* One automatic attempt for an architecture: a second would end as the first. */
int steam_prereq_tried(const char *root, unsigned int arch, int mark) {
  char leaf[64], p[512];
  snprintf(leaf, sizeof(leaf), "VC14-%s.added", arch ? "x64" : "x86");
  if (!path(root, leaf, p, mark))
    return 0;
  if (mark) {
    FILE *f = fopen(p, "wb");
    if (f)
      fclose(f);
    return f != NULL;
  }
  return regular(p);
}
#include <dirent.h>
unsigned int steam_prereq_inspect(const char *game, const char *exe,
                                  const char *script,
                                  struct steam_prereq_requirements *v) {
  char p[512], folder[512], relative[384];
  DIR *d;
  struct dirent *e;
  size_t n;
  unsigned int seen = 0;
  memset(v, 0, sizeof(*v));
  v->architecture = 2;
  n = strlen(game);
  if (!directory(game, 0))
    return STEAM_PREREQ_PATH;
  if (exe && *exe) {
    if (strncmp(exe, game, n) || exe[n] != '/' ||
        strlen(exe + n + 1) >= sizeof(relative))
      return STEAM_PREREQ_PATH;
    strcpy(relative, exe + n + 1);
    if (!steam_content_path(relative))
      return STEAM_PREREQ_PATH;
    /* Reject directory symlinks along the selected relative path. */
    snprintf(folder, sizeof(folder), "%s", game);
    for (char *part = relative, *end;; part = end + 1) {
      end = strchr(part, '/');
      if (!end)
        break;
      *end = 0;
      if (snprintf(p, sizeof(p), "%s/%s", folder, part) >= 512 ||
          !directory(p, 0))
        return STEAM_PREREQ_PATH;
      strcpy(folder, p);
    }
    pe_imports(exe, v, 1);
    if ((d = opendir(folder))) {
      while ((e = readdir(d))) {
        if (++seen > 256) {
          v->limited = 1;
          break;
        }
        size_t len = strlen(e->d_name);
        if (len < 5 || strcasecmp(e->d_name + len - 4, ".dll"))
          continue;
        if (v->files >= 32) {
          v->limited = 1;
          continue;
        }
        if (snprintf(p, sizeof(p), "%s/%s", folder, e->d_name) < 512)
          pe_imports(p, v, 0);
      }
      closedir(d);
    } else
      v->limited = 1;
  }
  /* Only path-safe scripts from metadata, or conventional root installscript.
   */
  if (script && *script) {
    if (strlen(script) >= sizeof(relative))
      return STEAM_PREREQ_PATH;
    strcpy(relative, script);
    if (!steam_content_path(relative))
      return STEAM_PREREQ_PATH;
    char *last = relative;
    strcpy(folder, game);
    for (char *part = relative, *end;; part = end + 1) {
      end = strchr(part, '/');
      if (!end)
        break;
      *end = 0;
      last = end + 1;
      if (snprintf(p, sizeof(p), "%s/%s", folder, part) >= 512 ||
          !directory(p, 0))
        return STEAM_PREREQ_PATH;
      strcpy(folder, p);
    }
    if (snprintf(p, sizeof(p), "%s/%s", folder, last) < 512)
      script_hints(p, v);
  } else if (snprintf(p, sizeof(p), "%s/installscript.vdf", game) < 512 &&
             regular(p))
    script_hints(p, v);
  return 0;
}
static int hash(const char *p, const struct steam_prereq_catalog *c) {
  FILE *f;
  struct stat s;
  unsigned char b[32768], digest[32];
  char hex[65];
  size_t n;
  int ok = 0;
  mbedtls_sha256_context h;
  if (lstat(p, &s) || !S_ISREG(s.st_mode) || (uint64_t)s.st_size != c->size ||
      !(f = fopen(p, "rb")))
    return 0;
  mbedtls_sha256_init(&h);
  if (mbedtls_sha256_starts_ret(&h, 0))
    goto done;
  while ((n = fread(b, 1, sizeof(b), f)))
    if (mbedtls_sha256_update_ret(&h, b, n))
      goto done;
  if (ferror(f) || mbedtls_sha256_finish_ret(&h, digest))
    goto done;
  for (n = 0; n < 32; ++n)
    snprintf(hex + n * 2, 3, "%02x", digest[n]);
  ok = !strcmp(hex, c->sha256);
done:
  mbedtls_sha256_free(&h);
  fclose(f);
  return ok;
}
int steam_prereq_cached(const char *root, unsigned int arch) {
  char p[512];
  const struct steam_prereq_catalog *c = steam_prereq_catalog(arch);
  return c && path(root, c->file, p, 0) && hash(p, c);
}
/* --- Taking the libraries out of Microsoft's package ---
 *
 * The package is an installer that needs Windows' own installer service, which
 * is not there to run it. What it would put in the Windows folder is inside it
 * as plain cabinets: the program with a cabinet attached, whose files are more
 * cabinets, whose files are the libraries under their own names with the
 * architecture after an underscore (mfc140u.dll_x86). Every cabinet found is
 * opened, to three deep, and each library of the package's family that the
 * Windows folder does not have is written there. Nothing that is there is
 * replaced: Wine's own builds of the common ones stay as they are.
 *
 * Stored and MSZIP folders are read; a cabinet in parts or in another
 * compression is passed over. The package was checked against its pinned
 * SHA-256 before this, so the cabinets' own checksums are not looked at. */
#include <zlib.h>
#define CAB_LIMIT (96u * 1024u * 1024u)
struct cab {
  const unsigned char *data;
  size_t size;
  unsigned int folders, files, reserve_folder, reserve_data;
  size_t folder_at, file_at;
};
struct cab_file {
  char name[128];
  size_t size, offset;
  unsigned int folder;
};
struct unpacking {
  const char *system;
  unsigned int arch, added, kept, error;
};
static unsigned int cab16(const unsigned char *p) { return p[0] | p[1] << 8; }
static size_t cab32(const unsigned char *p) {
  return (size_t)p[0] | (size_t)p[1] << 8 | (size_t)p[2] << 16 | (size_t)p[3] << 24;
}
static int cab_open(const unsigned char *data, size_t size, struct cab *c) {
  size_t p = 36, whole;
  unsigned int flags;
  memset(c, 0, sizeof(*c));
  if (size < 40 || memcmp(data, "MSCF", 4) || cab32(data + 4) || cab32(data + 12) ||
      cab32(data + 20) || data[25] != 1)
    return 0;
  whole = cab32(data + 8);
  flags = cab16(data + 30);
  /* A cabinet that goes on in another file is not one this reads. */
  if (whole < 40 || whole > size || (flags & 3))
    return 0;
  c->data = data;
  c->size = whole;
  c->file_at = cab32(data + 16);
  c->folders = cab16(data + 26);
  c->files = cab16(data + 28);
  if (flags & 4) {
    if (p + 4 > whole)
      return 0;
    c->reserve_folder = data[p + 2];
    c->reserve_data = data[p + 3];
    p += 4 + cab16(data + p);
  }
  c->folder_at = p;
  if (!c->folders || !c->files || c->folders > 256 ||
      p > whole || (size_t)c->folders * (8 + c->reserve_folder) > whole - p ||
      c->file_at < p || c->file_at >= whole)
    return 0;
  return 1;
}
/* The files in order; *at starts as 0. Returns 0 at the end or at a file that cannot be read. */
static int cab_next(const struct cab *c, size_t *at, unsigned int *index, struct cab_file *f) {
  size_t p = *at ? *at : c->file_at, n;
  if (*index >= c->files || p + 17 > c->size)
    return 0;
  f->size = cab32(c->data + p);
  f->offset = cab32(c->data + p + 4);
  f->folder = cab16(c->data + p + 8);
  p += 16;
  for (n = 0; p + n < c->size && c->data[p + n]; ++n) {
  }
  if (p + n >= c->size || n >= sizeof(f->name))
    return 0;
  memcpy(f->name, c->data + p, n + 1);
  *at = p + n + 1;
  ++*index;
  return 1;
}
/* A folder's data whole, as far as its files reach. NULL when it cannot be read. */
static unsigned char *cab_folder(const struct cab *c, unsigned int folder, size_t *size) {
  const unsigned char *entry = c->data + c->folder_at + (size_t)folder * (8 + c->reserve_folder);
  size_t p = cab32(entry), need = 0, used = 0, at = 0;
  unsigned int blocks = cab16(entry + 4), type = cab16(entry + 6) & 15, i, index = 0;
  struct cab_file f;
  unsigned char *out;
  z_stream z;
  if (type > 1)
    return NULL;
  while (cab_next(c, &at, &index, &f))
    if (f.folder == folder && f.offset + f.size > need)
      need = f.offset + f.size;
  if (index != c->files || !need || need > CAB_LIMIT || !(out = malloc(need)))
    return NULL;
  memset(&z, 0, sizeof(z));
  if (type && inflateInit2(&z, -15) != Z_OK) {
    free(out);
    return NULL;
  }
  for (i = 0; i < blocks && used < need; ++i) {
    size_t packed, plain;
    if (p + 8 + c->reserve_data > c->size)
      goto fail;
    packed = cab16(c->data + p + 4);
    plain = cab16(c->data + p + 6);
    p += 8 + c->reserve_data;
    if (packed > c->size - p || plain > need - used)
      goto fail;
    if (!type) {
      if (packed != plain)
        goto fail;
      memcpy(out + used, c->data + p, plain);
    } else {
      /* Each block is a stream of its own that may look back into the 32 KB before it. */
      size_t back = used < 32768 ? used : 32768;
      if (packed < 2 || c->data[p] != 'C' || c->data[p + 1] != 'K' || inflateReset(&z) != Z_OK ||
          (back && inflateSetDictionary(&z, out + used - back, (uInt)back) != Z_OK))
        goto fail;
      z.next_in = (Bytef *)(c->data + p + 2);
      z.avail_in = (uInt)(packed - 2);
      z.next_out = out + used;
      z.avail_out = (uInt)plain;
      if (inflate(&z, Z_FINISH) != Z_STREAM_END || z.avail_out)
        goto fail;
    }
    used += plain;
    p += packed;
  }
  if (used < need)
    goto fail;
  if (type)
    inflateEnd(&z);
  *size = used;
  return out;
fail:
  if (type)
    inflateEnd(&z);
  free(out);
  return NULL;
}
/* "mfc140u.dll_x86" for the 32-bit Windows folder is mfc140u.dll. */
static int unpack_name(const char *name, unsigned int arch, char out[64]) {
  const char *mark = strrchr(name, '_');
  size_t n;
  if (!mark || strchr(name, '/') || strchr(name, '\\'))
    return 0;
  if (arch ? strcasecmp(mark + 1, "amd64") && strcasecmp(mark + 1, "x64") : strcasecmp(mark + 1, "x86"))
    return 0;
  n = (size_t)(mark - name);
  if (n < 5 || n >= 64 || strncasecmp(mark - 4, ".dll", 4))
    return 0;
  memcpy(out, name, n);
  out[n] = 0;
  return vc14_library(out);
}
static void unpack_file(struct unpacking *u, const char *name, const unsigned char *data, size_t size) {
  char p[768], tmp[784];
  FILE *f;
  int ok;
  if (snprintf(p, sizeof(p), "%s/%s", u->system, name) >= (int)sizeof(p))
    return;
  if (regular(p)) {
    ++u->kept;
    return;
  }
  snprintf(tmp, sizeof(tmp), "%s.corkdeck-new", p);
  if (!(f = fopen(tmp, "wb"))) {
    u->error = STEAM_PREREQ_FILESYSTEM;
    return;
  }
  ok = fwrite(data, 1, size, f) == size && !fflush(f) && !fsync(fileno(f));
  if (fclose(f))
    ok = 0;
  if (ok && !rename(tmp, p))
    ++u->added;
  else {
    unlink(tmp);
    u->error = STEAM_PREREQ_FILESYSTEM;
  }
}
static void unpack_cab(struct unpacking *u, const struct cab *c, int depth) {
  unsigned int folder;
  for (folder = 0; folder < c->folders && !u->error; ++folder) {
    size_t size = 0, at = 0;
    unsigned int index = 0;
    struct cab_file f;
    unsigned char *data = cab_folder(c, folder, &size);
    if (!data)
      continue;
    while (!u->error && cab_next(c, &at, &index, &f)) {
      struct cab inner;
      char name[64];
      if (f.folder != folder || f.offset > size || f.size > size - f.offset)
        continue;
      if (unpack_name(f.name, u->arch, name))
        unpack_file(u, name, data + f.offset, f.size);
      else if (depth < 3 && cab_open(data + f.offset, f.size, &inner))
        unpack_cab(u, &inner, depth + 1);
    }
    free(data);
  }
}
static unsigned int unpack_package(const char *package, const char *system, unsigned int arch,
                                   unsigned int *added, unsigned int *kept) {
  struct unpacking u = {.system = system, .arch = arch};
  struct stat s;
  unsigned char *data;
  size_t size, at;
  FILE *f;
  *added = *kept = 0;
  if (lstat(package, &s) || !S_ISREG(s.st_mode) || s.st_size < 64 || (uint64_t)s.st_size > CAB_LIMIT)
    return STEAM_PREREQ_INTEGRITY;
  size = (size_t)s.st_size;
  if (!(data = malloc(size)))
    return STEAM_PREREQ_SPACE;
  if (!(f = fopen(package, "rb")) || fread(data, 1, size, f) != size) {
    if (f)
      fclose(f);
    free(data);
    return STEAM_PREREQ_FILESYSTEM;
  }
  fclose(f);
  for (at = 0; at + 40 <= size && !u.error;) {
    struct cab c;
    const unsigned char *found = memchr(data + at, 'M', size - at - 39);
    if (!found)
      break;
    at = (size_t)(found - data);
    if (cab_open(data + at, size - at, &c)) {
      unpack_cab(&u, &c, 1);
      at += c.size;
    } else
      ++at;
  }
  free(data);
  *added = u.added;
  *kept = u.kept;
  /* A package none of whose libraries could be found was not read at all. */
  return u.error ? u.error : u.added + u.kept ? 0 : STEAM_PREREQ_INTEGRITY;
}
unsigned int steam_prereq_unpack(const char *root, unsigned int arch, unsigned int *added, unsigned int *kept) {
  const struct steam_prereq_catalog *c = steam_prereq_catalog(arch);
  char package[512], system[512];
  struct statvfs v;
  *added = *kept = 0;
  if (!c || !path(root, c->file, package, 0) || !steam_prereq_cached(root, arch))
    return STEAM_PREREQ_INTEGRITY;
  if (snprintf(system, sizeof(system), "%s/drive_c/windows/%s", root, arch ? "system32" : "syswow64") >=
          (int)sizeof(system) || !directory(system, 0))
    return STEAM_PREREQ_PATH;
  if (!statvfs(system, &v) && (uint64_t)v.f_bavail * v.f_frsize < 64u * 1024u * 1024u)
    return STEAM_PREREQ_SPACE;
  return unpack_package(package, system, arch, added, kept);
}
struct transfer {
  FILE *file;
  mbedtls_sha256_context sha;
  struct steam_prereq_progress view;
  const struct steam_prereq_callbacks *cb;
};
static void progress(struct transfer *t) {
  if (t->cb && t->cb->progress)
    t->cb->progress(t->cb->context, &t->view);
}
static int cancel(void *opaque, curl_off_t a, curl_off_t b, curl_off_t c,
                  curl_off_t d) {
  struct transfer *t = opaque;
  (void)a;
  (void)b;
  (void)c;
  (void)d;
  return t->cb && t->cb->cancel && t->cb->cancel(t->cb->context);
}
static size_t receive(void *data, size_t size, size_t count, void *opaque) {
  struct transfer *t = opaque;
  size_t n;
  if (size && count > SIZE_MAX / size)
    return 0;
  n = size * count;
  if (n > t->view.total - t->view.downloaded) {
    t->view.error = STEAM_PREREQ_INTEGRITY;
    return 0;
  }
  if (fwrite(data, 1, n, t->file) != n) {
    t->view.error = STEAM_PREREQ_FILESYSTEM;
    return 0;
  }
  if (mbedtls_sha256_update_ret(&t->sha, data, n)) {
    t->view.error = STEAM_PREREQ_INTEGRITY;
    return 0;
  }
  t->view.downloaded += n;
  progress(t);
  return n;
}
unsigned int steam_prereq_fetch(const char *root, unsigned int arch,
                                const struct steam_prereq_callbacks *cb) {
  const struct steam_prereq_catalog *c = steam_prereq_catalog(arch);
  struct transfer t = {.cb = cb};
  char p[512], tmp[520], hex[65];
  unsigned char digest[32];
  CURL *curl = NULL;
  CURLcode code = CURLE_FAILED_INIT;
  long http = 0;
  int fd = -1;
  struct statvfs fs;
  unsigned int i;
  t.view.error = STEAM_PREREQ_PATH;
  if (!c || !path(root, c->file, p, 1))
    goto done;
  t.view.total = c->size;
  if (steam_prereq_cached(root, arch)) {
    t.view.downloaded = c->size;
    t.view.error = 0;
    progress(&t);
    return 0;
  }
  {
    char dir[512];
    if (base(root, dir, 0) && !statvfs(dir, &fs) && fs.f_frsize &&
        ((uint64_t)fs.f_bavail < c->size / fs.f_frsize + 2)) {
      t.view.error = STEAM_PREREQ_SPACE;
      goto done;
    }
  }
  snprintf(tmp, sizeof(tmp), "%s.part", p);
  unlink(tmp);
  fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) {
    t.view.error = STEAM_PREREQ_FILESYSTEM;
    goto done;
  }
  t.file = fdopen(fd, "wb");
  if (!t.file) {
    close(fd);
    unlink(tmp);
    t.view.error = STEAM_PREREQ_FILESYSTEM;
    goto done;
  }
  mbedtls_sha256_init(&t.sha);
  if (mbedtls_sha256_starts_ret(&t.sha, 0)) {
    t.view.error = STEAM_PREREQ_INTEGRITY;
    goto finish;
  }
  t.view.error = STEAM_PREREQ_NETWORK;
  if (!(curl = curl_easy_init()))
    goto finish;
#define OPT(k, v)                                                              \
  do {                                                                         \
    if ((code = curl_easy_setopt(curl, k, v)) != CURLE_OK)                     \
      goto finish;                                                             \
  } while (0)
  OPT(CURLOPT_URL, c->url);
  OPT(CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
  OPT(CURLOPT_FOLLOWLOCATION, 0L);
  OPT(CURLOPT_PROXY, "");
  OPT(CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);
  OPT(CURLOPT_SSL_VERIFYPEER, 1L);
  OPT(CURLOPT_SSL_VERIFYHOST, 2L);
  OPT(CURLOPT_CONNECTTIMEOUT, 20L);
  OPT(CURLOPT_LOW_SPEED_LIMIT, 1024L);
  OPT(CURLOPT_LOW_SPEED_TIME, 45L);
  OPT(CURLOPT_TIMEOUT, 300L);
  OPT(CURLOPT_NOSIGNAL, 1L);
  OPT(CURLOPT_WRITEFUNCTION, receive);
  OPT(CURLOPT_WRITEDATA, &t);
  OPT(CURLOPT_XFERINFOFUNCTION, cancel);
  OPT(CURLOPT_XFERINFODATA, &t);
  OPT(CURLOPT_NOPROGRESS, 0L);
  t.view.error = 0;
  code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
  t.view.http = (unsigned int)http;
  t.view.transport = (unsigned int)code;
  if (cancel(&t, 0, 0, 0, 0))
    t.view.error = STEAM_PREREQ_CANCELLED;
  else if (!t.view.error && (code != CURLE_OK || http != 200))
    t.view.error = STEAM_PREREQ_NETWORK;
  if (!t.view.error) {
    if (t.view.downloaded != c->size ||
        mbedtls_sha256_finish_ret(&t.sha, digest))
      t.view.error = STEAM_PREREQ_INTEGRITY;
    else {
      for (i = 0; i < 32; ++i)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
      if (strcmp(hex, c->sha256))
        t.view.error = STEAM_PREREQ_INTEGRITY;
    }
  }
finish:
  if (curl)
    curl_easy_cleanup(curl);
  mbedtls_sha256_free(&t.sha);
  {
    int ok = !fflush(t.file);
    if (ok)
      ok = !fsync(fileno(t.file));
    if (fclose(t.file))
      ok = 0;
    t.file = NULL;
    if (!ok)
      t.view.error = STEAM_PREREQ_FILESYSTEM;
  }
  if (!t.view.error && !hash(tmp, c))
    t.view.error = STEAM_PREREQ_INTEGRITY;
  if (!t.view.error && !steam_file_replace(tmp, p))
    t.view.error = STEAM_PREREQ_FILESYSTEM;
  if (t.view.error)
    unlink(tmp);
done:
  progress(&t);
  return t.view.error;
#undef OPT
}
static int save(const char *p, const char *text) {
  char tmp[520];
  int fd, ok;
  FILE *f;
  snprintf(tmp, sizeof(tmp), "%s.tmp", p);
  unlink(tmp);
  fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    return 0;
  if (!(f = fdopen(fd, "wb"))) {
    close(fd);
    unlink(tmp);
    return 0;
  }
  ok = fwrite(text, 1, strlen(text), f) == strlen(text);
  if (fflush(f) || fsync(fileno(f)))
    ok = 0;
  if (fclose(f))
    ok = 0;
  if (ok)
    ok = steam_file_replace(tmp, p);
  if (!ok)
    unlink(tmp);
  return ok;
}
unsigned int steam_prereq_prepare(const char *root, unsigned int arch,
                                  int probe, char out[512]) {
  char leaf[64], p[512], text[256];
  const struct steam_prereq_catalog *c = steam_prereq_catalog(arch);
  out[0] = 0;
  if (!c)
    return STEAM_PREREQ_PATH;
  if (probe) {
    static unsigned int sequence;
    char token[64];
    snprintf(leaf, sizeof(leaf), "probe-%s.exe", arch ? "x64" : "x86");
    if (!path(root, leaf, out, 0) || !regular(out))
      return STEAM_PREREQ_PROBE_MISSING;
    /* Non-secret correlation token, never an installed-state authority. */
    snprintf(token, sizeof(token), "%llu-%u", (unsigned long long)time(NULL),
             ++sequence);
    snprintf(leaf, sizeof(leaf), "probe-%s.request", arch ? "x64" : "x86");
    if (!path(root, leaf, p, 0) || !save(p, token))
      goto fs;
    snprintf(leaf, sizeof(leaf), "probe-%s.result", arch ? "x64" : "x86");
    if (!path(root, leaf, p, 0) || (unlink(p) && errno != ENOENT))
      goto fs;
  } else {
    if (!path(root, c->file, out, 0) || !steam_prereq_cached(root, arch))
      return STEAM_PREREQ_INTEGRITY;
    snprintf(text, sizeof(text),
             "/install /passive /norestart /log "
             "\"C:\\windows\\AutorunPrerequisites\\VC14-%s.log\"",
             arch ? "x64" : "x86");
    snprintf(leaf, sizeof(leaf), "VC14-%s.args.txt", arch ? "x64" : "x86");
    if (!path(root, leaf, p, 0) || !save(p, text))
      goto fs;
    /* A new attempt invalidates the old check; interrupted installs stay
     * Unknown. */
    snprintf(leaf, sizeof(leaf), "probe-%s.result", arch ? "x64" : "x86");
    if (!path(root, leaf, p, 0) || (unlink(p) && errno != ENOENT))
      goto fs;
  }
  snprintf(leaf, sizeof(leaf), "%s-%s.wine-nx.txt", probe ? "probe" : "VC14",
           arch ? "x64" : "x86");
  if (!path(root, leaf, p, 0) || !save(p, "d3d=wine\n"))
    goto fs;
  return 0;
fs:
  out[0] = 0;
  return STEAM_PREREQ_FILESYSTEM;
}
int steam_prereq_result(const char *root, unsigned int arch) {
  char leaf[64], p[512], request[64], got[64];
  FILE *f;
  unsigned int version, a, registry, files;
  char tail;
  if (arch > 1)
    return 0;
  snprintf(leaf, sizeof(leaf), "probe-%s.request", arch ? "x64" : "x86");
  if (!path(root, leaf, p, 0) || !regular(p) || !(f = fopen(p, "rb")))
    return 0;
  if (!fgets(request, sizeof(request), f)) {
    fclose(f);
    return 0;
  }
  fclose(f);
  snprintf(leaf, sizeof(leaf), "probe-%s.result", arch ? "x64" : "x86");
  if (!path(root, leaf, p, 0) || !regular(p) || !(f = fopen(p, "rb")))
    return 0;
  int ok = fscanf(f,
                  "autorun_prereq_probe=%u\narch=%u\nrequest=%63[^\n]"
                  "\nregistry=%u\nfiles=%u\n%c",
                  &version, &a, got, &registry, &files, &tail) == 5;
  fclose(f);
  if (!ok || version != 1 || a != arch || strcmp(request, got) ||
      registry > 1 || files > 1)
    return 0;
  return registry && files ? 1 : 2;
}
const char *steam_prereq_error_text(unsigned int e) {
  switch (e) {
  case STEAM_PREREQ_FILESYSTEM:
    return "Could not save prerequisite files. Check SD filesystem.";
  case STEAM_PREREQ_NETWORK:
    return "Microsoft HTTPS download failed. Check Wi-Fi and console clock.";
  case STEAM_PREREQ_INTEGRITY:
    return "Installer length or SHA-256 verification failed. Nothing was "
           "launched.";
  case STEAM_PREREQ_SPACE:
    return "Not enough free SD space for the installer.";
  case STEAM_PREREQ_CANCELLED:
    return "Cancelled. Partial prerequisite download removed.";
  case STEAM_PREREQ_PROBE_MISSING:
    return "Wine check tool missing. Extract the complete prerequisite update "
           "ZIP.";
  case STEAM_PREREQ_BUSY:
    return "Finish the current Steam download or removal first.";
  default:
    return "Unsafe or unavailable prerequisite path.";
  }
}
