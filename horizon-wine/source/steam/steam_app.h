#ifndef AUTORUN_STEAM_APP_H
#define AUTORUN_STEAM_APP_H
#include <stddef.h>
#include <stdint.h>
#define STEAM_DEPOT_LIMIT 64
struct steam_depot { uint32_t id,source_app,dlc_app;uint64_t manifest,size,languages;int windows,arch32,arch64,language,language_specific,optional,english; };
struct steam_app { uint32_t id;char name[128],executable[256],installscript[256];int windows,language_missing,english_available;uint64_t available_languages;struct steam_depot depots[STEAM_DEPOT_LIMIT];size_t count;uint64_t total_size; };
/* Streaming KeyValues visitor: public Windows depots, selected language. */
int steam_app_parse(const void *data,size_t size,uint32_t appid,struct steam_app *);
int steam_app_parse_language(const void *,size_t,uint32_t,const char *language,struct steam_app *);
#endif
