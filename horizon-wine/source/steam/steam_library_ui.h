#ifndef AUTORUN_STEAM_LIBRARY_UI_H
#define AUTORUN_STEAM_LIBRARY_UI_H
struct ui;
struct steam_provider;
struct steam_launcher_adapter;
struct steam_download_view;
struct steam_library_view;
struct steam_game;
void steam_library_ui_open(struct ui *,struct steam_provider *,const char *,const struct steam_launcher_adapter *);
int steam_library_ui_page(struct ui *,struct steam_provider *,const char *,const struct steam_launcher_adapter *,int tab);
/* One game's page on its own, as the library's own list opens it. */
int steam_library_ui_game(struct ui *,struct steam_provider *,const char *root,const struct steam_game *,
                          const struct steam_launcher_adapter *);
#endif
