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
int main(void) {
  char root[] = "/tmp/autorun-prerequisites-XXXXXX", game[512], p[512],
       exe[512], request[64], result[256], out[512];
  struct steam_prereq_requirements v;
  assert(mkdtemp(root));
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
