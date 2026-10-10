#include "steam_prerequisites_ui.h"
#include "../launcher.h"
#include "../launcher_ui.h"
#include "steam_install.h"
#include "steam_prerequisites.h"
#include "steam_storage.h"
#include "steam_ui.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>
struct task {
  SDL_mutex *mutex;
  SDL_atomic_t stop, done;
  struct steam_prereq_progress view;
  const char *root;
  unsigned int arch;
};
static int stop(void *opaque) {
  return SDL_AtomicGet(&((struct task *)opaque)->stop);
}
static void tick(void *opaque, const struct steam_prereq_progress *view) {
  struct task *t = opaque;
  SDL_LockMutex(t->mutex);
  t->view = *view;
  SDL_UnlockMutex(t->mutex);
}
static int worker(void *opaque) {
  struct task *t = opaque;
  struct steam_prereq_callbacks cb = {t, stop, tick};
  steam_prereq_fetch(t->root, t->arch, &cb);
  SDL_AtomicSet(&t->done, 1);
  return 0;
}
static unsigned int fetch(struct ui *ui, const char *root, unsigned int arch) {
  struct task t = {.root = root, .arch = arch};
  struct steam_prereq_progress v = {0};
  SDL_Thread *thread;
  struct ui_input in;
  char text[192];
  t.mutex = SDL_CreateMutex();
  if (!t.mutex)
    return 5;
  thread = SDL_CreateThread(worker, "prerequisite-download", &t);
  if (!thread) {
    SDL_DestroyMutex(t.mutex);
    return 5;
  }
  ui_start_screen(ui);
  while (!SDL_AtomicGet(&t.done)) {
    if (!ui_begin_frame(ui))
      SDL_AtomicSet(&t.stop, 1);
    while (ui_poll(ui, &in))
      if (in.button == UI_B)
        SDL_AtomicSet(&t.stop, 1);
    SDL_LockMutex(t.mutex);
    v = t.view;
    SDL_UnlockMutex(t.mutex);
    ui_background(ui);
    ui_header_back(ui, "Game dependencies", steam_prereq_catalog(arch)->name);
    ui_text(ui, ui->normal, 84, 210,
            arch == STEAM_PREREQ_STEAM_CLIENT
                ? "Downloading the Steam client library from Valve"
                : "Downloading verified Microsoft installer",
            ui->value);
    snprintf(text, sizeof(text), "%.2f / %.2f MiB%s",
             (double)v.downloaded / 1048576, (double)v.total / 1048576,
             SDL_AtomicGet(&t.stop) ? " / Cancelling..." : "");
    ui_text(ui, ui->normal, 84, 276, text, ui->value);
    ui_rounded(ui, 84, 342, 1100, 10, 5, ui->card);
    if (v.total)
      ui_rounded(ui, 84, 342, (int)(1100 * v.downloaded / v.total), 10, 5,
                 ui->value);
    ui_text(
        ui, ui->small, 84, 398,
        arch == STEAM_PREREQ_STEAM_CLIENT
            ? "Older Steam games need it. It is checked by SHA-256 before use."
            : "The installer will run through CorkDeck after SHA-256 verification.",
        ui->dim);
    const struct ui_hint hints[] = {{UI_B, "Cancel download"}};
    ui_footer(ui, hints, 1);
    ui_present(ui);
    SDL_Delay(16);
  }
  SDL_WaitThread(thread, NULL);
  SDL_LockMutex(t.mutex);
  v = t.view;
  SDL_UnlockMutex(t.mutex);
  SDL_DestroyMutex(t.mutex);
  snprintf(text, sizeof(text),
           "[PREREQUISITES] download arch=%u error=%u http=%u transport=%u "
           "bytes=%llu",
           arch, v.error, v.http, v.transport,
           (unsigned long long)v.downloaded);
  wine_nx_runtime_trace(text);
  ui_start_screen(ui);
  return v.error;
}
unsigned int steam_prerequisites_fetch_ui(struct ui *ui, const char *root, unsigned int arch) {
  return steam_prereq_cached(root, arch) ? 0 : fetch(ui, root, arch);
}
int steam_prerequisites_ui(struct ui *ui, const char *root, uint32_t app,
                           const char *script,
                           const struct steam_launcher_adapter *adapter) {
  struct steam_prereq_requirements v;
  struct ui_row rows[8];
  struct ui_list list = {0};
  char game[512], exe[512], target[512], context[256], message[512];
  unsigned int error;
  int cached[2];
  if (!steam_storage_directory(root, app, game)) {
    ui_message(ui, "Game dependencies", "Install the game first.");
    return 0;
  }
  exe[0]=0;
  steam_install_executable(root, app, exe);
  error = steam_prereq_inspect(game, exe, script, &v);
  if (error) {
    ui_message(ui, "Game dependencies", steam_prereq_error_text(error));
    return 0;
  }
  cached[0] = steam_prereq_cached(root, 0);
  cached[1] = steam_prereq_cached(root, 1);
  for (;;) {
    int i;
    memset(rows, 0, sizeof(rows));
    strcpy(rows[0].label, "Detected requirements");
    rows[0].kind = UI_ROW_INFO;
    snprintf(rows[0].value, sizeof(rows[0].value), "%s%s",
             v.needs & STEAM_PREREQ_VC14 ? "Visual C++ v14"
             : v.needs                   ? "Other prerequisites"
                                         : "Unknown",
             v.limited ? " / partial scan" : "");
    rows[0].help = "PE imports and script hints are advisory. No installscript "
                   "commands are executed.";
    strcpy(rows[1].label, "Other requirements");
    rows[1].kind = UI_ROW_INFO;
    snprintf(rows[1].value, sizeof(rows[1].value), "%s%s%s%s%s",
             v.needs & STEAM_PREREQ_STEAMWORKS ? "Steamworks / " : "",
             v.needs & STEAM_PREREQ_LEGACY_VC ? "Older VC++ / " : "",
             v.needs & STEAM_PREREQ_DX_HELPERS ? "DirectX helpers / " : "",
             v.needs & STEAM_PREREQ_DOTNET ? ".NET / " : "",
             v.needs & STEAM_PREREQ_CUSTOM ? "Custom installer" : "");
    if (!rows[1].value[0])
      strcpy(rows[1].value, "Not detected");
    rows[1].help = v.needs & STEAM_PREREQ_STEAMWORKS
        ? "This game imports Steamworks. CorkDeck sign-in and downloads do not "
          "provide its Steam client interfaces. Launch support is unverified."
        : "These components are not automatically installed in this "
          "first version. Graphics runtime stays unchanged.";
    for (i = 0; i < 2; ++i) {
      int result = steam_prereq_result(root, (unsigned)i);
      snprintf(rows[2 + i].label, sizeof(rows[2 + i].label),
               "Install Visual C++ v14 %s", i ? "x64" : "x86");
      snprintf(rows[2 + i].value, sizeof(rows[2 + i].value), "%s%s",
               cached[i] ? "Verified installer ready"
                         : "Download from Microsoft",
               v.architecture == (unsigned)i ? " / selected EXE" : "");
      rows[2 + i].help = "Version 14.44.35211. Accept Microsoft license; run "
                         "the original installer in existing Wine. "
                         "Installation may be unsupported on Horizon.";
      rows[2 + i].download = !cached[i];
      snprintf(rows[4 + i].label, sizeof(rows[4 + i].label),
               "Check Wine environment %s", i ? "x64" : "x86");
      snprintf(rows[4 + i].value, sizeof(rows[4 + i].value), "%s",
               result == 1   ? "Last check: registered + DLL files"
               : result == 2 ? "Last check: missing / incomplete"
                             : "Not checked / check pending");
      rows[4 + i].help =
          "Runs a read-only Windows probe in the same shared Wine environment. "
          "A fresh check is required after an installer or registry reset.";
      rows[4 + i].value_tone = result == 1   ? UI_VALUE_SUCCESS
                               : result == 2 ? UI_VALUE_DANGER
                                             : UI_VALUE_NORMAL;
    }
    snprintf(context, sizeof(context),
             "AppID %u / %u PE files checked / shared Wine environment", app,
             v.files);
    if (ui_list_run(ui, &list, "Game dependencies", context, rows, 6, 0) ==
        UI_ACTION_BACK)
      return 0;
    if (list.selection < 2)
      continue;
    unsigned int arch = (unsigned int)((list.selection - 2) % 2);
    int probe = list.selection >= 4;
    if (!adapter->launch_prerequisite) {
      ui_message(ui, "Game dependencies", "Launcher adapter unavailable.");
      continue;
    }
    if (!probe) {
      if (!ui_confirm(ui, "Install Visual C++",
                      "Download and run Microsoft's original Visual C++ "
                      "installer in the existing shared Wine environment? "
                      "Microsoft license terms apply. Close games first. Other "
                      "installed games may also use this component.",
                      "Install"))
        continue;
      error = fetch(ui, root, arch);
      cached[arch] = !error;
      if (error) {
        snprintf(message, sizeof(message), "Error %u: %s", error,
                 steam_prereq_error_text(error));
        ui_message(ui, "Game dependencies", message);
        continue;
      }
    }
    error = steam_prereq_prepare(root, arch, probe, target);
    if (error) {
      ui_message(ui, "Game dependencies", steam_prereq_error_text(error));
      continue;
    }
    if (!adapter->launch_prerequisite(adapter->context, target)) {
      ui_message(
          ui, "Game dependencies",
          "Could not queue the prerequisite executable. Files preserved.");
      continue;
    }
    return 1;
  }
}
