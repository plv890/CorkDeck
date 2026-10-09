#ifndef AUTORUN_STEAM_STORAGE_H
#define AUTORUN_STEAM_STORAGE_H
#include <stdint.h>
/* AppID is the stable identity; display names are only filesystem components. */
int steam_storage_directory(const char *root,uint32_t app,char out[512]);
int steam_storage_pending(const char *root,uint32_t app,char original[512]);
int steam_storage_plan(const char *root,uint32_t app,const char *title,char out[1024]);
int steam_storage_record(const char *root,uint32_t app,const char *directory);
/* Deletes only an installation with a matching Autorun marker. Never follows links.
 * Failed deletion retains an AppID-scoped trash directory for retry. */
unsigned int steam_storage_uninstall(const char *root,uint32_t app,int (*tick)(void *),void *context);
/* Caller must stop/join all download writers first. Idempotently deletes only
 * AppID-scoped staging/plan/manifests, never drive_c or installed metadata.
 * Returns 3112 plus filesystem errno on failure; no Steam connection needed. */
unsigned int steam_storage_discard(const char *root,uint32_t app,unsigned int *filesystem_error);
#endif
