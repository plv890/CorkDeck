#ifndef AUTORUN_STEAM_LIBRARY_H
#define AUTORUN_STEAM_LIBRARY_H
#include "steam_cm_message.h"
#define STEAM_LIBRARY_LIMIT 8192
struct steam_game { uint32_t app_id;char name[128],icon_hash[41];int installed,shared;unsigned int excluded;uint64_t lender; };
struct steam_library { struct steam_game *games;size_t count;uint64_t account; };
enum steam_library_error { STEAM_LIBRARY_OK,STEAM_LIBRARY_PROTOCOL,STEAM_LIBRARY_LIMIT_ERROR,STEAM_LIBRARY_MEMORY };
void steam_library_clear(struct steam_library *);
/* Build a complete replacement before changing caller state. No partial library. */
enum steam_library_error steam_library_read(const struct steam_cm_message *,uint64_t account,struct steam_library *);
/* Only server-included family apps with an owner are merged. Owned entries win. */
enum steam_library_error steam_library_merge_family(const struct steam_cm_message *,struct steam_library *);
int steam_library_matches(const struct steam_game *,const char *search);
size_t steam_library_page(const struct steam_library *,const char *search,size_t offset,struct steam_game *,size_t capacity,size_t *total);
#endif
