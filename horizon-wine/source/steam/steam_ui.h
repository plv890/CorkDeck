#ifndef AUTORUN_STEAM_UI_H
#define AUTORUN_STEAM_UI_H
#include <stdint.h>
#include <stddef.h>
struct ui;
struct steam_launcher_adapter {
    void *context;
    int (*register_executable)(void *,const char *,const char *);
    int (*choose_executable)(void *,char *,unsigned int);
    int (*unregister_directory)(void *,const char *);
    void (*draw_header)(void *,int section,int focused);
    int (*header_hit)(void *,int x,int y);
    /* Queue one transient executable for the unchanged launcher, no catalog entry. */
    int (*launch_prerequisite)(void *,const char *);
};
void steam_ui_open(struct ui *ui, const char *runtime_dir);
int steam_ui_language_picker(struct ui *,const char *current);
/* Filtered per-install choice; returns 0 on Back or no recognized options. */
int steam_ui_install_language_picker(struct ui *,uint64_t available,char out[32]);
void steam_ui_settings(struct ui *,const char *runtime_dir);
/* Returns main-shell destination (0 Home, 1 Library, 4 Settings), or -1 Back. */
int steam_ui_library(struct ui *,const char *runtime_dir,int downloads);
void steam_ui_prepare(const char *runtime_dir);
void steam_ui_tick(struct ui *,const char *runtime_dir);
void steam_ui_set_launcher(const struct steam_launcher_adapter *);
void steam_ui_shutdown(void);
void steam_ui_set_developer(int on);
/* For the shell's own screens. Each answers from the provider's state and
 * returns at once; none of them draws.
 * The transfer in hand: returns 0 when there is no provider. */
struct steam_download_view;
struct steam_game;
int steam_ui_download(struct steam_download_view *);
/* Stop keeps what was downloaded and verified; resume takes it up from there. */
void steam_ui_stop(void);
int steam_ui_restart(void);
int steam_ui_resume(uint32_t app_id);
int steam_ui_online(void);
/* Whether Steam is downloading, installing or removing that game right now. */
int steam_ui_busy(uint32_t app_id);
/* After the shell removed a game's files: the account's list has it as not
 * installed again. Returns 0 while Steam is itself working on that game. */
int steam_ui_forget_install(uint32_t app_id);
/* An installation found on the card that Steam's side had no record of. */
void steam_ui_note_install(uint32_t app_id);
/* The account's games as the library has them loaded; 0 when it is not loaded. */
size_t steam_ui_owned(const char *runtime_dir,struct steam_game *,size_t capacity);
/* The page of one game: install, verify, choose its program, remove. */
void steam_ui_game(struct ui *,const char *runtime_dir,const struct steam_game *);
void steam_ui_sign_in(struct ui *,const char *runtime_dir);
void steam_ui_autoresume(const char *runtime_dir);
#endif
