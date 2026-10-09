/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
/*
 * What a Steam install script (installscript.vdf) leaves on the computer for
 * the game's own programs to find: registry values, and folders copied to the
 * user's profile. Steam runs the script once after an installation; a game
 * installed here never had it run, and its launcher then says the game is not
 * installed (Fallout 3 reads "Installed Path"), or the game itself gives up.
 *
 * Only those two sections are read. "Run Process" starts installers and is
 * left to the prerequisites; nothing in a script is ever executed.
 *
 * Pure logic: the caller reads the file, and does what the plan says.
 */
#ifndef AUTORUN_STEAM_INSTALL_SCRIPT_H
#define AUTORUN_STEAM_INSTALL_SCRIPT_H

#include <stddef.h>
#include <stdint.h>

#define STEAM_SCRIPT_MAX_BYTES 65536
#define STEAM_SCRIPT_MAX_OPS   96

enum steam_script_op_kind
{
    STEAM_SCRIPT_KEY,     /* the registry key 'key', with no value */
    STEAM_SCRIPT_STRING,  /* REG_SZ 'name' = 'text' under 'key' */
    STEAM_SCRIPT_DWORD,   /* REG_DWORD 'name' = 'number' under 'key' */
    STEAM_SCRIPT_COPY     /* the folder 'key' copied to the folder 'text' */
};

struct steam_script_op
{
    int kind;
    /* A registry key as the NT API names it (\Registry\Machine\Software\...),
     * or the Windows path of the folder to copy. */
    char key[320];
    char name[128];
    /* The string value or the Windows path of the destination folder. */
    char text[520];
    uint32_t number;
};

struct steam_script_plan
{
    unsigned int count;
    /* Entries left out: a place this prefix does not have, a key outside
     * Software or in the part of it Windows itself owns, or more than fit. */
    unsigned int skipped;
    struct steam_script_op ops[STEAM_SCRIPT_MAX_OPS];
};

/* Windows paths, without a backslash at the end. A place that is NULL is not
 * known, and an entry that names it is skipped. */
struct steam_script_places
{
    const char *install_dir;       /* %INSTALLDIR% */
    const char *documents;         /* %USER_MYDOCS% */
    const char *public_documents;  /* %COMMON_MYDOCS% */
    const char *app_data;          /* %APPDATA% */
    const char *local_app_data;    /* %LOCALAPPDATA% */
    const char *windows;           /* %WinDir% */
    const char *root_drive;        /* %ROOTDRIVE%, as "C:" */
    const char *user_sid;          /* whose HKEY_CURRENT_USER it is */
    const char *language;          /* Steam's name of the game's language */
};

/* 1 when the text was read to its end, 0 when it is not a script. */
int steam_install_script_plan( const char *text, size_t size, const struct steam_script_places *places,
                               struct steam_script_plan *plan );

/* Stands for the script and where it was applied, to tell that it was. */
uint64_t steam_install_script_mark( const char *text, size_t size, const struct steam_script_places *places );

#endif
