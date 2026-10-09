#ifndef AUTORUN_STEAM_DIAGNOSTICS_H
#define AUTORUN_STEAM_DIAGNOSTICS_H
#include "steam_install.h"
#include "steam_provider.h"
#define STEAM_DIAGNOSTIC_BUILD 2026100816u
#define STEAM_DIAGNOSTIC_BUILD_LABEL "2026.10.08-19"
struct steam_diagnostic_view {
  struct steam_status_view auth;
  struct steam_library_view library;
  struct steam_session_view session;
  struct steam_download_view download;
  unsigned int concurrency;
};
/* History: 0 missing, 1 legacy v1, 2 current v2, 3 unknown, 4 unreadable.
 * Export stage: 1 path, 2 open, 3 write, 4 flush, 5 sync, 6 close, 7 readback.
 * No raw history content or sensitive snapshot fields are serialized. */
struct steam_diagnostic_result {
  unsigned int error, stage, history, history_error;
};
/* UI-owned separate report; never modifies the installer's transfer history.
 * A successful return requires flush, sync, close and exact bounded readback.
 */
unsigned int steam_diagnostic_export(const char *,
                                     const struct steam_diagnostic_view *,
                                     struct steam_diagnostic_result *);
/* Installer worker owns this append-only, bounded numeric log. Returns errno
 * on failure; idle snapshots cannot erase a previous transfer. */
unsigned int steam_download_diagnostic(const char *,
                                       const struct steam_download_view *);
/* One line for every download that failed, and for what was then chosen: when,
 * which game, the error and where it happened, how much had been verified.
 * Kept apart from the numeric history so that it can be read by a person. */
void steam_download_failure_log(const char *root,
                                const struct steam_download_view *,
                                const char *outcome);
#endif
