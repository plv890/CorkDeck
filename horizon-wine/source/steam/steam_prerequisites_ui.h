#ifndef AUTORUN_STEAM_PREREQUISITES_UI_H
#define AUTORUN_STEAM_PREREQUISITES_UI_H
#include <stdint.h>
struct ui;
struct steam_launcher_adapter;
int steam_prerequisites_ui(struct ui *,const char *root,uint32_t app,const char *script,
                           const struct steam_launcher_adapter *);
#endif
