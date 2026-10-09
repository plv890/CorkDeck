/*
 * What the application remembers about each entry of the library beyond the
 * catalogue: the time spent in it, its sessions and the category the user put
 * it in. One small file an entry, named by the entry's catalogue id, under
 * <runtime>/corkdeck/apps. Nothing here knows about screens or about Wine: the
 * runtime writes a session when a program ends, the shell reads them.
 */
#ifndef APP_RECORDS_H
#define APP_RECORDS_H

#define APP_RECORDS_SCHEMA 1
#define APP_SESSION_NOTE   96

struct app_record
{
    unsigned int id;
    char category[48];
    unsigned long long seconds;        /* in every session together */
    long long last_start;              /* seconds since 1970, 0 when never */
    unsigned int sessions;
};

struct app_session
{
    long long start;                   /* seconds since 1970 */
    unsigned int seconds;
    unsigned int exit_code;
    unsigned int frames;               /* frames presented, 0 when none or not counted */
    char build[24];
    char note[APP_SESSION_NOTE];       /* how it ended, in the runtime's own words */
};

/* A record that is not there yet reads as an empty one. Returns 0 on a file that cannot be read. */
int app_record_load( const char *runtime_dir, unsigned int id, struct app_record *record );
int app_record_save( const char *runtime_dir, const struct app_record *record );
/* Adds a session to the entry's list and to its totals. */
int app_record_add_session( const char *runtime_dir, unsigned int id, const struct app_session *session );
/* The newest sessions first; returns how many were read. */
int app_record_sessions( const char *runtime_dir, unsigned int id, struct app_session *sessions, int max );
/* Where the logs of an entry's sessions are kept: <runtime>/corkdeck/apps/<id>/. */
int app_record_folder( const char *runtime_dir, unsigned int id, char *out, unsigned int size );

#endif
