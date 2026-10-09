#ifndef AUTORUN_STEAM_INSTALL_H
#define AUTORUN_STEAM_INSTALL_H
#include "steam_backend.h"
enum steam_download_state { STEAM_DOWNLOAD_IDLE,STEAM_DOWNLOAD_PLANNING,STEAM_DOWNLOAD_MANIFESTS,STEAM_DOWNLOAD_FILES,
    STEAM_DOWNLOAD_VERIFYING,STEAM_DOWNLOAD_COMMITTING,STEAM_DOWNLOAD_COMPLETE,STEAM_DOWNLOAD_CANCELLED,STEAM_DOWNLOAD_ERROR,
    STEAM_DOWNLOAD_CLEANING,STEAM_DOWNLOAD_DISCARDED };
static inline int steam_download_active(enum steam_download_state s) {
    return (s > STEAM_DOWNLOAD_IDLE && s < STEAM_DOWNLOAD_COMPLETE) || s == STEAM_DOWNLOAD_CLEANING;
}
struct steam_download_view { enum steam_download_state state;uint32_t app_id;unsigned int error,http_status,transport,failed_stage;
    uint64_t downloaded,total,network_bytes,free_bytes,elapsed_ms,network_ms,decode_ms,write_ms,sync_ms,available_languages;
    unsigned int integrity_detail,chunk_format,integrity_retries,depot_id;
    uint64_t first_byte_ms,body_ms,verify_ms,resume_ms,writer_wait_ms;unsigned int requests,connections,parallelism,peak_requests,diagnostic_error,cleanup_errno,batch_files,batch_peak_files;
    /* Set by the provider after a failure: attempts in a row that verified nothing new, and whether Continue is of no more use. */
    unsigned int attempts,stuck;int free_known;double bytes_per_second;char file[128],executable[512],language[32]; };
struct steam_install_callbacks { void *context;int (*tick)(void *);void (*progress)(void *,const struct steam_download_view *);
    /* Set together with tick cancellation only for explicit user discard. */
    int (*discard_requested)(void *); };
/* Interrupted transfers retain validated staging. Explicit discard removes all
 * AppID installation cache after network/SD workers have stopped. */
int steam_install_run(struct steam_backend *,const char *root,uint32_t,const struct steam_install_callbacks *);
void steam_install_discard(const char *,struct steam_download_view *,const struct steam_install_callbacks *);
int steam_install_present(const char *root,uint32_t app);
int steam_install_executable(const char *,uint32_t,char out[512]);
int steam_install_select_executable(const char *,uint32_t,const char *);
#endif
