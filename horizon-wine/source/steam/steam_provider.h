#ifndef AUTORUN_STEAM_PROVIDER_H
#define AUTORUN_STEAM_PROVIDER_H
#include <stdint.h>
#include "steam_library.h"
#include "steam_install.h"
struct steam_provider;
struct steam_app_view {uint32_t app_id;int state,windows,language_missing,english_available;uint64_t size,available_languages;unsigned int error;char installscript[256];};
struct steam_remove_view {uint32_t app_id;int busy;unsigned int error,generation;char directory[512];};
int steam_provider_inspect(struct steam_provider *,uint32_t);
void steam_provider_app_status(struct steam_provider *,struct steam_app_view *);
int steam_provider_uninstall(struct steam_provider *,uint32_t);
void steam_provider_remove_status(struct steam_provider *,struct steam_remove_view *);
int steam_provider_set_root(struct steam_provider *,const char *);
int steam_provider_install(struct steam_provider *,uint32_t);
/* NULL uses the global setting; override applies only to this queued install. */
int steam_provider_install_language(struct steam_provider *,uint32_t,const char *);
void steam_provider_cancel_download(struct steam_provider *);
int steam_provider_restart_download(struct steam_provider *);
int steam_provider_restart_poll(struct steam_provider *);
void steam_provider_download_status(struct steam_provider *,struct steam_download_view *);
enum steam_library_state { STEAM_LIBRARY_IDLE,STEAM_LIBRARY_LOADING,STEAM_LIBRARY_READY,STEAM_LIBRARY_FAILED };
struct steam_library_view { enum steam_library_state state;unsigned int error,generation,family_error;size_t count; };
void steam_provider_language(struct steam_provider *,char out[32]);
void steam_provider_language_preference(struct steam_provider *,char out[32]);
int steam_provider_set_language(struct steam_provider *,const char *);
unsigned int steam_provider_concurrency(struct steam_provider *);
int steam_provider_set_concurrency(struct steam_provider *,unsigned int);
struct steam_session_view { int present,saving;unsigned int error;int automatic; };
void steam_provider_session_status(struct steam_provider *,struct steam_session_view *);
int steam_provider_save_session(struct steam_provider *,const char *passphrase);
int steam_provider_save_automatic_session(struct steam_provider *);
int steam_provider_resume_session(struct steam_provider *);
int steam_provider_unlock_session(struct steam_provider *,const char *passphrase);
int steam_provider_refresh_library(struct steam_provider *);
void steam_provider_library_status(struct steam_provider *,struct steam_library_view *);
size_t steam_provider_library_page(struct steam_provider *,const char *,size_t,struct steam_game *,size_t,size_t *);
size_t steam_provider_installed_page(struct steam_provider *,const char *,size_t,struct steam_game *,size_t,size_t *);
enum steam_status { STEAM_OFFLINE,STEAM_CONNECTING,STEAM_AUTHENTICATING,STEAM_NEED_GUARD,
    STEAM_CONFIRM_DEVICE,STEAM_LOGGING_ON,STEAM_ONLINE,STEAM_ERROR };
struct steam_status_view { enum steam_status state; unsigned int error,guard_type; uint64_t steam_id;
    unsigned int failed_stage,cm_type,transport_error; };
struct steam_provider *steam_provider_create(void);
void steam_provider_destroy(struct steam_provider *);
int steam_provider_login(struct steam_provider *,const char *,const char *);
int steam_provider_login_qr(struct steam_provider *);
/* Sensitive ephemeral challenge: never log or persist. Empty when unavailable. */
void steam_provider_challenge(struct steam_provider *,char out[512]);
int steam_provider_guard(struct steam_provider *,const char *);
void steam_provider_logout(struct steam_provider *);
void steam_provider_status(struct steam_provider *,struct steam_status_view *);
#endif
