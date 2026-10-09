#define _POSIX_C_SOURCE 200809L
#include "../source/steam/steam_diagnostics.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void) {
  char root[] = "/tmp/steam-diagnostic-XXXXXX", folder[256], path[300],
       old[340], data[4096];
  FILE *f;
  size_t n;
  struct stat st;
  struct steam_download_view v = {.app_id = 123,
                                  .state = STEAM_DOWNLOAD_FILES,
                                  .network_bytes = 100,
                                  .requests = 2,
                                  .parallelism = 2, .integrity_detail=6,.chunk_format=2,.integrity_retries=2,.depot_id=124};
  assert(mkdtemp(root));
  snprintf(folder, sizeof(folder), "%s/logs", root);
  snprintf(path, sizeof(path), "%s/steam-download.log", folder);
  snprintf(old, sizeof(old), "%s.previous", path);
  assert(
      !steam_download_diagnostic(root, &v)); /* logs directory did not exist */
  {
    char report[300], saved[4096], expected[32];
    struct steam_diagnostic_result result;
    struct steam_diagnostic_view snapshot = {
        .auth = {.state = STEAM_ONLINE, .steam_id = 76561198000000000},
        .library = {.state = STEAM_LIBRARY_READY, .count = 10},
        .download = v,
        .concurrency = 2};
    snprintf(report, sizeof(report), "%s/steam-report.log", folder);
    strcpy(snapshot.download.file, "DO_NOT_LOG_PASSWORD_TOKEN_COOKIE_GUARD");
    strcpy(snapshot.download.executable, "DO_NOT_LOG_ACCOUNT_PATH");
    assert(!steam_diagnostic_export(root, &snapshot, &result) &&
           !result.stage && result.history == 2);
    f = fopen(report, "rb");
    assert(f);
    n = fread(saved, 1, sizeof(saved) - 1, f);
    assert(!fclose(f));
    saved[n] = 0;
    snprintf(expected, sizeof(expected), "build=%u ", STEAM_DIAGNOSTIC_BUILD);
    assert(strstr(saved, expected) &&
           strstr(saved, "available=1 appid=123") && strstr(saved,"integrity_detail=6 chunk_format=2 integrity_retries=2 depot_id=124"));
    assert(!strstr(saved, "DO_NOT_LOG") && !strstr(saved, "76561198000000000"));
    assert(!stat(path, &st));
    {
      off_t original = st.st_size;
      memset(&snapshot.download, 0, sizeof(snapshot.download));
      assert(!steam_diagnostic_export(root, &snapshot, &result));
      assert(!stat(path, &st) && st.st_size == original);
    }
    f = fopen(path, "wb");
    assert(f);
    fputs("steam_library_v1 state=0\n", f);
    assert(!fclose(f));
    assert(!steam_diagnostic_export(root, &snapshot, &result) &&
           result.history == 1);
    assert(!unlink(report));
    assert(!symlink(path, report));
    assert(steam_diagnostic_export(root, &snapshot, &result) == EINVAL &&
           result.stage == 1);
    assert(!unlink(report));
  }
  f = fopen(path, "wb");
  assert(f);
  fputs("steam_library_v1 state=0 count=0\nsteam_download_v1 appid=0\n", f);
  assert(!fclose(f));
  assert(!steam_download_diagnostic(root, &v));
  f = fopen(path, "rb");
  assert(f);
  n = fread(data, 1, sizeof(data) - 1, f);
  assert(!fclose(f));
  data[n] = 0;
  assert(!strncmp(data, "steam_download_v2 ", 18) && !strstr(data, "appid=0") &&
         strstr(data, "network=100") && strstr(data,"integrity_detail=6 chunk_format=2 integrity_retries=2 depot_id=124"));
  v.state = STEAM_DOWNLOAD_COMPLETE;
  assert(!steam_download_diagnostic(root, &v));
  assert(!stat(path, &st) && st.st_size > (off_t)n);
  f = fopen(path, "ab");
  assert(f);
  memset(data, ' ', sizeof(data));
  while (ftell(f) < 512 * 1024)
    assert(fwrite(data, 1, sizeof(data), f) == sizeof(data));
  assert(!fclose(f));
  assert(!steam_download_diagnostic(root, &v));
  assert(!stat(old, &st) && st.st_size >= 512 * 1024);
  assert(!stat(path, &st) && st.st_size < 4096);
  assert(!unlink(path));
  assert(!symlink(old, path));
  assert(steam_download_diagnostic(root, &v) == EINVAL);
  assert(!unlink(path));
  assert(!unlink(old) && !rmdir(folder));
  assert(!mkdir(folder, 0700));
  assert(!mkdir(path, 0700));
  assert(steam_download_diagnostic(root, &v) == EINVAL);
  assert(!rmdir(path) && !rmdir(folder) && !rmdir(root));
  puts("Download logger migration/rotation and explicit synced, reread export: "
       "legacy history detection, no secrets/IDs, idle history preserved and "
       "unsafe-path errors passed");
  return 0;
}
