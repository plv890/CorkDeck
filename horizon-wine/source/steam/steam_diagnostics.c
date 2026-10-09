#define _POSIX_C_SOURCE 200809L
#include "steam_diagnostics.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static int regular(const char *path, struct stat *st) {
  if (lstat(path, st))
    return errno == ENOENT;
  if (!S_ISREG(st->st_mode) || S_ISLNK(st->st_mode)) {
    errno = EINVAL;
    return 0;
  }
  return 1;
}
static unsigned int io_error(void) { return errno ? (unsigned int)errno : EIO; }
static void history_format(const char *folder,
                           struct steam_diagnostic_result *r) {
  char path[832], prefix[19] = {0};
  struct stat st = {0};
  FILE *f;
  snprintf(path, sizeof(path), "%s/steam-download.log", folder);
  if (!regular(path, &st)) {
    r->history = 4;
    r->history_error = io_error();
    return;
  }
  f = fopen(path, "rb");
  if (!f) {
    if (errno != ENOENT) {
      r->history = 4;
      r->history_error = io_error();
    }
    return;
  }
  fread(prefix, 1, sizeof(prefix) - 1, f);
  r->history = !strncmp(prefix, "steam_library_v1 ", 17) ||
                       !strncmp(prefix, "steam_download_v1 ", 18)
                   ? 1
               : !strncmp(prefix, "steam_download_v2 ", 18) ? 2
                                                            : 3;
  if (ferror(f)) {
    r->history = 4;
    r->history_error = io_error();
  }
  if (fclose(f)) {
    r->history = 4;
    r->history_error = io_error();
  }
}
unsigned int steam_diagnostic_export(const char *root,
                                     const struct steam_diagnostic_view *v,
                                     struct steam_diagnostic_result *r) {
  char folder[768], path[832], text[2048], readback[2048];
  const struct steam_download_view *d = &v->download;
  struct stat st = {0};
  FILE *f;
  int n;
  size_t size;
  *r = (struct steam_diagnostic_result){.stage = 1};
  n = snprintf(folder, sizeof(folder), "%s/logs", root);
  if (n < 0 || (size_t)n >= sizeof(folder)) {
    r->error = ENAMETOOLONG;
    return r->error;
  }
  if (lstat(folder, &st)) {
    if (errno != ENOENT || mkdir(folder, 0777)) {
      r->error = io_error();
      return r->error;
    }
  } else if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
    r->error = EINVAL;
    return r->error;
  }
  history_format(folder, r);
  snprintf(path, sizeof(path), "%s/steam-report.log", folder);
  if (!regular(path, &st)) {
    r->error = io_error();
    return r->error;
  }
  n = snprintf(
      text, sizeof(text),
      "steam_report_v1 build=%u history=%u history_error=%u "
      "auth_state=%u auth_error=%u auth_stage=%u cm_type=%u transport=%u "
      "library_state=%u library_error=%u library_count=%llu family_error=%u "
      "vault_present=%u vault_saving=%u vault_error=%u concurrency=%u\n"
      "download_snapshot available=%u appid=%u state=%u error=%u http=%u "
      "transport=%u stage=%u verified=%llu total=%llu network=%llu free=%llu "
      "elapsed_ms=%llu network_ms=%llu first_byte_ms=%llu body_ms=%llu "
      "decode_ms=%llu write_ms=%llu sync_ms=%llu verify_ms=%llu resume_ms=%llu "
      "writer_wait_ms=%llu requests=%u connections=%u parallelism=%u "
      "peak_requests=%u diagnostic_error=%u cleanup_errno=%u batch_files=%u batch_peak_files=%u integrity_detail=%u chunk_format=%u integrity_retries=%u depot_id=%u\n",
      STEAM_DIAGNOSTIC_BUILD, r->history, r->history_error,
      (unsigned int)v->auth.state, v->auth.error, v->auth.failed_stage,
      v->auth.cm_type, v->auth.transport_error, (unsigned int)v->library.state,
      v->library.error, (unsigned long long)v->library.count,
      v->library.family_error, (unsigned int)!!v->session.present,
      (unsigned int)!!v->session.saving, v->session.error, v->concurrency,
      (unsigned int)(d->app_id && d->state != STEAM_DOWNLOAD_IDLE), d->app_id,
      (unsigned int)d->state, d->error, d->http_status, d->transport,
      d->failed_stage, (unsigned long long)d->downloaded,
      (unsigned long long)d->total, (unsigned long long)d->network_bytes,
      (unsigned long long)d->free_bytes, (unsigned long long)d->elapsed_ms,
      (unsigned long long)d->network_ms, (unsigned long long)d->first_byte_ms,
      (unsigned long long)d->body_ms, (unsigned long long)d->decode_ms,
      (unsigned long long)d->write_ms, (unsigned long long)d->sync_ms,
      (unsigned long long)d->verify_ms, (unsigned long long)d->resume_ms,
      (unsigned long long)d->writer_wait_ms, d->requests, d->connections,
      d->parallelism, d->peak_requests, d->diagnostic_error, d->cleanup_errno, d->batch_files, d->batch_peak_files, d->integrity_detail,d->chunk_format,d->integrity_retries,d->depot_id);
  if (n < 0 || (size_t)n >= sizeof(text)) {
    r->error = EOVERFLOW;
    return r->error;
  }
  size = (size_t)n;
  r->stage = 2;
  f = fopen(path, "wb");
  if (!f) {
    r->error = io_error();
    return r->error;
  }
  r->stage = 3;
  if (fwrite(text, 1, size, f) != size)
    r->error = io_error();
  if (!r->error) {
    r->stage = 4;
    if (fflush(f))
      r->error = io_error();
  }
  if (!r->error) {
    r->stage = 5;
    if (fsync(fileno(f)))
      r->error = io_error();
  }
  if (!r->error)
    r->stage = 6;
  if (fclose(f) && !r->error)
    r->error = io_error();
  if (r->error)
    return r->error;
  r->stage = 7;
  f = fopen(path, "rb");
  if (!f) {
    r->error = io_error();
    return r->error;
  }
  if (fread(readback, 1, size + 1, f) != size || ferror(f) ||
      memcmp(text, readback, size))
    r->error = EIO;
  if (fclose(f) && !r->error)
    r->error = io_error();
  if (!r->error)
    r->stage = 0;
  return r->error;
}
unsigned int steam_download_diagnostic(const char *root,
                                       const struct steam_download_view *v) {
  char folder[768], path[800], previous[832], header[64];
  struct stat st = {0};
  FILE *f;
  const char *mode = "ab";
  int n, written, closed;
  if (!v->app_id || v->state == STEAM_DOWNLOAD_IDLE)
    return 0;
  n = snprintf(folder, sizeof(folder), "%s/logs", root);
  if (n < 0 || (size_t)n >= sizeof(folder))
    return ENAMETOOLONG;
  if (lstat(folder, &st)) {
    if (errno != ENOENT || mkdir(folder, 0777))
      return errno ? errno : EIO;
  } else if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
    return EINVAL;
  n = snprintf(path, sizeof(path), "%s/steam-download.log", folder);
  if (n < 0 || (size_t)n >= sizeof(path))
    return ENAMETOOLONG;
  memset(&st, 0, sizeof(st));
  if (!regular(path, &st))
    return errno;
  /* Migrate old UI snapshots, including the idle-zero file. Real v2
   * transfers append independently of Horizon's rename-overwrite behavior. */
  if (st.st_size && (f = fopen(path, "rb"))) {
    if (fgets(header, sizeof(header), f) &&
        !strncmp(header, "steam_library_v1 ", 17))
      mode = "wb";
    fclose(f);
  }
  if (st.st_size >= 512 * 1024) {
    snprintf(previous, sizeof(previous), "%s.previous", path);
    if (!regular(previous, &st))
      return errno;
    if (unlink(previous) && errno != ENOENT)
      return errno;
    if (rename(path, previous))
      return errno;
    mode = "ab";
  }
  f = fopen(path, mode);
  if (!f)
    return errno ? errno : EIO;
  /* Numeric allowlist only. No account IDs, file paths, URLs, server bodies
   * or credentials. One record per interval plus initial/terminal states. */
  written = fprintf(
      f,
      "steam_download_v2 appid=%u state=%u error=%u http=%u transport=%u "
      "stage=%u "
      "verified=%llu total=%llu network=%llu free=%llu elapsed_ms=%llu "
      "network_ms=%llu "
      "requests=%u connections=%u parallelism=%u peak_requests=%u "
      "decode_ms=%llu write_ms=%llu "
      "sync_ms=%llu first_byte_ms=%llu body_ms=%llu verify_ms=%llu "
      "resume_ms=%llu writer_wait_ms=%llu cleanup_errno=%u batch_files=%u batch_peak_files=%u integrity_detail=%u chunk_format=%u integrity_retries=%u depot_id=%u\n",
      v->app_id, (unsigned int)v->state, v->error, v->http_status, v->transport,
      v->failed_stage, (unsigned long long)v->downloaded,
      (unsigned long long)v->total, (unsigned long long)v->network_bytes,
      (unsigned long long)v->free_bytes, (unsigned long long)v->elapsed_ms,
      (unsigned long long)v->network_ms, v->requests, v->connections,
      v->parallelism, v->peak_requests, (unsigned long long)v->decode_ms,
      (unsigned long long)v->write_ms, (unsigned long long)v->sync_ms,
      (unsigned long long)v->first_byte_ms, (unsigned long long)v->body_ms,
      (unsigned long long)v->verify_ms, (unsigned long long)v->resume_ms,
      (unsigned long long)v->writer_wait_ms, v->cleanup_errno, v->batch_files, v->batch_peak_files, v->integrity_detail,v->chunk_format,v->integrity_retries,v->depot_id);
  closed = fclose(f);
  if (written < 0 || closed)
    return errno ? errno : EIO;
  return 0;
}
void steam_download_failure_log(const char *root,
                                const struct steam_download_view *v,
                                const char *outcome) {
  char folder[768], path[832], previous[864], when[32] = "";
  struct stat st = {0};
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  FILE *f;
  if (!v->app_id ||
      snprintf(folder, sizeof(folder), "%s/logs", root) >= (int)sizeof(folder))
    return;
  if (lstat(folder, &st) && (errno != ENOENT || mkdir(folder, 0777)))
    return;
  snprintf(path, sizeof(path), "%s/steam-download-failures.log", folder);
  if (!lstat(path, &st) && st.st_size >= 256 * 1024) {
    snprintf(previous, sizeof(previous), "%s.previous", path);
    unlink(previous);
    rename(path, previous);
  }
  if (tm)
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", tm);
  f = fopen(path, "ab");
  if (!f)
    return;
  fprintf(f,
          "%s build=%s appid=%u language=%s error=%u stage=%u http=%u "
          "transport=%u integrity=%u depot=%u file=\"%s\" verified=%llu "
          "total=%llu network=%llu free=%llu attempts=%u: %s\n",
          when, STEAM_DIAGNOSTIC_BUILD_LABEL, v->app_id,
          v->language[0] ? v->language : "-", v->error, v->failed_stage,
          v->http_status, v->transport, v->integrity_detail, v->depot_id,
          v->file, (unsigned long long)v->downloaded,
          (unsigned long long)v->total, (unsigned long long)v->network_bytes,
          v->free_known ? (unsigned long long)v->free_bytes : 0ull, v->attempts,
          outcome);
  fclose(f);
}
