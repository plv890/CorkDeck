#ifndef AUTORUN_STEAM_FILE_H
#define AUTORUN_STEAM_FILE_H
/* Recoverable replacement of one provider-owned regular metadata file.
 * Horizon may refuse rename over an existing destination. No symlink following.
 * Not a multi-file transaction or a power-loss durability guarantee. */
int steam_file_recover(const char *path);
int steam_file_replace(const char *temporary, const char *path);
#endif
