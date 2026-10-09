#include "app_records.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int folder( const char *runtime_dir, char *out, size_t size )
{
    int n = snprintf( out, size, "%s/corkdeck", runtime_dir );

    if (n < 0 || (size_t)n >= size) return 0;
    if (mkdir( out, 0777 ) && errno != EEXIST) return 0;
    n = snprintf( out, size, "%s/corkdeck/apps", runtime_dir );
    if (n < 0 || (size_t)n >= size) return 0;
    return !mkdir( out, 0777 ) || errno == EEXIST;
}

static int file( const char *runtime_dir, unsigned int id, const char *suffix, char *out, size_t size )
{
    char base[600];
    int n;

    if (!id || !folder( runtime_dir, base, sizeof(base) )) return 0;
    n = snprintf( out, size, "%s/%u%s", base, id, suffix );
    return n > 0 && (size_t)n < size;
}

int app_record_folder( const char *runtime_dir, unsigned int id, char *out, unsigned int size )
{
    if (!file( runtime_dir, id, "", out, size )) return 0;
    return !mkdir( out, 0777 ) || errno == EEXIST;
}

int app_record_load( const char *runtime_dir, unsigned int id, struct app_record *record )
{
    char path[700], line[160];
    FILE *f;

    memset( record, 0, sizeof(*record) );
    record->id = id;
    if (!file( runtime_dir, id, ".txt", path, sizeof(path) )) return 0;
    if (!(f = fopen( path, "rb" ))) return errno == ENOENT;
    while (fgets( line, sizeof(line), f ))
    {
        line[strcspn( line, "\r\n" )] = 0;
        if (!strncmp( line, "category=", 9 )) snprintf( record->category, sizeof(record->category), "%s", line + 9 );
        else if (!strncmp( line, "seconds=", 8 )) record->seconds = strtoull( line + 8, NULL, 10 );
        else if (!strncmp( line, "last-start=", 11 )) record->last_start = strtoll( line + 11, NULL, 10 );
        else if (!strncmp( line, "sessions=", 9 )) record->sessions = (unsigned int)strtoul( line + 9, NULL, 10 );
    }
    fclose( f );
    return 1;
}

int app_record_save( const char *runtime_dir, const struct app_record *record )
{
    char path[700], temp[720];
    FILE *f;
    int ok;

    if (!file( runtime_dir, record->id, ".txt", path, sizeof(path) )) return 0;
    snprintf( temp, sizeof(temp), "%s.new", path );
    if (!(f = fopen( temp, "wb" ))) return 0;
    ok = fprintf( f, "schema=%d\ncategory=%s\nseconds=%llu\nlast-start=%lld\nsessions=%u\n", APP_RECORDS_SCHEMA,
                  record->category, record->seconds, record->last_start, record->sessions ) > 0;
    ok = !fclose( f ) && ok;
    /* FAT cannot rename over an existing file. */
    if (ok) remove( path );
    if (!ok || rename( temp, path ))
    {
        remove( temp );
        return 0;
    }
    return 1;
}

int app_record_add_session( const char *runtime_dir, unsigned int id, const struct app_session *session )
{
    struct app_record record;
    char path[700], note[APP_SESSION_NOTE];
    FILE *f;
    size_t i;
    int ok;

    if (!app_record_load( runtime_dir, id, &record ) || !file( runtime_dir, id, ".sessions.txt", path, sizeof(path) ))
        return 0;
    /* One line a session, tab-separated, so the note may not hold either. */
    snprintf( note, sizeof(note), "%s", session->note );
    for (i = 0; note[i]; i++) if (note[i] == '\t' || note[i] == '\n' || note[i] == '\r') note[i] = ' ';
    if (!(f = fopen( path, "ab" ))) return 0;
    ok = fprintf( f, "%lld\t%u\t%u\t%u\t%s\t%s\n", session->start, session->seconds, session->exit_code,
                  session->frames, session->build[0] ? session->build : "-", note ) > 0;
    ok = !fclose( f ) && ok;
    record.seconds += session->seconds;
    record.last_start = session->start;
    record.sessions++;
    return app_record_save( runtime_dir, &record ) && ok;
}

int app_record_sessions( const char *runtime_dir, unsigned int id, struct app_session *sessions, int max )
{
    char path[700], line[320];
    int count = 0, i;
    FILE *f;

    if (max <= 0 || !file( runtime_dir, id, ".sessions.txt", path, sizeof(path) ) || !(f = fopen( path, "rb" )))
        return 0;
    while (fgets( line, sizeof(line), f ))
    {
        struct app_session s;
        char *field[6], *at = line;

        memset( &s, 0, sizeof(s) );
        line[strcspn( line, "\r\n" )] = 0;
        for (i = 0; i < 6; i++)
        {
            field[i] = at;
            if (i < 5)
            {
                if (!(at = strchr( at, '\t' ))) break;
                *at++ = 0;
            }
        }
        if (i < 6) continue;   /* a line cut short by a flat battery */
        s.start = strtoll( field[0], NULL, 10 );
        s.seconds = (unsigned int)strtoul( field[1], NULL, 10 );
        s.exit_code = (unsigned int)strtoul( field[2], NULL, 10 );
        s.frames = (unsigned int)strtoul( field[3], NULL, 10 );
        snprintf( s.build, sizeof(s.build), "%s", field[4] );
        snprintf( s.note, sizeof(s.note), "%s", field[5] );
        /* The newest first: the list keeps the last max of them. */
        if (count == max) memmove( sessions + 1, sessions, (max - 1) * sizeof(*sessions) ), count--;
        else memmove( sessions + 1, sessions, count * sizeof(*sessions) );
        sessions[0] = s;
        count++;
    }
    fclose( f );
    return count;
}
