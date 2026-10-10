#ifndef AUTORUN_STEAM_PREREQUISITES_UI_H
#define AUTORUN_STEAM_PREREQUISITES_UI_H
#include <stdint.h>
struct ui;
struct steam_launcher_adapter;
int steam_prerequisites_ui(struct ui *,const char *root,uint32_t app,const char *script,
                           const struct steam_launcher_adapter *);
/* The download of the Visual C++ installer with its progress on the screen;
 * nothing when it is already on the card. Returns a steam_prereq_error or 0. */
unsigned int steam_prerequisites_fetch_ui(struct ui *,const char *root,unsigned int arch);
#endif
