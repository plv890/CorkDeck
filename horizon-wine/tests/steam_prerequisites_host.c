#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
/* No network in unit tests. Exercise cancellation through real curl options. */
static CURLcode failed_transfer(CURL *c) {
  (void)c;
  return CURLE_ABORTED_BY_CALLBACK;
}
#define curl_easy_perform failed_transfer
#include "../source/steam/steam_prerequisites.c"
#undef curl_easy_perform
static void write_file(const char *p, const void *data, size_t n) {
  FILE *f = fopen(p, "wb");
  assert(f);
  assert(fwrite(data, 1, n, f) == n);
  assert(!fclose(f));
}
static void put32(unsigned char *p, unsigned int n) {
  for (int i = 0; i < 4; ++i)
    p[i] = (unsigned char)(n >> (i * 8));
}
static void pe_fixture(const char *path, int arch, int delay, const char *dll) {
  unsigned char b[1024] = {0};
  unsigned int dir = arch ? 112 : 96, section = 64 + 24 + (arch ? 240 : 224);
  b[0] = 'M';
  b[1] = 'Z';
  put32(b + 60, 64);
  b[64] = 'P';
  b[65] = 'E';
  b[68] = arch ? 0x64 : 0x4c;
  b[69] = arch ? 0x86 : 1;
  b[70] = 1;
  b[84] = arch ? 240 : 224;
  b[88] = arch ? 0xb : 0xb;
  b[89] = arch ? 2 : 1;
  put32(b + 88 + dir - 4, 16);
  put32(b + 88 + dir + (delay ? 13 : 1) * 8, 0x1000);
  put32(b + 88 + dir + (delay ? 13 : 1) * 8 + 4, delay ? 64 : 40);
  put32(b + section + 8, 512);
  put32(b + section + 12, 0x1000);
  put32(b + section + 16, 512);
  put32(b + section + 20, 512);
  if (delay)
    put32(b + 512, 1);
  put32(b + 512 + (delay ? 4 : 12), 0x1080);
  assert(strlen(dll) < 128);
  strcpy((char *)b + 640, dll);
  write_file(path, b, sizeof(b));
}
static int yes(void *p) {
  (void)p;
  return 1;
}

/* A package as Microsoft's is laid out: a program, then a cabinet whose file is
 * a cabinet whose files are libraries named with their architecture. */
static size_t put_cab(unsigned char *out, const char *const names[], const unsigned char *const blobs[],
                      const size_t sizes[], unsigned int count, int packed) {
  unsigned char plain[4096], block[4096];
  size_t total = 0, p, files_at, data_at, block_size;
  unsigned int i;
  for (i = 0; i < count; ++i) {
    memcpy(plain + total, blobs[i], sizes[i]);
    total += sizes[i];
  }
  if (packed) {
    z_stream z = {0};
    assert(deflateInit2(&z, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    block[0] = 'C';
    block[1] = 'K';
    z.next_in = plain;
    z.avail_in = (uInt)total;
    z.next_out = block + 2;
    z.avail_out = sizeof(block) - 2;
    assert(deflate(&z, Z_FINISH) == Z_STREAM_END);
    block_size = 2 + z.total_out;
    deflateEnd(&z);
  } else {
    memcpy(block, plain, total);
    block_size = total;
  }
  memset(out, 0, 36 + 8);
  memcpy(out, "MSCF", 4);
  out[24] = 3;
  out[25] = 1;
  out[26] = 1;
  out[28] = (unsigned char)count;
  files_at = 36 + 8;
  for (p = files_at, i = 0, total = 0; i < count; ++i) {
    put32(out + p, (unsigned int)sizes[i]);
    put32(out + p + 4, (unsigned int)total);
    memset(out + p + 8, 0, 8);
    strcpy((char *)out + p + 16, names[i]);
    p += 16 + strlen(names[i]) + 1;
    total += sizes[i];
  }
  data_at = p;
  put32(out + 16, (unsigned int)files_at);
  put32(out + 36, (unsigned int)data_at);
  out[40] = 1;
  out[42] = (unsigned char)packed;
  put32(out + p, 0);
  out[p + 4] = (unsigned char)block_size;
  out[p + 5] = (unsigned char)(block_size >> 8);
  out[p + 6] = (unsigned char)total;
  out[p + 7] = (unsigned char)(total >> 8);
  memcpy(out + p + 8, block, block_size);
  p += 8 + block_size;
  put32(out + 8, (unsigned int)p);
  return p;
}
static void test_unpack(const char *dir) {
  static unsigned char inner[8192], outer[8192], package[16384];
  unsigned char one[600], two[40], other[20], got[600];
  const char *names[] = {"mfc140u.dll_x86", "vcruntime140.dll_x86", "mfc140u.dll_amd64", "notes.txt"};
  const unsigned char *blobs[] = {one, two, other, other};
  const size_t sizes[] = {sizeof(one), sizeof(two), sizeof(other), sizeof(other)};
  const char *outer_names[] = {"a0"};
  const unsigned char *outer_blobs[] = {inner};
  size_t inner_size, outer_size, outer_sizes[1];
  char file[512], system[512], path[640];
  unsigned int added = 9, kept = 9, i;
  FILE *f;
  for (i = 0; i < sizeof(one); ++i)
    one[i] = (unsigned char)(i * 7);
  memset(two, 'B', sizeof(two));
  memset(other, 'C', sizeof(other));
  inner_size = put_cab(inner, names, blobs, sizes, 4, 1);
  outer_sizes[0] = inner_size;
  outer_size = put_cab(outer, outer_names, outer_blobs, outer_sizes, 1, 1);
  memset(package, 'M', 1000); /* a program before it, full of the cabinet's first letter */
  memcpy(package + 1000, outer, outer_size);
  snprintf(file, sizeof(file), "%s/package.exe", dir);
  write_file(file, package, 1000 + outer_size);
  snprintf(system, sizeof(system), "%s/syswow64", dir);
  assert(!mkdir(system, 0700));
  /* What is there already stays as it is. */
  snprintf(path, sizeof(path), "%s/vcruntime140.dll", system);
  write_file(path, "wine's own", 10);
  assert(!unpack_package(file, system, 0, &added, &kept));
  assert(added == 1 && kept == 1);
  assert((f = fopen(path, "rb")) && fread(got, 1, sizeof(got), f) == 10 && !memcmp(got, "wine's own", 10));
  fclose(f);
  snprintf(path, sizeof(path), "%s/mfc140u.dll", system);
  assert((f = fopen(path, "rb")) && fread(got, 1, sizeof(got), f) == sizeof(one) && !memcmp(got, one, sizeof(one)));
  fclose(f);
  snprintf(path, sizeof(path), "%s/notes.txt", system);
  assert(access(path, F_OK));
  /* A second time adds nothing; the other architecture takes its own file. */
  assert(!unpack_package(file, system, 0, &added, &kept) && added == 0 && kept == 2);
  snprintf(system, sizeof(system), "%s/system32", dir);
  assert(!mkdir(system, 0700));
  assert(!unpack_package(file, system, 1, &added, &kept) && added == 1 && kept == 0);
  snprintf(path, sizeof(path), "%s/mfc140u.dll", system);
  assert((f = fopen(path, "rb")) && fread(got, 1, sizeof(got), f) == sizeof(other));
  fclose(f);
  /* A file with no cabinet of libraries in it is not a package. */
  write_file(file, package, 1000);
  assert(unpack_package(file, system, 0, &added, &kept) == STEAM_PREREQ_INTEGRITY);
  /* Microsoft's own package, when the test is given one. */
  if (getenv("CORKDECK_VC14_X86")) {
    snprintf(system, sizeof(system), "%s/real", dir);
    assert(!mkdir(system, 0700));
    assert(!unpack_package(getenv("CORKDECK_VC14_X86"), system, 0, &added, &kept));
    printf("Microsoft's package: %u libraries\n", added);
    assert(added >= 20 && !kept);
    snprintf(path, sizeof(path), "%s/mfc140u.dll", system);
    assert(!access(path, F_OK));
  }
}
static void test_missing(const char *dir) {
  struct steam_prereq_missing m;
  char root[512], p[640], exe[640];
  snprintf(root, sizeof(root), "%s/card", dir);
  assert(!mkdir(root, 0700));
  snprintf(p, sizeof(p), "%s/drive_c", root);
  assert(!mkdir(p, 0700));
  snprintf(p, sizeof(p), "%s/drive_c/windows", root);
  assert(!mkdir(p, 0700));
  snprintf(p, sizeof(p), "%s/drive_c/windows/syswow64", root);
  assert(!mkdir(p, 0700));
  snprintf(p, sizeof(p), "%s/drive_c/windows/system32", root);
  assert(!mkdir(p, 0700));
  snprintf(p, sizeof(p), "%s/Any Game", dir);
  assert(!mkdir(p, 0700));
  snprintf(exe, sizeof(exe), "%s/Any Game/Any.exe", dir);
  /* What a 32-bit program asks for is looked for beside it and in the 32-bit folder. */
  pe_fixture(exe, 0, 0, "MFC140U.dll");
  assert(steam_prereq_missing(root, exe, &m) && m.count == 1 && m.vc14 && m.architecture == 0);
  assert(!strcmp(m.names[0], "MFC140U.dll"));
  snprintf(p, sizeof(p), "%s/drive_c/windows/system32/MFC140U.dll", root);
  write_file(p, "x", 1);
  assert(steam_prereq_missing(root, exe, &m) && m.count == 1);
  snprintf(p, sizeof(p), "%s/drive_c/windows/syswow64/MFC140U.dll", root);
  write_file(p, "x", 1);
  assert(steam_prereq_missing(root, exe, &m) && !m.count);
  /* A 64-bit one in the 64-bit folder; a library of no package is named and not called Visual C++. */
  pe_fixture(exe, 1, 0, "physx_loader.dll");
  assert(steam_prereq_missing(root, exe, &m) && m.count == 1 && !m.vc14 && m.architecture == 1);
  snprintf(p, sizeof(p), "%s/Any Game/physx_loader.dll", dir);
  write_file(p, "x", 1);
  assert(steam_prereq_missing(root, exe, &m) && !m.count);
  /* The names Windows resolves by itself are not files to look for. */
  pe_fixture(exe, 1, 0, "api-ms-win-crt-runtime-l1-1-0.dll");
  assert(steam_prereq_missing(root, exe, &m) && !m.count);
  /* One automatic attempt for each architecture is remembered. */
  assert(!steam_prereq_tried(root, 0, 0) && steam_prereq_tried(root, 0, 1) && steam_prereq_tried(root, 0, 0));
  assert(!steam_prereq_tried(root, 1, 0));
}
int main(void) {
  char root[] = "/tmp/autorun-prerequisites-XXXXXX", game[512], p[512],
       exe[512], request[64], result[256], out[512];
  struct steam_prereq_requirements v;
  assert(mkdtemp(root));
  {
    char extra[] = "/tmp/corkdeck-libraries-XXXXXX", command[128];
    assert(mkdtemp(extra));
    test_unpack(extra);
    test_missing(extra);
    snprintf(command, sizeof(command), "rm -rf %s", extra);
    assert(!system(command));
  }
  snprintf(p, sizeof(p), "%s/drive_c", root);
  assert(!mkdir(p, 0700));
  snprintf(p, sizeof(p), "%s/drive_c/windows", root);
  assert(!mkdir(p, 0700));
  snprintf(game, sizeof(game), "%s/Game", root);
  assert(!mkdir(game, 0700));
  snprintf(exe, sizeof(exe), "%s/Game/Game.exe", root);
  pe_fixture(exe, 1, 0, "VCRUNTIME140.dll");
  assert(!steam_prereq_inspect(game, exe, NULL, &v) && v.architecture == 1 &&
         (v.needs & STEAM_PREREQ_VC14));
  pe_fixture(exe, 0, 1, "msvcp140_1.dll");
  assert(!steam_prereq_inspect(game, exe, NULL, &v) && v.architecture == 0 &&
         (v.needs & STEAM_PREREQ_VC14));
  snprintf(p, sizeof(p), "%s/Game/Neighbor.dll", root);
  pe_fixture(p, 0, 0, "d3dx9_43.dll");
  assert(!steam_prereq_inspect(game, exe, NULL, &v) &&
         (v.needs & STEAM_PREREQ_DX_HELPERS));
  assert(!unlink(p));
  pe_fixture(exe, 0, 0, "StEaM_ApI.dll");
  assert(!steam_prereq_inspect(game, exe, NULL, &v) &&
         v.needs == STEAM_PREREQ_STEAMWORKS);
  pe_fixture(exe, 1, 1, "steam_api64.dll");
  assert(!steam_prereq_inspect(game, exe, NULL, &v) &&
         v.architecture == 1 && v.needs == STEAM_PREREQ_STEAMWORKS);
  pe_fixture(exe, 0, 0, "steam_api.dll.backup");
  assert(!steam_prereq_inspect(game, exe, NULL, &v) && !v.needs);
  snprintf(p, sizeof(p), "%s/Game/scripts", root);
  assert(!mkdir(p, 0700));
  snprintf(p, sizeof(p), "%s/Game/scripts/setup.vdf", root);
  const char *s = "\"Run Process\" { \"VC_redist 2022\" \"dotnet\" }";
  write_file(p, s, strlen(s));
  assert(!steam_prereq_inspect(game, exe, "scripts\\setup.vdf", &v) &&
         v.script && (v.needs & STEAM_PREREQ_DOTNET));
  assert(steam_prereq_inspect(game, exe, "../outside.vdf", &v) ==
         STEAM_PREREQ_PATH);
  snprintf(p, sizeof(p), "%s/Game/link", root);
  assert(!symlink("/tmp", p));
  assert(steam_prereq_inspect(game, exe, "link/missing.vdf", &v) ==
         STEAM_PREREQ_PATH);
  assert(!unlink(p));
  assert(steam_prereq_inspect(game, "/tmp/other.exe", NULL, &v) ==
         STEAM_PREREQ_PATH);
  write_file(exe, "MZ", 2);
  assert(!steam_prereq_inspect(game, exe, NULL, &v) && v.architecture == 2 &&
         !v.needs);
  assert(!steam_prereq_catalog(2));
  assert(!steam_prereq_cached(root, 0));
  assert(steam_prereq_prepare(root, 0, 0, out) == STEAM_PREREQ_INTEGRITY);
  struct steam_prereq_callbacks cb = {.cancel = yes};
  assert(steam_prereq_fetch(root, 0, &cb) == STEAM_PREREQ_CANCELLED);
  assert(path(root, "VC14-x86.exe.part", p, 0) && access(p, F_OK));
  assert(!steam_prereq_cached(root, 0));
  assert(steam_prereq_prepare(root, 0, 1, out) == STEAM_PREREQ_PROBE_MISSING);
  assert(path(root, "probe-x86.exe", p, 0));
  write_file(p, "fixture", 7);
  assert(!steam_prereq_prepare(root, 0, 1, out));
  assert(!steam_prereq_result(root, 0));
  assert(path(root, "probe-x86.request", p, 0));
  FILE *f = fopen(p, "rb");
  assert(f && fgets(request, sizeof(request), f));
  fclose(f);
  assert(path(root, "probe-x86.result", p, 0));
  snprintf(result, sizeof(result),
           "autorun_prereq_probe=1\narch=0\nrequest=%s\nregistry=1\nfiles=1\n",
           request);
  write_file(p, result, strlen(result));
  assert(steam_prereq_result(root, 0) == 1);
  snprintf(result, sizeof(result),
           "autorun_prereq_probe=1\narch=1\nrequest=%s\nregistry=1\nfiles=1\n",
           request);
  write_file(p, result, strlen(result));
  assert(!steam_prereq_result(root, 0));
  snprintf(
      result, sizeof(result),
      "autorun_prereq_probe=1\narch=0\nrequest=stale\nregistry=1\nfiles=1\n");
  write_file(p, result, strlen(result));
  assert(!steam_prereq_result(root, 0));
  snprintf(result, sizeof(result),
           "autorun_prereq_probe=1\narch=0\nrequest=%s\nregistry=1\nfiles=0\n",
           request);
  write_file(p, result, strlen(result));
  assert(steam_prereq_result(root, 0) == 2);
  assert(!steam_prereq_prepare(root, 0, 1, out) &&
         !steam_prereq_result(root, 0));
  /* Disk readback, not response hash alone. */
  assert(path(root, "hash-fixture", p, 0));
  write_file(p, "abc", 3);
  struct steam_prereq_catalog tiny = {
      .size = 3,
      .sha256 =
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"};
  assert(hash(p, &tiny));
  write_file(p, "abd", 3);
  assert(!hash(p, &tiny));
  struct transfer t = {0};
  t.file = tmpfile();
  assert(t.file);
  t.view.total = 2;
  assert(!receive("abc", 1, 3, &t) && t.view.error == STEAM_PREREQ_INTEGRITY);
  fclose(t.file);
  printf("Prerequisites: PE imports/delay imports, path containment, script "
         "hints, cancel cleanup, SHA readback and fresh probe correlation "
         "passed. Fixture: %s\n",
         root);
  const char *leaves[] = {
      "Game/Game.exe",
      "Game/scripts/setup.vdf",
      "drive_c/windows/AutorunPrerequisites/probe-x86.exe",
      "drive_c/windows/AutorunPrerequisites/probe-x86.request",
      "drive_c/windows/AutorunPrerequisites/probe-x86.request.previous",
      "drive_c/windows/AutorunPrerequisites/probe-x86.wine-nx.txt",
      "drive_c/windows/AutorunPrerequisites/probe-x86.wine-nx.txt.previous",
      "drive_c/windows/AutorunPrerequisites/hash-fixture"};
  for (size_t i = 0; i < sizeof(leaves) / sizeof(*leaves); ++i) {
    snprintf(p, sizeof(p), "%s/%s", root, leaves[i]);
    assert(!unlink(p) || errno == ENOENT);
  }
  const char *dirs[] = {"Game/scripts", "Game",
                        "drive_c/windows/AutorunPrerequisites",
                        "drive_c/windows", "drive_c"};
  for (size_t i = 0; i < sizeof(dirs) / sizeof(*dirs); ++i) {
    snprintf(p, sizeof(p), "%s/%s", root, dirs[i]);
    assert(!rmdir(p));
  }
  assert(!rmdir(root));
  return 0;
}
