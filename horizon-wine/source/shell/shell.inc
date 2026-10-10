/*
 * CorkDeck's shell: the screens a person moves between. Part of launcher.c's
 * translation unit (it is included there), because it stands on the library,
 * artwork and launch code of that file; everything it knows about settings,
 * sessions and Steam it asks of their services (app/app_settings.h,
 * app/app_records.h, steam/steam_ui.h) and never works out for itself.
 *
 * One scheme of buttons everywhere: A opens or confirms, B goes back, X is the
 * menu of what is in focus, Y searches, L and R change tab, ZL and ZR page,
 * Plus opens the main menu and Minus the quick panel. Every text is written in
 * English and goes through the drawing layer's translation (shell_strings.inc).
 */

#include <time.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include "../app/app_settings.h"
#include "../app/app_records.h"
#include "../app/app_version.h"
#include "../config_json.h"
#include "../system_language.h"
#include "../steam/steam_provider.h"
#include "shell_strings.inc"

#define SH_BLUE      (SDL_Color){ 26, 159, 255, 255 }
#define SH_GREEN     (SDL_Color){ 92, 194, 74, 255 }
#define SH_GREEN_D   (SDL_Color){ 47, 143, 47, 255 }
#define SH_AMBER     (SDL_Color){ 224, 166, 60, 255 }
#define SH_PANEL     (SDL_Color){ 24, 33, 44, 255 }
#define SH_PANEL2    (SDL_Color){ 33, 44, 58, 255 }
#define SH_LINE      (SDL_Color){ 44, 58, 75, 255 }
#define SH_SIDE      (SDL_Color){ 19, 27, 37, 255 }
#define SH_CORK      (SDL_Color){ 199, 154, 98, 255 }
#define SH_MARGIN    64
#define SH_TOP       72
/* The account's games that are not installed: an account may hold thousands. */
#define SH_OWNED     4096
#define SH_ITEMS     (LAUNCHER_MAX_ENTRIES + SH_OWNED)

enum sh_tab { TAB_ALL, TAB_FAVORITES, TAB_INSTALLED, TAB_AVAILABLE, TAB_LOCAL, TAB_CATEGORY, TAB_COUNT };
enum sh_page { PAGE_HOME, PAGE_LIBRARY, PAGE_DOWNLOADS, PAGE_SETTINGS, PAGE_MAINTENANCE, PAGE_STORAGE,
               PAGE_DIAGNOSTICS, PAGE_POWER, PAGE_COUNT };
enum sh_sort { SORT_TITLE, SORT_RECENT, SORT_ADDED, SORT_TIME, SORT_COUNT };

static struct shell
{
    int language;
    int page;                              /* PAGE_HOME or PAGE_LIBRARY: the two that stay open */
    int home_row, home_col;
    int tab, sort, list_view, zone, tool, selection, top;
    char category[48];
    int items[SH_ITEMS];                   /* a program's index, or -1 - n for the n-th Steam game not installed */
    int count;
    unsigned char steam[LAUNCHER_MAX_ENTRIES], known[LAUNCHER_MAX_ENTRIES];
    struct app_record records[LAUNCHER_MAX_ENTRIES];
    struct steam_game owned[SH_OWNED];
    int owned_count;
    int steam_asked;      /* the saved session was tried once this run */
    int developer;        /* developer mode: the Diagnostics page, the card's probe, the logs */
    Uint32 owned_read;
    struct wine_nx_config global;
    char global_path[512];
    struct { SDL_Rect rect; int code; } hits[128];
    int hit_count;
    /* Asked once a frame or once a second, not once for every card drawn. */
    struct steam_download_view transfer;
    int transfer_active, transfer_known;
    Uint32 status_read;
    int status, hour, minute, battery, charging;
    unsigned int app_id[LAUNCHER_MAX_ENTRIES];
    /* Covers and wide pictures of Steam games, fetched on a thread of their own. */
    SDL_Thread *art_thread;
    SDL_atomic_t art_stop, art_done;
    int art_index;
    unsigned int art_app;
    /* Where the main menu, opened from inside a screen, said to go: every
     * screen on the way back to the root returns when this is set. */
    int pending;
    char art_portrait[512], art_hero[512];
} sh;

/***********************************************************************
 * Small things every screen uses
 */

static void sh_hit( int x, int y, int w, int h, int code )
{
    if (sh.hit_count < (int)(sizeof(sh.hits) / sizeof(sh.hits[0])))
    {
        sh.hits[sh.hit_count].rect = (SDL_Rect){ x, y, w, h };
        sh.hits[sh.hit_count++].code = code;
    }
}

static int sh_hit_at( int x, int y )
{
    SDL_Point point = { x, y };
    int i;

    for (i = sh.hit_count - 1; i >= 0; i--)
        if (SDL_PointInRect( &point, &sh.hits[i].rect )) return sh.hits[i].code;
    return -1;
}

/* A text that fits is drawn from the cache of ready lines; only one that has
 * to be cut is measured, which is the slow part of drawing text. */
static void sh_fit( struct ui *ui, TTF_Font *font, int x, int y, int max_width, const char *text, SDL_Color color,
                    int scroll )
{
    if (!text || !text[0] || max_width <= 0) return;
    if (ui_text_width( ui, font, text ) <= max_width) ui_text( ui, font, x, y, text, color );
    else ui_text_fit( ui, font, x, y, max_width, text, color, scroll );
}

/* A title on two lines at most, split at a space once and remembered. */
static void sh_title( struct ui *ui, int x, int bottom, int width, const char *title, SDL_Color color )
{
    static struct { char key[128]; int width; char first[96], second[96]; } memo[96];
    unsigned int hash = (unsigned int)width;
    const char *c;
    int line = TTF_FontHeight( ui->small ) + 2, slot;

    title = ui->translate ? ui->translate( title ) : title;
    for (c = title; *c; c++) hash = hash * 31 + (unsigned char)*c;
    slot = hash % 96;
    if (strcmp( memo[slot].key, title ) || memo[slot].width != width)
    {
        size_t length = strlen( title ), cut = length;

        snprintf( memo[slot].key, sizeof(memo[slot].key), "%s", title );
        memo[slot].width = width;
        snprintf( memo[slot].first, sizeof(memo[slot].first), "%s", title );
        memo[slot].second[0] = 0;
        while (ui_text_width( ui, ui->small, memo[slot].first ) > width && cut > 1)
        {
            for (cut--; cut > 1 && title[cut] != ' '; cut--) {}
            if (cut <= 1)
            {
                /* One long word: it is cut where it ends, not broken in two. */
                cut = length;
                snprintf( memo[slot].first, sizeof(memo[slot].first), "%s", title );
                break;
            }
            snprintf( memo[slot].first, sizeof(memo[slot].first), "%.*s", (int)cut, title );
        }
        if (cut < length) snprintf( memo[slot].second, sizeof(memo[slot].second), "%s", title + cut + (title[cut] == ' ') );
    }
    if (memo[slot].second[0])
    {
        ui_text( ui, ui->small, x, bottom - 2 * line, memo[slot].first, color );
        sh_fit( ui, ui->small, x, bottom - line, width, memo[slot].second, color, 0 );
    }
    else sh_fit( ui, ui->small, x, bottom - line, width, memo[slot].first, color, 0 );
}

static int sh_global_key( struct launcher *l, int button );

static void sh_focus_ring( struct ui *ui, int x, int y, int w, int h, int radius )
{
    (void)radius;
    ui_outline( ui, x - 5, y - 5, w + 10, h + 10, 6, 3, SH_BLUE );
}

static const char *sh_tr( const char *text ) { return shell_translate( text ); }

/* 3 h 20 min, 45 min, under a minute. */
static const char *sh_duration( unsigned long long seconds, char *out, size_t size )
{
    if (seconds >= 3600) snprintf( out, size, sh_tr( "%llu h %llu min" ), seconds / 3600, seconds % 3600 / 60 );
    else if (seconds >= 60) snprintf( out, size, sh_tr( "%llu min" ), seconds / 60 );
    else snprintf( out, size, "%s", sh_tr( "under a minute" ) );
    return out;
}

static const char *sh_date( long long when, char *out, size_t size )
{
    time_t t = (time_t)when;
    struct tm *tm = localtime( &t );

    if (!when || !tm) snprintf( out, size, "%s", sh_tr( "never" ) );
    else snprintf( out, size, "%02d.%02d.%04d %02d:%02d", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900,
                   tm->tm_hour, tm->tm_min );
    return out;
}

static int sh_note_clock( int *hour, int *minute )
{
    int battery, charging;

    return launcher_platform_status( hour, minute, &battery, &charging ) & LAUNCHER_STATUS_CLOCK;
}

/* Whether a program belongs to a Steam installation: the marker the installer
 * leaves in its folder, looked for once. */
static int sh_is_steam( struct launcher *l, int index )
{
    char dir[512], marker[560];
    int depth;

    if (sh.known[index] & 1) return sh.steam[index];
    sh.known[index] |= 1;
    sh.steam[index] = 0;
    snprintf( dir, sizeof(dir), "%s", l->programs[index].path );
    for (depth = 0; depth < 8; depth++)
    {
        parent_dir( dir );
        if (is_root( dir )) break;
        snprintf( marker, sizeof(marker), "%s/autorun-steam-install.ini", dir );
        if (file_exists( marker ))
        {
            char line[128];
            FILE *f = fopen( marker, "rb" );

            sh.steam[index] = 1;
            sh.app_id[index] = 0;
            while (f && fgets( line, sizeof(line), f ))
                if (!strncmp( line, "appid=", 6 )) sh.app_id[index] = (unsigned int)strtoul( line + 6, NULL, 10 );
            if (f) fclose( f );
            /* A folder brought over from another card has the marker and not
             * the record Steam's side finds it by, and was offered there as a
             * game still to install. */
            if (sh.app_id[index] && !steam_storage_directory( l->options->runtime_dir, sh.app_id[index], marker ))
            {
                int recorded = steam_storage_record( l->options->runtime_dir, sh.app_id[index], dir );

                launcher_log( "[SHELL] the Steam installation of app %u in %s had no record: %s", sh.app_id[index], dir,
                              recorded ? "written" : "could not be written" );
                if (recorded) { steam_ui_note_install( sh.app_id[index] ); sh.owned_read = 0; }
            }
            break;
        }
    }
    return sh.steam[index];
}

static struct app_record *sh_record( struct launcher *l, int index )
{
    if (!(sh.known[index] & 2))
    {
        sh.known[index] |= 2;
        if (!l->programs[index].catalog_id ||
            !app_record_load( l->options->runtime_dir, l->programs[index].catalog_id, &sh.records[index] ))
            memset( &sh.records[index], 0, sizeof(sh.records[index]) );
    }
    return &sh.records[index];
}

static void sh_forget( int index ) { sh.known[index] = 0; }

/* The general settings: read whole, changed and written at once, so the
 * runtime finds them as the user left them. */
static void sh_global_load( struct launcher *l )
{
    snprintf( sh.global_path, sizeof(sh.global_path), "%s/config/settings.json", l->options->runtime_dir );
    wine_nx_config_load( &sh.global, sh.global_path );
}

static void sh_apply_global( struct launcher *l )
{
    struct app_settings s;
    int language;

    app_settings_resolve( &s, &sh.global, NULL, NULL, NULL, NULL );
    language = app_settings_get( &s, APP_SET_LANGUAGE );
    if (language == 1) sh.language = APP_LANG_EN;
    else if (language == 2) sh.language = APP_LANG_RU;
    else sh.language = !strncmp( nx_system_language().console, "ru", 2 ) ? APP_LANG_RU : APP_LANG_EN;
#ifndef __SWITCH__
    if (language == 0 && getenv( "CORKDECK_LANGUAGE" ))
        sh.language = !strcmp( getenv( "CORKDECK_LANGUAGE" ), "ru" ) ? APP_LANG_RU : APP_LANG_EN;
#endif
    shell_language = sh.language;
    if (sh.developer && app_settings_get( &s, APP_SET_DEVELOPER ) <= 0) lan_log_stop();
    sh.developer = app_settings_get( &s, APP_SET_DEVELOPER ) > 0;
    steam_ui_set_developer( sh.developer );
    l->ui.animations = app_settings_get( &s, APP_SET_ANIMATIONS ) > 0;
    l->ui.muted = app_settings_get( &s, APP_SET_SOUNDS ) <= 0;
    l->options->reopen_launcher = app_settings_get( &s, APP_SET_REOPEN_SHELL ) > 0;
    l->options->dxvk_on_add = app_settings_get( &s, APP_SET_DXVK_FOR_NEW ) > 0;
}

/* The transfer Steam is making, if any. */
static int sh_download( struct steam_download_view *view )
{
    if (!sh.transfer_known)
    {
        memset( &sh.transfer, 0, sizeof(sh.transfer) );
        sh.transfer_active = steam_ui_download( &sh.transfer ) && steam_download_active( sh.transfer.state );
        sh.transfer_known = 1;
    }
    *view = sh.transfer;
    return sh.transfer_active;
}

/* The clock and the battery, read once a second. */
static int sh_status( int *hour, int *minute, int *battery, int *charging )
{
    if (!sh.status_read || SDL_GetTicks() - sh.status_read >= 1000)
    {
        sh.status = launcher_platform_status( &sh.hour, &sh.minute, &sh.battery, &sh.charging );
        sh.status_read = SDL_GetTicks() | 1;
    }
    *hour = sh.hour; *minute = sh.minute; *battery = sh.battery; *charging = sh.charging;
    return sh.status;
}

static const char *sh_owned_name( uint32_t app_id )
{
    int i;

    for (i = 0; i < sh.owned_count; i++) if (sh.owned[i].app_id == app_id) return sh.owned[i].name;
    return "Steam";
}

/***********************************************************************
 * Covers and wide pictures of Steam games
 *
 * An installed Steam game without a cover gets Steam's own: the library
 * capsule for its card and the library hero for its page, from Steam's public
 * content servers into the card's steam/artwork folder, one game at a time on
 * a thread of its own. The entry then names the files like any other artwork.
 */

#include <curl/curl.h>

static size_t sh_art_write( char *data, size_t size, size_t count, void *file )
{
    return fwrite( data, size, count, file );
}

/* How the card answers right now, for the log: a file opened and closed, a
 * file asked about, and the free space. Says when any of them changes. */
static char sh_probe_text[240];

/* The heap in use, in megabytes. glibc has renamed the call that counts it. */
static unsigned int sh_heap_used_mb( void )
{
#if defined(__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 33)
    struct mallinfo2 heap = mallinfo2();
#else
    struct mallinfo heap = mallinfo();
#endif
    return (unsigned)(heap.uordblks >> 20);
}

static void sh_card_probe( struct launcher *l, const char *when )
{
    static int last = -1;
    static Uint32 said;
    char path[512];
    struct stat st;
    struct statvfs vfs;
    int fd, open_errno = 0, stat_errno = 0, vfs_errno = 0, state;
    unsigned int open_fs = 0, stat_fs = 0, vfs_fs = 0;

    static Uint32 tried;

    if (when[0] == 't' && !sh.developer) return;
    if (when[0] == 't' && tried && SDL_GetTicks() - tried < 250) return;
    if (sh_probe_text[0] == 'C') return;   /* said once; the first failure is the one that tells */
    tried = SDL_GetTicks() | 1;
    runtime_file( l, "launcher-library-v2.ini", path, sizeof(path) );
    errno = 0;
    fd = open( path, O_RDONLY );
    if (fd < 0) open_errno = errno;
#ifdef __SWITCH__
    if (fd < 0) open_fs = fsdevGetLastResult();
#endif
    if (fd >= 0) close( fd );
    errno = 0;
    if (stat( path, &st ))
    {
        stat_errno = errno;
#ifdef __SWITCH__
        stat_fs = fsdevGetLastResult();
#endif
    }
    errno = 0;
    if (statvfs( LAUNCHER_DRIVE_C, &vfs ))
    {
        vfs_errno = errno;
#ifdef __SWITCH__
        vfs_fs = fsdevGetLastResult();
#endif
    }
    state = (fd < 0) | (stat_errno ? 2 : 0) | (vfs_errno ? 4 : 0);
    if (state)
    {
        /* On the screen too: a card that does not answer cannot take the log. */
        void *block = malloc( 4 << 20 );
        char dlls[64];
        int tone;

        snprintf( sh_probe_text, sizeof(sh_probe_text), "CARD %s %ums fs=%x/%x/%x heap %s %uM art=%d dll=%s steam=%d | %s",
                  when, SDL_GetTicks(), open_fs, stat_fs, vfs_fs, block ? "ok" : "FULL", sh_heap_used_mb(),
                  sh.art_thread != NULL, launcher_dlls_status( l->dlls, dlls, sizeof(dlls), &tone ), steam_ui_online(),
                  launcher_last_log );
        free( block );
    }
    else if (!sh_probe_text[0] || sh_probe_text[0] == 'c')
        snprintf( sh_probe_text, sizeof(sh_probe_text), "card ok %s %us fd=%d", when, SDL_GetTicks() / 1000, fd );
    if (state == last && when[0] == 't' && SDL_GetTicks() - said < 60000) return;
    last = state;
    said = SDL_GetTicks();
    launcher_log( "[CARD] %s at %u ms: open fd=%d errno=%d fs=0x%x; stat errno=%d fs=0x%x; free space errno=%d fs=0x%x",
                  when, SDL_GetTicks(), fd, open_errno, open_fs, stat_errno, stat_fs, vfs_errno, vfs_fs );
}

/* Lets a download of a picture be given up when the shell has to go on. */
static int sh_art_progress( void *unused, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d )
{
    (void)unused; (void)a; (void)b; (void)c; (void)d;
    return SDL_AtomicGet( &sh.art_stop );
}

static int sh_art_fetch( const char *url, const char *path )
{
    char part[560];
    CURL *curl;
    FILE *file;
    long http = 0;
    int ok = 0;

    snprintf( part, sizeof(part), "%s.part", path );
    if (!(file = fopen( part, "wb" )))
    {
        launcher_log( "[ART] %s: could not create the file, errno %d", url, errno );
        return 0;
    }
    launcher_log( "[ART] %s: asking", url );
    if ((curl = curl_easy_init()))
    {
        CURLcode code;

        curl_easy_setopt( curl, CURLOPT_URL, url );
        curl_easy_setopt( curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS );
        curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 0L );
        curl_easy_setopt( curl, CURLOPT_SSL_VERIFYPEER, 1L );
        curl_easy_setopt( curl, CURLOPT_SSL_VERIFYHOST, 2L );
        curl_easy_setopt( curl, CURLOPT_CONNECTTIMEOUT, 8L );
        curl_easy_setopt( curl, CURLOPT_TIMEOUT, 25L );
        curl_easy_setopt( curl, CURLOPT_NOSIGNAL, 1L );
        curl_easy_setopt( curl, CURLOPT_NOPROGRESS, 0L );
        curl_easy_setopt( curl, CURLOPT_XFERINFOFUNCTION, sh_art_progress );
        curl_easy_setopt( curl, CURLOPT_MAXFILESIZE, 4L * 1024 * 1024 );
        curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, sh_art_write );
        curl_easy_setopt( curl, CURLOPT_WRITEDATA, file );
        code = curl_easy_perform( curl );
        ok = code == CURLE_OK &&
             curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &http ) == CURLE_OK && http == 200;
        launcher_log( "[ART] %s: curl %d, HTTP %ld", url, (int)code, http );
        curl_easy_cleanup( curl );
    }
    ok = !fclose( file ) && ok;
    if (ok) { remove( path ); ok = !rename( part, path ); }
    if (!ok) remove( part );
    return ok;
}

static int sh_art_worker( void *unused )
{
    char url[200];
    int got = 0;

    (void)unused;
    snprintf( url, sizeof(url), "https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/%u/library_600x900.jpg", sh.art_app );
    if (!SDL_AtomicGet( &sh.art_stop ) && sh_art_fetch( url, sh.art_portrait )) got |= 1;
    snprintf( url, sizeof(url), "https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/%u/library_hero.jpg", sh.art_app );
    if (!SDL_AtomicGet( &sh.art_stop ) && sh_art_fetch( url, sh.art_hero )) got |= 2;
    SDL_AtomicSet( &sh.art_done, got | 4 );
    return 0;
}

/* Called while a card is drawn: asks for the pictures of a Steam game that has none. */
static void sh_art_want( struct launcher *l, int index )
{
    struct program *p = &l->programs[index];
    char folder[512];

    if (launcher_dlls_busy( l->dlls )) return;
    if (sh.art_thread || (sh.known[index] & 4) || !sh_is_steam( l, index ) || !sh.app_id[index] ||
        (p->portrait_art[0] && p->hero_art[0]) || p->missing) return;
    sh.known[index] |= 4;
    runtime_file( l, "steam", folder, sizeof(folder) );
    mkdir( folder, 0777 );
    runtime_file( l, "steam/artwork", folder, sizeof(folder) );
    mkdir( folder, 0777 );
    snprintf( sh.art_portrait, sizeof(sh.art_portrait), "%s/corkdeck-%u-cover.jpg", folder, sh.app_id[index] );
    snprintf( sh.art_hero, sizeof(sh.art_hero), "%s/corkdeck-%u-hero.jpg", folder, sh.app_id[index] );
    sh.art_index = index;
    sh.art_app = sh.app_id[index];
    SDL_AtomicSet( &sh.art_done, 0 );
    sh.art_thread = SDL_CreateThreadWithStackSize( sh_art_worker, "corkdeck-art", 256 * 1024, NULL );
}

/* Once a frame: takes what the thread fetched and gives it to the entry. */
static void sh_art_pump( struct launcher *l )
{
    int done = SDL_AtomicGet( &sh.art_done );
    struct program *p;

    if (!sh.art_thread || !(done & 4)) return;
    SDL_WaitThread( sh.art_thread, NULL );
    sh.art_thread = NULL;
    p = &l->programs[sh.art_index];
    if ((done & 1) && !p->portrait_art[0])
    {
        snprintf( p->portrait_art, sizeof(p->portrait_art), "%s", sh.art_portrait );
        if (p->icon) SDL_DestroyTexture( p->icon );
        p->icon = NULL;
        p->icon_state = ICON_UNKNOWN;
    }
    if ((done & 2) && !p->hero_art[0])
    {
        snprintf( p->hero_art, sizeof(p->hero_art), "%s", sh.art_hero );
        if (p->hero_icon) SDL_DestroyTexture( p->hero_icon );
        p->hero_icon = NULL;
        p->hero_state = ICON_UNKNOWN;
    }
    if (done & 3) save_library( l );
}

static void sh_art_stop( void )
{
    if (!sh.art_thread) return;
    SDL_AtomicSet( &sh.art_stop, 1 );
    SDL_WaitThread( sh.art_thread, NULL );
    sh.art_thread = NULL;
}

/***********************************************************************
 * Top bar, buttons, cards
 */

static void sh_top( struct launcher *l, const char *crumb, const char *detail )
{
    struct ui *ui = &l->ui;
    struct steam_download_view d;
    char text[160];
    int x = SH_MARGIN, right = ui->width - SH_MARGIN, cy = 40, hour, minute, battery, charging, status;

    /* The mark: a cork, and the name. */
    ui_rounded( ui, x, cy - 13, 18, 24, 5, SH_CORK );
    ui_fill( ui, x + 2, cy + 6, 14, 3, (SDL_Color){ 168, 124, 72, 255 } );
    ui_text( ui, ui->normal, x + 28, cy - TTF_FontHeight( ui->normal ) / 2, "CorkDeck", ui->value );
    x += 28 + ui_text_width( ui, ui->normal, "CorkDeck" ) + 22;
    if (crumb)
    {
        ui_text( ui, ui->small, x, cy - TTF_FontHeight( ui->small ) / 2, crumb, ui->dim );
        x += ui_text_width( ui, ui->small, crumb ) + 10;
    }
    if (detail) sh_fit( ui, ui->small, x, cy - TTF_FontHeight( ui->small ) / 2, 420, detail, ui->dim, 0 );

    status = sh_status( &hour, &minute, &battery, &charging );
    if (status & LAUNCHER_STATUS_CLOCK)
    {
        snprintf( text, sizeof(text), "%02d:%02d", hour, minute );
        ui_text_right( ui, ui->small, right, cy - TTF_FontHeight( ui->small ) / 2, text, ui->dim );
        right -= ui_text_width( ui, ui->small, text ) + 18;
    }
    if (status & LAUNCHER_STATUS_BATTERY)
    {
        snprintf( text, sizeof(text), "%d%%", battery );
        ui_text_right( ui, ui->small, right, cy - TTF_FontHeight( ui->small ) / 2, text, ui->dim );
        right -= ui_text_width( ui, ui->small, text ) + 8;
        draw_battery( ui, right - 30, cy, battery, charging );
        right -= 48;
    }
    if (sh_download( &d ) && d.total)
    {
        int percent = (int)(d.downloaded * 100 / d.total), w = 90;

        ui_rounded( ui, right - w, cy - 3, w, 6, 3, SH_LINE );
        ui_rounded( ui, right - w, cy - 3, w * percent / 100 > 6 ? w * percent / 100 : 6, 6, 3, SH_BLUE );
        right -= w + 10;
        snprintf( text, sizeof(text), "%s · %d%%", sh_owned_name( d.app_id ), percent );
        ui_text_right( ui, ui->small, right, cy - TTF_FontHeight( ui->small ) / 2, text, SH_BLUE );
    }
}

static void sh_frame_begin( struct launcher *l )
{
    struct ui *ui = &l->ui;

    sh.hit_count = 0;
    sh.transfer_known = 0;
    ui_background( ui );
}

/* A button: 0 plain, 1 the green one that starts a program, 2 an outline. */
static int sh_button( struct ui *ui, int x, int y, const char *label, int kind, int focused, int code )
{
    TTF_Font *font = kind == 1 ? ui->normal : ui->small;
    int w = ui_text_width( ui, font, label ) + (kind == 1 ? 96 : 44), h = kind == 1 ? 58 : 48;

    if (kind == 1)
    {
        ui_rounded( ui, x, y, w, h, 9, SH_GREEN_D );
        ui_rounded( ui, x, y, w, h - 4, 9, SH_GREEN );
        ui_text( ui, font, x + 62, y + (h - 4 - TTF_FontHeight( font )) / 2, label, (SDL_Color){ 6, 32, 10, 255 } );
        {
            SDL_Vertex v[3] = { { { x + 34, y + 17 }, { 6, 32, 10, 255 }, { 0, 0 } },
                                { { x + 34, y + 37 }, { 6, 32, 10, 255 }, { 0, 0 } },
                                { { x + 50, y + 27 }, { 6, 32, 10, 255 }, { 0, 0 } } };
            SDL_RenderGeometry( ui->renderer, NULL, v, 3, NULL, 0 );
        }
    }
    else
    {
        if (kind == 2) ui_outline( ui, x, y, w, h, 9, 2, SH_LINE );
        else ui_rounded( ui, x, y, w, h, 9, SH_PANEL2 );
        ui_text( ui, font, x + 22, y + (h - TTF_FontHeight( font )) / 2, label, kind == 2 ? ui->dim : ui->text );
    }
    if (focused) sh_focus_ring( ui, x, y, w, h, 9 );
    sh_hit( x, y, w, h, code );
    return w;
}

static int sh_chip( struct ui *ui, int x, int y, const char *text, SDL_Color color )
{
    int w = ui_text_width( ui, ui->small, text ) + 24, h = TTF_FontHeight( ui->small ) + 8;

    ui_rounded( ui, x, y, w, h, h / 2, SH_PANEL2 );
    ui_text( ui, ui->small, x + 12, y + 4, text, color );
    return w + 8;
}

static void sh_tag( struct ui *ui, int x, int y, int max, const char *text, SDL_Color color )
{
    int w = ui_text_width( ui, ui->small, text ) + 14, h = TTF_FontHeight( ui->small ) + 4;

    if (w > max) w = max;
    ui_rounded( ui, x, y, w, h, 6, (SDL_Color){ 10, 14, 20, 215 } );
    sh_fit( ui, ui->small, x + 7, y + 2, w - 12, text, color, 0 );
}

/* A cover that has no picture: a tint taken from the name, and the name. */
static void sh_blank_cover( struct ui *ui, const char *title, SDL_Rect r, int dimmed )
{
    static const SDL_Color tints[] = { { 91, 108, 138, 255 }, { 154, 75, 58, 255 }, { 63, 127, 115, 255 },
                                       { 122, 95, 168, 255 }, { 176, 138, 60, 255 }, { 70, 104, 156, 255 },
                                       { 138, 143, 153, 255 }, { 163, 79, 122, 255 } };
    unsigned int hash = 0;
    const char *c;
    SDL_Color top, bottom;

    for (c = title; *c; c++) hash = hash * 31 + (unsigned char)*c;
    top = tints[hash % 8];
    if (dimmed) top = (SDL_Color){ top.r / 2 + 20, top.g / 2 + 22, top.b / 2 + 26, 255 };
    bottom = (SDL_Color){ top.r / 4, top.g / 4, top.b / 4, 255 };
    ui_rounded( ui, r.x, r.y, r.w, r.h, 10, top );
    ui_gradient( ui, r.x, r.y + r.h / 3, r.w, r.h - r.h / 3, top, bottom, 0 );
    sh_title( ui, r.x + 10, r.y + r.h - 10, r.w - 20, title, ui->value );
}

static void sh_card( struct launcher *l, int item, SDL_Rect r, int focused, int code )
{
    struct ui *ui = &l->ui;

    if (item < 0)
    {
        const struct steam_game *game = &sh.owned[-1 - item];

        sh_blank_cover( ui, game->name, r, 1 );
        sh_tag( ui, r.x + 8, r.y + 8, r.w - 16, "NOT INSTALLED", ui->dim );
    }
    else
    {
        struct program *p = &l->programs[item];
        struct steam_download_view d;

        request_art( l, item, ART_PORTRAIT );
        if (p->icon && p->icon_is_art) draw_cover( ui, p, r, 10, p->missing ? 110 : 255, 255 );
        else sh_blank_cover( ui, p->title, r, p->missing );
        if (p->missing) sh_tag( ui, r.x + 8, r.y + 8, r.w - 16, usb_program_path( p->path ) ? "USB REMOVED" : "FILE MISSING", SH_AMBER );
        else if (sh_is_steam( l, item ))
        {
            if (sh_download( &d ) && d.total && strstr( p->path, d.executable[0] ? d.executable : "\n" ))
                sh_tag( ui, r.x + 8, r.y + 8, r.w - 16, "UPDATING", SH_BLUE );
            else sh_tag( ui, r.x + 8, r.y + 8, r.w - 16, "STEAM", SH_GREEN );
        }
        else if (usb_program_path( p->path )) sh_tag( ui, r.x + 8, r.y + 8, r.w - 16, "USB", ui->dim );
        if (p->favorite) ui_fill_circle( ui, r.x + r.w - 16, r.y + 16, 6, SH_AMBER );
    }
    if (focused) sh_focus_ring( ui, r.x, r.y, r.w, r.h, 10 );
    sh_hit( r.x, r.y, r.w, r.h, code );
}

static const char *sh_item_title( struct launcher *l, int item )
{
    return item < 0 ? sh.owned[-1 - item].name : l->programs[item].title;
}

/***********************************************************************
 * The library's list
 */

static struct launcher *sh_sort_launcher;

static int sh_compare( const void *a, const void *b )
{
    int x = *(const int *)a, y = *(const int *)b;
    struct launcher *l = sh_sort_launcher;

    if (x >= 0 && y >= 0)
    {
        const struct program *p = &l->programs[x], *q = &l->programs[y];

        if (sh.sort == SORT_RECENT && p->launched_order != q->launched_order)
            return p->launched_order < q->launched_order ? 1 : -1;
        if (sh.sort == SORT_ADDED && p->added_order != q->added_order) return p->added_order < q->added_order ? 1 : -1;
        if (sh.sort == SORT_TIME && sh_record( l, x )->seconds != sh_record( l, y )->seconds)
            return sh_record( l, x )->seconds < sh_record( l, y )->seconds ? 1 : -1;
    }
    else if ((x < 0) != (y < 0)) return x < 0 ? 1 : -1;   /* what can be started comes first */
    return strcasecmp( sh_item_title( l, x ), sh_item_title( l, y ) );
}

static void sh_read_owned( struct launcher *l, int force )
{
    struct steam_game *page;
    size_t count, i;

    if (!force && sh.owned_read && SDL_GetTicks() - sh.owned_read < 15000) return;
    sh.owned_read = SDL_GetTicks() | 1;
    sh.owned_count = 0;
    if (!(page = malloc( sizeof(*page) * SH_OWNED ))) return;
    count = steam_ui_owned( l->options->runtime_dir, page, SH_OWNED );
    for (i = 0; i < count && sh.owned_count < SH_OWNED; i++)
        if (!page[i].installed) sh.owned[sh.owned_count++] = page[i];
    free( page );
}

static void sh_rebuild( struct launcher *l, int keep )
{
    int i;

    sh.count = 0;
    for (i = 0; i < l->program_count; i++)
    {
        const struct program *p = &l->programs[i];

        if (p->removed || (l->hide_missing && p->missing) || (p->settings.hidden && !l->show_hidden) ||
            !contains_case( p->title, l->search )) continue;
        if (sh.tab == TAB_FAVORITES && !p->favorite) continue;
        if (sh.tab == TAB_INSTALLED && p->missing) continue;
        if (sh.tab == TAB_AVAILABLE) continue;
        if (sh.tab == TAB_LOCAL && sh_is_steam( l, i )) continue;
        if (sh.tab == TAB_CATEGORY && (!sh.category[0] || strcasecmp( sh_record( l, i )->category, sh.category ))) continue;
        sh.items[sh.count++] = i;
    }
    /* The account is reached when its games are asked for, not at every start:
     * the shell keeps off the network and the card until something needs them. */
    if (sh.tab == TAB_AVAILABLE && !sh.steam_asked)
    {
        sh.steam_asked = 1;
        launcher_log( "[SHELL] the Not installed tab: signing in with the saved Steam session, if there is one" );
        steam_ui_autoresume( l->options->runtime_dir );
    }
    if (sh.tab == TAB_ALL || sh.tab == TAB_AVAILABLE)
    {
        /* Every installation is looked at before the account's games are
         * listed, so that none of them is listed as still to install. */
        for (i = 0; i < l->program_count; i++) if (!l->programs[i].removed) sh_is_steam( l, i );
        sh_read_owned( l, 0 );
        for (i = 0; i < sh.owned_count && sh.count < SH_ITEMS; i++)
            if (contains_case( sh.owned[i].name, l->search )) sh.items[sh.count++] = -1 - i;
    }
    sh_sort_launcher = l;
    qsort( sh.items, sh.count, sizeof(sh.items[0]), sh_compare );
    for (i = 0; i < sh.count; i++) if (sh.items[i] == keep && keep >= 0) sh.selection = i;
    if (sh.selection >= sh.count) sh.selection = sh.count ? sh.count - 1 : 0;
    if (!sh.count && sh.zone) sh.zone = 0;
}

/***********************************************************************
 * A page of rows: the pages that are lists by nature
 */

struct sh_row { char label[96], value[160]; const char *help; int disabled, tone; };

static int sh_rows( struct launcher *l, const char *title, const char *context, struct sh_row *rows, int count,
                    int *selection )
{
    struct ui_row list_rows[24];
    struct ui_list list = { .selection = *selection };
    enum ui_action action;
    int i;

    if (sh.pending >= 0) return -1;
    if (count > 24) count = 24;
    memset( list_rows, 0, sizeof(list_rows) );
    for (i = 0; i < count; i++)
    {
        snprintf( list_rows[i].label, sizeof(list_rows[i].label), "%s", sh_tr( rows[i].label ) );
        snprintf( list_rows[i].value, sizeof(list_rows[i].value), "%s", sh_tr( rows[i].value ) );
        list_rows[i].help = rows[i].help;
        list_rows[i].disabled = rows[i].disabled;
        list_rows[i].value_tone = rows[i].tone;
    }
    action = ui_list_run( &l->ui, &list, sh_tr( title ), context ? sh_tr( context ) : NULL, list_rows, count, 0 );
    *selection = list.selection;
    ui_start_screen( &l->ui );
    if (sh.pending >= 0) return -1;
    return action == UI_ACTION_CHOOSE ? list.selection : -1;
}

/***********************************************************************
 * Settings: one screen for a program's own and for the general ones
 */

/* A name a person can read for a translator option: its own is an environment variable. */
static const char *sh_fex_title( const struct nx_fex_option *option )
{
    static const char *const titles[] =
    {
        [NX_FEX_TSO] = "FEX: memory write order (TSO)", [NX_FEX_MULTIBLOCK] = "FEX: compile blocks together",
        [NX_FEX_MAXINST] = "FEX: instructions in a block", [NX_FEX_SMC] = "FEX: self-modifying code checks",
        [NX_FEX_DISABLE_L2] = "FEX: second lookup cache", [NX_FEX_DYNAMIC_L1] = "FEX: resizable first lookup cache",
        [NX_FEX_X87_REDUCED] = "FEX: reduced x87 precision",
    };

    return option->id < (int)(sizeof(titles) / sizeof(titles[0])) && titles[option->id] ? titles[option->id] : option->name;
}

struct sh_settings
{
    struct launcher *l;
    struct program *p;                     /* NULL: the general settings */
    struct launcher_kv profile;
    char profile_path[768];
    struct app_settings resolved;
    int ids[APP_SET_COUNT + 8 + NX_FEX_OPTION_COUNT], count;   /* the rows of the group shown */
    int group, row, top, in_rows;
};

/* Rows that are not settings of the model: actions, and from SH_FEX the
 * translator's own options (fex_options.h), kept in the same file. */
enum { SH_ACT_FEX = APP_SET_COUNT, SH_ACT_KEYS, SH_ACT_TITLE, SH_ACT_ARGS, SH_FEX };

static int sh_settings_has( const struct sh_settings *s, const struct app_setting_def *def )
{
    /* Without developer mode its switch is all there is of diagnostics. */
    if (def->group == APP_GROUP_DIAGNOSTICS && def->id != APP_SET_DEVELOPER && !sh.developer) return 0;
    if (!s->p) return def->global_key != NULL;
    if (!def->profile_key) return 0;
    if (def->id == APP_SET_DIAGNOSTICS_MODE) return 0;
    if (def->id == APP_SET_VKD3D_VERSION) return s->resolved.values[APP_SET_RENDERER].effective == 2;
    if (def->id == APP_SET_DXVK_VERSION || def->id == APP_SET_DXVK_SOURCE || def->id == APP_SET_DXVK_HUD)
        return s->resolved.values[APP_SET_RENDERER].effective != 0;
    if (def->id == APP_SET_SHARPNESS) return s->resolved.values[APP_SET_UPSCALING].effective == 1;
    if (def->id == APP_SET_TRANSLATOR) return s->p->machine == 0x014c || s->p->machine == 0x8664;
    return 1;
}

static void sh_settings_resolve( struct sh_settings *s )
{
    struct launcher *l = s->l;
    struct app_settings_limits limits = { 0, 1, 1, 1, 1, 0 };
    char path[768];
    int id, fex_first;

    if (s->p)
    {
        limits.machine = s->p->machine;
        limits.vulkan = l->options->vulkan;
        limits.four_cores_available = l->options->four_cores_available;
        runtime_file( l, "lsfg/Lossless.dll", path, sizeof(path) );
        limits.lsfg_installed = file_exists( path );
        limits.has_own_keys = launcher_keys_path( s->p->path, path, sizeof(path) ) && file_exists( path );
        if (!launcher_program_settings_path( l->options->runtime_dir, s->p->path, s->profile_path,
                                             sizeof(s->profile_path) ) ||
            !launcher_kv_load( &s->profile, s->profile_path ))
        {
            s->profile.size = 0;
            s->profile.text[0] = 0;
        }
    }
    app_settings_resolve( &s->resolved, &sh.global, s->p ? &s->profile : NULL, &limits, NULL, NULL );
    s->count = 0;
    /* First in its group: of everything here, switching the x86 memory order
     * off has made the largest difference to speed on this console. */
    fex_first = s->p && s->group == APP_GROUP_TRANSLATION && (s->p->machine == 0x014c || s->p->machine == 0x8664) &&
                s->resolved.values[APP_SET_TRANSLATOR].effective == 1;
    if (fex_first) s->ids[s->count++] = SH_FEX + NX_FEX_TSO;
    for (id = 0; id < APP_SET_COUNT; id++)
    {
        const struct app_setting_def *def = app_setting_def( id );

        if ((int)def->group == s->group && sh_settings_has( s, def )) s->ids[s->count++] = id;
    }
    if (s->p && s->group == APP_GROUP_TRANSLATION && (s->p->machine == 0x014c || s->p->machine == 0x8664))
    {
        /* The translator's everyday options stand in the list; the rest are behind one row. */
        if (s->resolved.values[APP_SET_TRANSLATOR].effective == 1)
            for (id = 0; id < NX_FEX_OPTION_COUNT; id++)
                if (!nx_fex_options[id].advanced && id != NX_FEX_TSO) s->ids[s->count++] = SH_FEX + id;
        s->ids[s->count++] = SH_ACT_FEX;
    }
    if (s->p && s->group == APP_GROUP_INPUT) s->ids[s->count++] = SH_ACT_KEYS;
    if (s->p && s->group == APP_GROUP_SHELL)
    {
        s->ids[s->count++] = SH_ACT_TITLE;
        s->ids[s->count++] = SH_ACT_ARGS;
    }
    if (s->row >= s->count) s->row = s->count ? s->count - 1 : 0;
}

static void sh_settings_store( struct sh_settings *s, int id, int choice, const char *text )
{
    struct launcher *l = s->l;

    if (s->p)
    {
        char folder[768];

        if (launcher_settings_on_usb( s->p->path ))
        {
            runtime_file( l, "program-settings", folder, sizeof(folder) );
            mkdir( folder, 0777 );
        }
        if (!app_settings_set_profile( &s->profile, id, choice, text ) ||
            !launcher_kv_save( &s->profile, s->profile_path ))
            ui_toast( &l->ui, "Could not save the program's settings", 3000 );
        load_program_settings( l, s->p );
    }
    else
    {
        if (choice < 0) choice = app_setting_def( id )->default_choice;
        if (!app_settings_set_global( &sh.global, id, choice ) || !wine_nx_config_save( &sh.global, sh.global_path ))
            ui_toast( &l->ui, "Could not save the settings", 3000 );
        sh_apply_global( l );
    }
}

static void sh_settings_change( struct sh_settings *s, int id )
{
    struct launcher *l = s->l;
    struct ui *ui = &l->ui;
    const struct app_setting_def *def;
    const char *items[APP_SETTING_CHOICES + 1];
    char first[96], path[768], under[512];
    int count = 0, chosen, i, current;

    if (id >= SH_FEX)
    {
        const struct nx_fex_option *option = &nx_fex_options[id - SH_FEX];
        char value[64];

        snprintf( first, sizeof(first), "%s", sh_tr( "Default" ) );
        items[count++] = first;
        for (i = 0; i < option->value_count && count <= APP_SETTING_CHOICES; i++) items[count++] = option->value_names[i];
        current = launcher_kv_get( &s->profile, option->name, value, sizeof(value) ) ? nx_fex_option_choice( option, value ) + 1 : 0;
        chosen = ui_menu( ui, sh_fex_title( option ), items, count, current < 0 ? 0 : current );
        ui_start_screen( ui );
        if (chosen < 0) return;
        if (!launcher_kv_set( &s->profile, option->name, chosen ? option->values[chosen - 1] : NULL ) ||
            !launcher_kv_save( &s->profile, s->profile_path ))
            ui_toast( ui, "Could not save the program's settings", 3000 );
        return;
    }
    if (id == SH_ACT_FEX)
    {
        if (s->resolved.values[APP_SET_TRANSLATOR].effective == 1) fex_options_menu( l, s->p );
        else box64_options_menu( l, s->p );
        ui_start_screen( ui );
        return;
    }
    if (id == SH_ACT_KEYS)
    {
        if (launcher_keys_path( s->p->path, path, sizeof(path) ))
        {
            shared_keys( l, under, sizeof(under) );
            controls_screen( l, path, under, "Controls" );
        }
        ui_start_screen( ui );
        return;
    }
    if (id == SH_ACT_TITLE)
    {
        edit_text( sh_tr( "Title" ), s->p->settings.title, sizeof(s->p->settings.title) );
        save_program_settings( l, s->p );
        save_library( l );
        return;
    }
    if (id == SH_ACT_ARGS)
    {
        char line[896] = "", edited[896];

        if (!launcher_args_path( s->p->path, path, sizeof(path) )) return;
        read_line( path, line, sizeof(line) );
        if (!launcher_platform_prompt( sh_tr( "Arguments" ), line, edited, sizeof(edited) )) return;
        if (edited[0]) write_line( path, edited );
        else remove( path );
        return;
    }
    def = app_setting_def( id );
    if (def->type == APP_TYPE_TEXT)
    {
        struct ui_list anchor = {0};
        enum dxvk_source source = (enum dxvk_source)s->resolved.values[APP_SET_DXVK_SOURCE].effective;
        char version[32];

        snprintf( version, sizeof(version), "%s", s->resolved.values[id].chosen_text );
        if (launcher_graphics_select( l->graphics, &anchor, s->p->machine, id == APP_SET_VKD3D_VERSION,
                                      s->resolved.values[APP_SET_RENDERER].effective == 2, &source, version ))
        {
            sh_settings_store( s, id, 0, version );
            if (id == APP_SET_DXVK_VERSION) sh_settings_store( s, APP_SET_DXVK_SOURCE, source, NULL );
        }
        ui_start_screen( ui );
        return;
    }
    if (s->p)
    {
        snprintf( first, sizeof(first), "%s", sh_tr( def->global_key ? "As in general settings" : "Default" ) );
        items[count++] = first;
    }
    for (i = 0; i < def->choice_count; i++) items[count++] = def->choices[i].label[sh.language];
    current = s->p ? s->resolved.values[id].layer[APP_LAYER_PROFILE] + 1
                   : s->resolved.values[id].layer[APP_LAYER_GLOBAL] >= 0 ? s->resolved.values[id].layer[APP_LAYER_GLOBAL]
                                                                           : def->default_choice;
    chosen = ui_menu( ui, def->name[sh.language], items, count, current < 0 ? 0 : current );
    ui_start_screen( ui );
    if (chosen < 0) return;
    sh_settings_store( s, id, s->p ? chosen - 1 : chosen, NULL );
    /* Asking for DXVK means having one: the same check the launch makes. */
    if (s->p && id == APP_SET_RENDERER && chosen - 1 > 0 && !prepare_program_graphics( l, s->p ))
        sh_settings_store( s, id, -1, NULL );
    if (s->p) save_program_settings( l, s->p );
}

static void sh_settings_draw( struct sh_settings *s )
{
    struct launcher *l = s->l;
    struct ui *ui = &l->ui;
    const int nav_x = SH_MARGIN, nav_w = 268, rows_x = 348, rows_w = 516, help_x = 884, help_w = 332;
    const int row_h = 66, visible = 8;
    char line[320];
    int i, y;
    static const struct ui_hint hints[] = { { UI_Y, "Reset" }, { UI_B, "Back" }, { UI_A, "Change" } };

    sh_frame_begin( l );
    sh_top( l, s->p ? s->p->title : sh_tr( "Settings" ), s->p ? sh_tr( "Settings" ) : NULL );
    for (i = 0, y = SH_TOP + 24; i < APP_GROUP_COUNT; i++, y += 50)
    {
        int on = i == s->group;

        if (on) ui_rounded( ui, nav_x, y, nav_w, 44, 8, SH_PANEL2 );
        sh_fit( ui, ui->small, nav_x + 16, y + (44 - TTF_FontHeight( ui->small )) / 2, nav_w - 28,
                     app_setting_group_name( i, sh.language ), on ? ui->value : ui->dim, 0 );
        if (on && !s->in_rows) sh_focus_ring( ui, nav_x, y, nav_w, 44, 8 );
        sh_hit( nav_x, y, nav_w, 44, 1000 + i );
    }
    if (s->row < s->top) s->top = s->row;
    if (s->row >= s->top + visible) s->top = s->row - visible + 1;
    for (i = s->top, y = SH_TOP + 24; i < s->count && i < s->top + visible; i++, y += row_h + 6)
    {
        int id = s->ids[i], focused = s->in_rows && i == s->row;
        const char *label, *value = "", *source = "";
        SDL_Color source_color = ui->dim;

        ui_rounded( ui, rows_x, y, rows_w, row_h, 8, focused ? SH_PANEL2 : SH_PANEL );
        if (id >= SH_FEX)
        {
            const struct nx_fex_option *option = &nx_fex_options[id - SH_FEX];
            char fex_value[64];
            int choice = -1;

            label = sh_fex_title( option );
            if (launcher_kv_get( &s->profile, option->name, fex_value, sizeof(fex_value) ))
                choice = nx_fex_option_choice( option, fex_value );
            value = option->value_names[choice >= 0 ? choice : option->default_choice];
            source = app_setting_layer_name( choice >= 0 ? APP_LAYER_PROFILE : APP_LAYER_DEFAULT, sh.language );
            if (choice >= 0) source_color = SH_BLUE;
        }
        else if (id >= APP_SET_COUNT)
        {
            label = id == SH_ACT_FEX ? (s->resolved.values[APP_SET_TRANSLATOR].effective == 1 ? "More FEX options" : "Box64 options")
                    : id == SH_ACT_KEYS ? "Edit this application's keys" : id == SH_ACT_TITLE ? "Title" : "Arguments";
            if (id == SH_ACT_TITLE) value = s->p->title;
            source = id == SH_ACT_FEX ? "Fine tuning of the translator for this application" : "";
        }
        else
        {
            const struct app_setting_value *v = &s->resolved.values[id];

            label = app_setting_def( id )->name[sh.language];
            value = app_setting_value_label( &s->resolved, id, sh.language );
            if (!value[0]) value = sh_tr( "Newest installed" );
            source = app_setting_layer_name( v->source, sh.language );
            if (v->source == APP_LAYER_PROFILE || v->source == APP_LAYER_GLOBAL) source_color = SH_BLUE;
            if (v->source >= APP_LAYER_LIMIT) source_color = SH_AMBER;
        }
        {
            /* A short value stands at the right of the name. A long one takes
             * the second line, with where it came from after it. */
            int value_w = ui_text_width( ui, ui->small, value ), room = rows_w - 36, line2 = y + 9 + TTF_FontHeight( ui->small ) + 2;

            if (value_w <= room * 2 / 5)
            {
                sh_fit( ui, ui->small, rows_x + 18, y + 9, room - value_w - 14, label, ui->text, focused );
                ui_text_right( ui, ui->small, rows_x + rows_w - 18, y + 9, value, ui->value );
                sh_fit( ui, ui->small, rows_x + 18, line2, room, source, source_color, 0 );
            }
            else
            {
                int source_w = ui_text_width( ui, ui->small, source ) + 16;

                if (source_w > room / 2) source_w = room / 2;
                sh_fit( ui, ui->small, rows_x + 18, y + 9, room, label, ui->text, focused );
                sh_fit( ui, ui->small, rows_x + 18, line2, room - source_w, value, ui->value, focused );
                sh_fit( ui, ui->small, rows_x + rows_w - 18 - source_w + 16, line2, source_w - 16, source, source_color, 0 );
            }
        }
        if (focused) sh_focus_ring( ui, rows_x, y, rows_w, row_h, 8 );
        sh_hit( rows_x, y, rows_w, row_h, 2000 + i );
    }
    if (!s->count)
        ui_text( ui, ui->small, rows_x, SH_TOP + 36, "Nothing to set here for this application.", ui->dim );

    /* What the setting in focus is for, and what it may cost. */
    if (!s->in_rows || !s->count || s->ids[s->row] >= APP_SET_COUNT)
    {
        static const char *const about[APP_GROUP_COUNT] =
        {
            "How the picture is made: the renderer, its version, the frame rate, scaling and frame generation.",
            "How the program's processor instructions are run on the console, and how its threads are placed on the cores.",
            "What the controller sends to the program, and the on-screen keyboard.",
            "What is recorded while a program runs. Observation does not change how it runs; the rest does, and says so.",
            "Fallbacks and comparisons. Nothing here is needed for ordinary use.",
            "The interface itself: language, animations, sounds and what happens when a program closes.",
        };
        const char *title = app_setting_group_name( s->group, sh.language ), *body = about[s->group];
        int ty = SH_TOP + 40;

        if (s->in_rows && s->count && s->ids[s->row] >= SH_FEX)
        {
            title = sh_fex_title( &nx_fex_options[s->ids[s->row] - SH_FEX] );
            body = nx_fex_options[s->ids[s->row] - SH_FEX].help;
        }
        else if (s->in_rows && s->count)
        {
            int id = s->ids[s->row];

            title = id == SH_ACT_FEX ? "Translator options" : id == SH_ACT_KEYS ? "Edit this application's keys"
                    : id == SH_ACT_TITLE ? "Title" : "Arguments";
            body = id == SH_ACT_FEX ? "Every option of the translator chosen above, each with its own description. They are kept for this application only."
                   : id == SH_ACT_KEYS ? "Which keyboard keys this application's controls send, over the shared mapping."
                   : id == SH_ACT_TITLE ? "The name shown in the library. An empty name goes back to the one in the program's own file."
                   : "What is put after the program's name on its command line. An empty line removes them.";
        }
        ui_rounded( ui, help_x, SH_TOP + 24, help_w, 560, 10, SH_PANEL );
        ty += ui_text_wrapped( ui, ui->normal, help_x + 18, ty, help_w - 36, 3, title, ui->value, 0 ) * (TTF_FontHeight( ui->normal ) + 4) + 14;
        ui_text_wrapped( ui, ui->small, help_x + 18, ty, help_w - 36, 12, body, ui->dim, 0 );
    }
    else
    {
        const struct app_setting_def *def = app_setting_def( s->ids[s->row] );
        const struct app_setting_value *v = &s->resolved.values[s->ids[s->row]];
        int ty = SH_TOP + 40;

        ui_rounded( ui, help_x, SH_TOP + 24, help_w, 560, 10, SH_PANEL );
        ty += ui_text_wrapped( ui, ui->normal, help_x + 18, ty, help_w - 36, 3, def->name[sh.language], ui->value, 0 ) * (TTF_FontHeight( ui->normal ) + 4) + 14;
        ui_text( ui, ui->small, help_x + 18, ty, "PURPOSE", ui->text );
        ty += TTF_FontHeight( ui->small ) + 4;
        ty += ui_text_wrapped( ui, ui->small, help_x + 18, ty, help_w - 36, 8, def->purpose[sh.language], ui->dim, 0 ) * (TTF_FontHeight( ui->small ) + 4) + 14;
        if (def->impact[sh.language])
        {
            ui_text( ui, ui->small, help_x + 18, ty, "POSSIBLE EFFECT", ui->text );
            ty += TTF_FontHeight( ui->small ) + 4;
            ty += ui_text_wrapped( ui, ui->small, help_x + 18, ty, help_w - 36, 6, def->impact[sh.language], ui->dim, 0 ) * (TTF_FontHeight( ui->small ) + 4) + 14;
        }
        if (v->reason)
        {
            snprintf( line, sizeof(line), "%s", app_setting_reason_text( v->reason, sh.language ) );
            ui_text_wrapped( ui, ui->small, help_x + 18, ty, help_w - 36, 5, line, SH_AMBER, 0 );
        }
    }
    ui_hints_right( ui, hints, 3, ui->width - SH_MARGIN, UI_FOOTER_Y - 6 );
    ui_draw_toast( ui );
    ui_present( ui );
}

static void sh_settings_screen( struct launcher *l, struct program *p )
{
    struct sh_settings s = { .l = l, .p = p, .group = p ? APP_GROUP_GRAPHICS : APP_GROUP_SHELL };
    struct ui *ui = &l->ui;
    struct ui_input input;

    sh_settings_resolve( &s );
    ui_start_screen( ui );
    while (ui_begin_frame( ui ))
    {
        while (ui_poll( ui, &input ))
        {
            int before = s.group;

            if (input.touch == UI_TOUCH_TAP)
            {
                int code = sh_hit_at( input.x, input.y );

                if (code >= 2000) { s.in_rows = 1; if (s.row == code - 2000) input.button = UI_A; else s.row = code - 2000; }
                else if (code >= 1000) { s.group = code - 1000; s.in_rows = 0; }
            }
            else if (input.touch == UI_TOUCH_SCROLL_UP && s.row + 1 < s.count) s.row++, s.in_rows = 1;
            else if (input.touch == UI_TOUCH_SCROLL_DOWN && s.row > 0) s.row--, s.in_rows = 1;
            switch (input.button)
            {
            case UI_UP:
                if (s.in_rows) { if (s.row > 0) s.row--; }
                else if (s.group > 0) s.group--;
                break;
            case UI_DOWN:
                if (s.in_rows) { if (s.row + 1 < s.count) s.row++; }
                else if (s.group + 1 < APP_GROUP_COUNT) s.group++;
                break;
            case UI_RIGHT: if (s.count) s.in_rows = 1; break;
            case UI_LEFT: s.in_rows = 0; break;
            case UI_L: if (s.group > 0) s.group--; break;
            case UI_R: if (s.group + 1 < APP_GROUP_COUNT) s.group++; break;
            case UI_A:
                ui_sound( ui, LAUNCHER_SOUND_ACCEPT );
                if (!s.in_rows) { if (s.count) s.in_rows = 1; }
                else if (s.count) sh_settings_change( &s, s.ids[s.row] );
                sh_settings_resolve( &s );
                break;
            case UI_Y:
                if (s.in_rows && s.count && s.ids[s.row] >= SH_FEX)
                {
                    launcher_kv_set( &s.profile, nx_fex_options[s.ids[s.row] - SH_FEX].name, NULL );
                    launcher_kv_save( &s.profile, s.profile_path );
                    sh_settings_resolve( &s );
                    ui_toast( ui, "Back to the default", 1500 );
                }
                else if (s.in_rows && s.count && s.ids[s.row] < APP_SET_COUNT)
                {
                    sh_settings_store( &s, s.ids[s.row], -1, NULL );
                    if (p) save_program_settings( l, p );
                    sh_settings_resolve( &s );
                    ui_toast( ui, "Back to the default", 1500 );
                }
                break;
            case UI_B:
                ui_sound( ui, LAUNCHER_SOUND_BACK );
                if (s.in_rows) s.in_rows = 0;
                else { ui_start_screen( ui ); return; }
                break;
            case UI_MINUS:
            case UI_PLUS:
                sh_global_key( l, input.button );
                break;
            }
            if (sh.pending >= 0) { ui_start_screen( ui ); return; }
            if (before != s.group) { s.row = s.top = 0; sh_settings_resolve( &s ); }
            if (!ui->running) return;
        }
        if (!ui->running) return;
        sh_settings_draw( &s );
        ui_wait( ui );
    }
}

/***********************************************************************
 * A program's page
 */

static void sh_sessions( struct launcher *l, struct program *p )
{
    struct app_session sessions[20];
    struct sh_row rows[20];
    char date[40], length[48];
    int count, i, selection = 0;

    count = p->catalog_id ? app_record_sessions( l->options->runtime_dir, p->catalog_id, sessions, 20 ) : 0;
    if (!count)
    {
        ui_message( &l->ui, sh_tr( "Sessions" ), sh_tr( "Nothing has been recorded for this application yet. "
                    "A session is written down when the application closes." ) );
        return;
    }
    for (;;)
    {
        memset( rows, 0, sizeof(rows) );
        for (i = 0; i < count; i++)
        {
            snprintf( rows[i].label, sizeof(rows[i].label), "%s", sh_date( sessions[i].start, date, sizeof(date) ) );
            snprintf( rows[i].value, sizeof(rows[i].value), "%s", sh_duration( sessions[i].seconds, length, sizeof(length) ) );
        }
        if ((i = sh_rows( l, "Sessions", p->title, rows, count, &selection )) < 0) return;
        {
            char text[512], frames[64];

            if (sessions[i].frames) snprintf( frames, sizeof(frames), "%u", sessions[i].frames );
            else snprintf( frames, sizeof(frames), "%s", sh_tr( "no data" ) );
            snprintf( text, sizeof(text), sh_tr( "Started: %s\nLength: %s\nFrames presented: %s\nExit code: 0x%x\nHow it ended: %s\nBuild: %s" ),
                      sh_date( sessions[i].start, date, sizeof(date) ),
                      sh_duration( sessions[i].seconds, length, sizeof(length) ), frames, sessions[i].exit_code,
                      sessions[i].note[0] ? sessions[i].note : sh_tr( "no data" ), sessions[i].build );
            ui_message( &l->ui, sh_tr( "Session" ), text );
            ui_start_screen( &l->ui );
        }
    }
}

/* The other programs of the same installation: the folder the program is in
 * and one level below it. */
static void sh_launch_option( struct launcher *l, struct program *p )
{
    static char found[24][512];
    const char *items[24];
    char dir[512], sub[640];
    DIR *d, *inner;
    struct dirent *e, *f;
    int count = 0, chosen, current = 0, i;

    snprintf( dir, sizeof(dir), "%s", p->path );
    parent_dir( dir );
    if ((d = opendir( dir )))
    {
        while ((e = readdir( d )) && count < 24)
        {
            size_t n = strlen( e->d_name );

            if (e->d_name[0] == '.') continue;
            snprintf( sub, sizeof(sub), "%s/%s", dir, e->d_name );
            if (n > 4 && !strcasecmp( e->d_name + n - 4, ".exe" )) snprintf( found[count++], sizeof(found[0]), "%s", sub );
            else if ((inner = opendir( sub )))
            {
                while ((f = readdir( inner )) && count < 24)
                {
                    n = strlen( f->d_name );
                    if (n > 4 && !strcasecmp( f->d_name + n - 4, ".exe" ))
                        snprintf( found[count++], sizeof(found[0]), "%s/%s", sub, f->d_name );
                }
                closedir( inner );
            }
        }
        closedir( d );
    }
    for (i = 0; i < count; i++)
    {
        items[i] = found[i] + strlen( dir ) + 1;
        if (!strcasecmp( found[i], p->path )) current = i;
    }
    if (count < 2)
    {
        ui_message( &l->ui, sh_tr( "Launch option" ), sh_tr( "This installation has one program to start." ) );
        return;
    }
    chosen = ui_menu( &l->ui, sh_tr( "Launch option" ), items, count, current );
    ui_start_screen( &l->ui );
    if (chosen < 0 || chosen == current) return;
    if (find_program( l, found[chosen] ) >= 0)
    {
        ui_message( &l->ui, sh_tr( "Launch option" ), sh_tr( "That program is already another entry of the library." ) );
        return;
    }
    {
        struct program other;

        if (!describe_program( l, &other, found[chosen] ))
        {
            ui_message( &l->ui, sh_tr( "Launch option" ), sh_tr( "CorkDeck cannot start that program." ) );
            return;
        }
        snprintf( p->path, sizeof(p->path), "%s", other.path );
        snprintf( p->dos, sizeof(p->dos), "%s", other.dos );
        p->machine = other.machine;
        p->missing = 0;
        load_program_settings( l, p );
        save_library( l );
        ui_toast( &l->ui, "Launch option changed", 1800 );
    }
}

/***********************************************************************
 * Deleting a game from the card
 *
 * The folder of a Steam installation is the one that holds the installer's
 * marker; of any other program, the folder its file is in. The files are
 * counted first, so that the question names how much goes, and removed with
 * the marker last: a removal that was cut short is still an installation that
 * can be removed again.
 */

static const char *sh_bytes( unsigned long long bytes, char *out, size_t size );

struct sh_tree
{
    struct ui *ui;
    const char *title;
    unsigned long long bytes, files;   /* counted */
    unsigned long long done_bytes, done_files;
    int erase, error;
    Uint32 shown;
};

static void sh_tree_show( struct sh_tree *t, int force )
{
    char status[160];

    if (!force && SDL_GetTicks() - t->shown < 120) return;
    t->shown = SDL_GetTicks();
    if (t->erase) snprintf( status, sizeof(status), sh_tr( "Deleted %llu of %llu files" ), t->done_files, t->files );
    else snprintf( status, sizeof(status), sh_tr( "Counting the files: %llu" ), t->files );
    ui_progress_update( t->ui, t->title, status, t->erase ? t->done_bytes : 0, t->erase ? (t->bytes ? t->bytes : 1) : 0 );
}

static int sh_tree_walk( struct sh_tree *t, const char *path, int depth )
{
    char child[1024];
    struct dirent *entry;
    struct stat st;
    DIR *dir;
    int ok = 1;

    if (depth > 32) { t->error = ELOOP; return 0; }
    if (lstat( path, &st )) { t->error = errno ? errno : EIO; return 0; }
    if (!S_ISDIR( st.st_mode ))
    {
        if (!t->erase) { t->bytes += (unsigned long long)st.st_size; t->files++; }
        else
        {
            if (unlink( path )) { t->error = errno ? errno : EIO; return 0; }
            t->done_bytes += (unsigned long long)st.st_size;
            t->done_files++;
        }
        sh_tree_show( t, 0 );
        return 1;
    }
    if (!(dir = opendir( path ))) { t->error = errno ? errno : EIO; return 0; }
    for (;;)
    {
        errno = 0;
        if (!(entry = readdir( dir ))) { if (errno) { t->error = errno; ok = 0; } break; }
        if (!strcmp( entry->d_name, "." ) || !strcmp( entry->d_name, ".." )) continue;
        if (t->erase && !depth && !strcmp( entry->d_name, "autorun-steam-install.ini" )) continue;
        if (snprintf( child, sizeof(child), "%s/%s", path, entry->d_name ) >= (int)sizeof(child)) { t->error = ENAMETOOLONG; ok = 0; break; }
        if (!sh_tree_walk( t, child, depth + 1 )) { ok = 0; break; }
    }
    closedir( dir );
    if (ok && t->erase)
    {
        if (!depth)
        {
            snprintf( child, sizeof(child), "%s/autorun-steam-install.ini", path );
            if (!lstat( child, &st ))
            {
                if (unlink( child )) { t->error = errno ? errno : EIO; return 0; }
                t->done_bytes += (unsigned long long)st.st_size;
                t->done_files++;
            }
        }
        if (rmdir( path )) { t->error = errno ? errno : EIO; return 0; }
    }
    return ok;
}

/* The folder of the Steam installation a program belongs to. */
static int sh_steam_folder( struct launcher *l, int index, char *dir, size_t size )
{
    char marker[560];
    int depth;

    snprintf( dir, size, "%s", l->programs[index].path );
    for (depth = 0; depth < 8; depth++)
    {
        parent_dir( dir );
        if (is_root( dir )) break;
        snprintf( marker, sizeof(marker), "%s/autorun-steam-install.ini", dir );
        if (file_exists( marker )) return 1;
    }
    return 0;
}

static int sh_path_inside( const char *path, const char *dir )
{
    size_t len = strlen( dir );

    return !strncasecmp( path, dir, len ) && (!path[len] || path[len] == '/');
}

/* Whether a folder is a program's own and may go whole. When it may not, says why. */
static const char *sh_folder_shared( struct launcher *l, int index, const char *folder, char *other, size_t other_size )
{
    static const char *const kept[] = { "windows", "users", "Program Files", "Program Files (x86)", "ProgramData", "Steam" };
    static const char *const card[] = { "switch", "atmosphere", "bootloader", "Nintendo", "config", "emuMMC",
                                        "Emutendo", "SaltySD", "sept", "warmboot_mariko", "backup", "JKSV", "themes" };
    char path[600];
    unsigned int i;
    int j;

    other[0] = 0;
    if (is_root( folder ) || !strcasecmp( folder, LAUNCHER_DRIVE_C ) || sh_path_inside( l->options->runtime_dir, folder ))
        return "The program's file lies in a folder that is shared, not in a folder of its own.";
    if (sh_path_inside( folder, LAUNCHER_DRIVE_C ))
    {
        for (i = 0; i < sizeof(kept) / sizeof(kept[0]); i++)
        {
            snprintf( path, sizeof(path), "%s/%s", LAUNCHER_DRIVE_C, kept[i] );
            /* Windows and the user profiles not at any depth; the others as themselves. */
            if (i < 2 ? sh_path_inside( folder, path ) : !strcasecmp( folder, path ))
                return "The program's file lies in a folder that is shared, not in a folder of its own.";
        }
    }
    else if (sh_path_inside( folder, l->options->runtime_dir ))
        return "The program's file lies among CorkDeck's own files.";
    else if (!usb_program_path( folder ))
    {
        for (i = 0; i < sizeof(card) / sizeof(card[0]); i++)
        {
            snprintf( path, sizeof(path), "sdmc:/%s", card[i] );
            if (!strcasecmp( folder, path )) return "The program's file lies in a folder that is shared, not in a folder of its own.";
        }
    }
    for (j = 0; j < l->program_count; j++)
    {
        if (j == index || l->programs[j].removed || !sh_path_inside( l->programs[j].path, folder )) continue;
        snprintf( other, other_size, "%s", l->programs[j].title );
        return "Another program of the library lies in the same folder: %s.";
    }
    return NULL;
}

static void sh_delete_from_card( struct launcher *l, int index, int *gone )
{
    struct ui *ui = &l->ui;
    struct program *p = &l->programs[index];
    struct sh_tree tree = { .ui = ui, .title = "Delete from the card" };
    char folder[512], text[900], size[40], other[128], path[600], shown[512];
    const char *why;
    unsigned int app = 0, fs_error = 0;
    int steam = sh_steam_folder( l, index, folder, sizeof(folder) ), counted, erased, i;

    if (steam)
    {
        sh.known[index] &= ~1;
        sh_is_steam( l, index );
        app = sh.app_id[index];
        if (steam_ui_busy( app ))
        {
            ui_message( ui, sh_tr( "Delete from the card" ),
                        sh_tr( "Steam is downloading or removing a game right now. Wait for it to finish, or cancel it on the Downloads page." ) );
            return;
        }
    }
    else
    {
        snprintf( folder, sizeof(folder), "%s", p->path );
        parent_dir( folder );
        if ((why = sh_folder_shared( l, index, folder, other, sizeof(other) )))
        {
            char reason[300];

            snprintf( reason, sizeof(reason), sh_tr( why ), other );
            snprintf( text, sizeof(text), "%s %s", reason,
                      sh_tr( "CorkDeck does not delete such a folder: other things would go with it. Delete the game's files in a file manager, and remove the entry from the library here." ) );
            ui_message( ui, sh_tr( "Delete from the card" ), text );
            return;
        }
    }
    if (!directory_exists( folder ))
    {
        ui_message( ui, sh_tr( "Delete from the card" ),
                    sh_tr( "The program's folder is not on the card any more. Remove the entry from the library instead." ) );
        return;
    }
    launcher_log( "[SHELL] delete from the card: \"%s\" (%s, app %u), folder %s", p->title, steam ? "Steam" : "local", app, folder );
    ui_progress_begin( ui );
    sh_tree_show( &tree, 1 );
    counted = sh_tree_walk( &tree, folder, 0 );
    ui_progress_end( ui );
    if (!counted)
    {
        launcher_log( "[SHELL] delete from the card: counting failed, errno %d", tree.error );
        snprintf( text, sizeof(text), sh_tr( "The folder could not be read (error %d). Nothing was deleted." ), tree.error );
        ui_message( ui, sh_tr( "Delete from the card" ), text );
        return;
    }
    /* As the folder is known in Windows when it is on drive C. */
    if (sh_path_inside( folder, LAUNCHER_DRIVE_C ))
    {
        char *c;

        snprintf( shown, sizeof(shown), "C:%s", folder + strlen( LAUNCHER_DRIVE_C ) );
        for (c = shown; *c; c++) if (*c == '/') *c = '\\';
    }
    else snprintf( shown, sizeof(shown), "%s", folder );
    snprintf( text, sizeof(text),
              sh_tr( steam ? "\"%s\" will be deleted from the card: the folder %s, %s in %llu files, with the record of its installation and its unfinished download. Saves the game keeps in the Windows user profile stay. This cannot be undone."
                           : "\"%s\" will be deleted from the card: the folder %s with everything in it, %s in %llu files. Saves the game keeps in the Windows user profile stay. This cannot be undone." ),
              p->title, shown, sh_bytes( tree.bytes, size, sizeof(size) ), tree.files );
    if (!ui_confirm( ui, sh_tr( "Delete from the card" ), text, sh_tr( "Delete" ) ))
    {
        launcher_log( "[SHELL] delete from the card: not confirmed" );
        return;
    }
    tree.erase = 1;
    ui_progress_begin( ui );
    sh_tree_show( &tree, 1 );
    erased = sh_tree_walk( &tree, folder, 0 );
    ui_progress_end( ui );
    launcher_log( "[SHELL] delete from the card: %s, %llu of %llu files, %llu of %llu bytes, errno %d", erased ? "done" : "FAILED",
                  tree.done_files, tree.files, tree.done_bytes, tree.bytes, tree.error );
    if (!erased)
    {
        snprintf( text, sizeof(text),
                  sh_tr( "Deleting stopped with error %d: %s freed, part of the folder is still on the card. Choose the deletion again to finish it." ),
                  tree.error, sh_bytes( tree.done_bytes, size, sizeof(size) ) );
        ui_message( ui, sh_tr( "Delete from the card" ), text );
        for (i = 0; i < l->program_count; i++)
            if (sh_path_inside( l->programs[i].path, folder )) l->programs[i].missing = !file_exists( l->programs[i].path );
        return;
    }
    if (steam && app)
    {
        runtime_file( l, "steam/installed", path, sizeof(path) );
        snprintf( path + strlen( path ), sizeof(path) - strlen( path ), "/%u.path", app );
        unlink( path );
        runtime_file( l, "steam/selected", path, sizeof(path) );
        snprintf( path + strlen( path ), sizeof(path) - strlen( path ), "/%u.exe", app );
        unlink( path );
        if (steam_storage_discard( l->options->runtime_dir, app, &fs_error ))
            launcher_log( "[SHELL] delete from the card: the unfinished download of app %u stayed, errno %u", app, fs_error );
        steam_ui_forget_install( app );
        sh.owned_read = 0;
    }
    if (!steam_unregister_directory( l, folder ))
        ui_toast( ui, "The files are deleted, but the library could not be saved. Remove the entry by hand.", 5000 );
    else
    {
        snprintf( text, sizeof(text), sh_tr( "Deleted from the card: %s freed" ), sh_bytes( tree.bytes, size, sizeof(size) ) );
        ui_toast( ui, text, 4000 );
    }
    for (i = 0; i < l->program_count; i++) sh_forget( i );
    *gone = 1;
}

static void sh_manage( struct launcher *l, int index, int *gone )
{
    struct program *p = &l->programs[index];
    struct app_record *record = sh_record( l, index );
    const char *items[9];
    int count = 0, chosen, favorite, category, art, shortcut, locate = -1, hide, remove_item, delete_item;

    items[favorite = count++] = sh_tr( p->favorite ? "Remove from favourites" : "Add to favourites" );
    items[category = count++] = sh_tr( "Category" );
    items[art = count++] = sh_tr( "Download artwork" );
    items[shortcut = count++] = sh_tr( "Create a HOME menu shortcut" );
    if (p->missing) items[locate = count++] = sh_tr( "Locate the program" );
    items[hide = count++] = sh_tr( p->settings.hidden ? "Show in the library" : "Hide from the library" );
    items[remove_item = count++] = sh_tr( "Remove from the library" );
    items[delete_item = count++] = sh_tr( "Delete from the card" );
    chosen = ui_menu( &l->ui, p->title, items, count, 0 );
    ui_start_screen( &l->ui );
    if (chosen < 0) return;
    if (chosen == favorite)
    {
        p->favorite = !p->favorite;
        if (!save_library( l )) p->favorite = !p->favorite;
        else ui_toast( &l->ui, p->favorite ? "Added to favourites" : "Removed from favourites", 1500 );
    }
    else if (chosen == category)
    {
        char text[48];

        snprintf( text, sizeof(text), "%s", record->category );
        if (!p->catalog_id) save_library( l );
        if (launcher_platform_prompt( sh_tr( "Category (empty removes it)" ), text, text, sizeof(text) ))
        {
            record->id = p->catalog_id;
            snprintf( record->category, sizeof(record->category), "%s", text );
            if (!app_record_save( l->options->runtime_dir, record )) ui_toast( &l->ui, "Could not save the category", 2500 );
        }
    }
    else if (chosen == art) download_artwork( l, p );
    else if (chosen == shortcut)
    {
        if (!p->added || !l->options->install_game_forwarder)
            ui_message( &l->ui, sh_tr( "Shortcut" ), sh_tr( "A shortcut needs CorkDeck to be started from its own HOME menu entry." ) );
        else if (save_library( l ))
        {
            char key[256] = "", previous[256];
            struct launcher_forwarder_game game = { p->catalog_id, p->title, p->path, p->square_art };

            launcher_kv_get( &l->look, "steamgriddb-key", key, sizeof(key) );
            strcpy( previous, key );
            launcher_forwarder_run( &l->ui, l->options, &game, key, sizeof(key), pick_forwarder_icon, l );
            if (strcmp( key, previous ))
            {
                launcher_kv_set( &l->look, "steamgriddb-key", key );
                save_look( l );
            }
        }
    }
    else if (chosen == locate)
    {
        char selected[512];
        struct program replacement;

        if (file_browser_pick( l, selected, sizeof(selected) ) && describe_program( l, &replacement, selected ))
        {
            snprintf( p->path, sizeof(p->path), "%s", replacement.path );
            snprintf( p->dos, sizeof(p->dos), "%s", replacement.dos );
            p->machine = replacement.machine;
            p->missing = 0;
            load_program_settings( l, p );
            save_library( l );
        }
    }
    else if (chosen == hide)
    {
        p->settings.hidden = !p->settings.hidden;
        save_program_settings( l, p );
    }
    else if (chosen == remove_item &&
             ui_confirm( &l->ui, sh_tr( "Remove from the library" ),
                         sh_tr( "The entry leaves the library. Its files, saves and settings stay on the card." ),
                         sh_tr( "Remove" ) ))
    {
        p->removed = 1;
        if (!save_library( l )) p->removed = 0;
        else *gone = 1;
    }
    else if (chosen == delete_item) sh_delete_from_card( l, index, gone );
    ui_start_screen( &l->ui );
    sh_forget( index );
}

static int sh_side_card( struct ui *ui, int x, int y, int w, const char *title, const char *labels[], const char *values[],
                         const SDL_Color colors[], int count )
{
    int line = TTF_FontHeight( ui->small ) + 6, h = 22 + line * (count + 1), i;

    ui_rounded( ui, x, y, w, h, 10, (SDL_Color){ 24, 33, 44, 235 } );
    ui_text( ui, ui->small, x + 16, y + 11, title, ui->dim );
    for (i = 0; i < count; i++)
    {
        sh_fit( ui, ui->small, x + 16, y + 11 + line * (i + 1),
                     w - 44 - ui_text_width( ui, ui->small, values[i] ), labels[i], ui->text, 0 );
        ui_text_right( ui, ui->small, x + w - 16, y + 11 + line * (i + 1), values[i], colors[i] );
    }
    return h + 12;
}

/* What the program's file asks for that Windows here does not have. A missing
 * part of Microsoft's Visual C++ 2015-2022 package is fetched from Microsoft,
 * checked, and its libraries are put in the Windows folder -- once for each
 * architecture, for any program, with nothing to press. Returns -1 when the
 * user chose not to start, 0 to go on. */
static int sh_dependencies( struct launcher *l, struct program *p )
{
    struct ui *ui = &l->ui;
    struct steam_prereq_missing missing;
    char names[400] = "", text[900];
    unsigned int i, error, added = 0, kept = 0;

    if (!steam_prereq_missing( l->options->runtime_dir, p->path, &missing ) || !missing.count) return 0;
    for (i = 0; i < missing.count; i++)
        snprintf( names + strlen( names ), sizeof(names) - strlen( names ), "%s%s", i ? ", " : "", missing.names[i] );
    launcher_log( "[SHELL] \"%s\" asks for libraries that are not here: %s%s", p->title, names,
                  missing.vc14 ? " (Visual C++ 2015-2022)" : "" );
    if (missing.vc14 && !steam_prereq_tried( l->options->runtime_dir, missing.architecture, 0 ))
    {
        error = steam_prerequisites_fetch_ui( ui, l->options->runtime_dir, missing.architecture );
        if (error == STEAM_PREREQ_CANCELLED) return -1;
        if (!error)
        {
            ui_progress_begin( ui );
            ui_progress_update( ui, "Microsoft libraries", sh_tr( "Adding Microsoft Visual C++ 2015-2022 to Windows..." ), 0, 0 );
            error = steam_prereq_unpack( l->options->runtime_dir, missing.architecture, &added, &kept );
            ui_progress_end( ui );
            /* Tried means the package was read: a download that failed is tried again. */
            if (!error) steam_prereq_tried( l->options->runtime_dir, missing.architecture, 1 );
        }
        launcher_log( "[SHELL] Visual C++ 2015-2022 %s for \"%s\": error %u, %u libraries added, %u already there",
                      missing.architecture ? "x64" : "x86", p->title, error, added, kept );
        if (error)
        {
            snprintf( text, sizeof(text),
                      sh_tr( "This game needs Microsoft Visual C++ 2015-2022 (%s is missing), and it could not be added: error %u. Check the internet connection and press Play again. The game may not start without it." ),
                      names, error );
            return ui_confirm( ui, sh_tr( "Microsoft libraries" ), text, sh_tr( "Play anyway" ) ) ? 0 : -1;
        }
        if (!steam_prereq_missing( l->options->runtime_dir, p->path, &missing ) || !missing.count)
        {
            ui_toast( ui, "Microsoft Visual C++ 2015-2022 was added for this game", 3000 );
            return 0;
        }
        for (names[0] = 0, i = 0; i < missing.count; i++)
            snprintf( names + strlen( names ), sizeof(names) - strlen( names ), "%s%s", i ? ", " : "", missing.names[i] );
    }
    snprintf( text, sizeof(text),
              sh_tr( "This game asks for libraries that are not installed: %s. CorkDeck has no automatic installation for them yet. The game may not start." ),
              names );
    return ui_confirm( ui, sh_tr( "Microsoft libraries" ), text, sh_tr( "Play anyway" ) ) ? 0 : -1;
}

/* A game of the first Steam generation loads Valve's own Steam.dll. That file
 * is Valve's to distribute, so it is not in the package: the first time a
 * program needs it, Valve's package of client libraries is fetched from
 * Valve's update server, checked, and the one file is taken out of it -- for
 * any such program, with nothing to look for. Returns -1 when the user chose
 * not to start, 0 to go on. */
static int sh_legacy_steam( struct launcher *l, struct program *p )
{
    struct ui *ui = &l->ui;
    struct steam_legacy_context context;
    const char *root = l->options->runtime_dir;
    char folder[600], target[640], text[600];
    unsigned int error;

    if (steam_legacy_prepare( root, p->path, p->machine, &context ) != STEAM_LOCAL_PAYLOAD_MISSING) return 0;
    launcher_log( "[SHELL] \"%s\" needs the Steam.dll of the old Steam client", p->title );
    error = steam_prerequisites_fetch_ui( ui, root, STEAM_PREREQ_STEAM_CLIENT );
    if (error == STEAM_PREREQ_CANCELLED) return -1;
    if (!error)
    {
        snprintf( folder, sizeof(folder), "%s/steamworks-local", root );
        mkdir( folder, 0777 );
        snprintf( folder, sizeof(folder), "%s/steamworks-local/legacy-client", root );
        mkdir( folder, 0777 );
        snprintf( target, sizeof(target), "%s/Steam.dll", folder );
        error = steam_prereq_extract( root, STEAM_PREREQ_STEAM_CLIENT, "Steam.dll", target,
                                      STEAM_LEGACY_DLL_SIZE, STEAM_LEGACY_DLL_SHA256 );
        /* Sixty megabytes for one file of half a megabyte: not kept. */
        if (!error) steam_prereq_discard( root, STEAM_PREREQ_STEAM_CLIENT );
    }
    if (!error && steam_legacy_prepare( root, p->path, p->machine, &context ) == STEAM_LOCAL_PAYLOAD_MISSING)
        error = STEAM_PREREQ_PROBE_MISSING;
    launcher_log( "[SHELL] Steam.dll of the old Steam client for \"%s\": error %u", p->title, error );
    if (!error)
    {
        ui_toast( ui, "The Steam library for older games was added", 3000 );
        return 0;
    }
    snprintf( text, sizeof(text),
              sh_tr( "This game needs the Steam library of older games, and it could not be added: error %u. Check the internet connection and press Play again. The game may not start without it." ),
              error );
    return ui_confirm( ui, sh_tr( "Steam library" ), text, sh_tr( "Play anyway" ) ) ? 0 : -1;
}

/* Returns 1 with target set when the program is to start. */
static int sh_app_page( struct launcher *l, int index, char *target, size_t size )
{
    launcher_log( "[SHELL] page of \"%s\" (entry %u): %s%s", l->programs[index].title, l->programs[index].catalog_id,
                  l->programs[index].path, l->programs[index].missing ? " -- its file was not found" : "" );
    struct ui *ui = &l->ui;
    struct ui_input input;
    int focus = 0, gone = 0, dirty = 1, overrides = 0, has_last = 0;
    struct steam_prereq_missing libraries = { 0 };
    struct sh_settings s = { .l = l };
    struct app_session last;
    struct dxvk_version dxvk;
    static const char *const buttons[] = { "Play", "Launch option", "Settings", "Sessions", "Manage" };
    static const struct ui_hint hints[] = { { UI_X, "Manage" }, { UI_B, "Back" }, { UI_A, "Choose" } };

    ui_start_screen( ui );
    while (ui_begin_frame( ui ))
    {
        struct program *p = &l->programs[index];
        struct app_record *record = sh_record( l, index );
        char text[512], length[48], date[40], dxvk_text[64];
        int x, y, i, id;

        pump_icons( l );
        /* Steam's pictures of this one game, asked for by opening its page. */
        sh_art_want( l, index );
        sh_art_pump( l );
        while (ui_poll( ui, &input ))
        {
            if (input.touch == UI_TOUCH_TAP)
            {
                int code = sh_hit_at( input.x, input.y );

                if (code >= 0 && code < 5) { if (focus == code) input.button = UI_A; else focus = code; }
            }
            switch (input.button)
            {
            case UI_LEFT: if (focus > 0) focus--; break;
            case UI_RIGHT: if (focus < 4) focus++; break;
            case UI_X: sh_manage( l, index, &gone ); break;
            case UI_MINUS:
            case UI_PLUS: sh_global_key( l, input.button ); break;
            case UI_B: ui_sound( ui, LAUNCHER_SOUND_BACK ); ui_start_screen( ui ); return 0;
            case UI_A:
                ui_sound( ui, LAUNCHER_SOUND_ACCEPT );
                if (focus == 0)
                {
                    if (p->missing)
                        ui_message( ui, sh_tr( "Program unavailable" ),
                                    sh_tr( "The program's file is not where it was. Connect its drive, or use Manage > Locate the program." ) );
                    else
                    {
                        if (!sh_legacy_steam( l, p ) && !sh_dependencies( l, p ) && start_program( l, p, target, size )) return 1;
                    }
                    ui_start_screen( ui );
                }
                else if (focus == 1) sh_launch_option( l, p );
                else if (focus == 2) sh_settings_screen( l, p );
                else if (focus == 3) sh_sessions( l, p );
                else sh_manage( l, index, &gone );
                sh_forget( index );
                sh.known[index] |= 4;
                break;
            }
            if (input.button == UI_A || input.button == UI_X) dirty = 1;
            if (gone || !ui->running || sh.pending >= 0) { ui_start_screen( ui ); return 0; }
        }
        if (!ui->running) return 0;

        if (dirty)
        {
            /* Everything read from the card for this page, once. */
            p = &l->programs[index];
            record = sh_record( l, index );
            s.p = p;
            sh_settings_resolve( &s );
            for (id = 0, overrides = 0; id < APP_SET_COUNT; id++)
                overrides += s.resolved.values[id].layer[APP_LAYER_PROFILE] >= 0;
            memset( &dxvk, 0, sizeof(dxvk) );
            if (s.resolved.values[APP_SET_RENDERER].effective != 0)
                dxvk_resolve_version( (enum dxvk_source)s.resolved.values[APP_SET_DXVK_SOURCE].effective,
                                      l->options->runtime_dir, p->machine, s.resolved.values[APP_SET_DXVK_VERSION].text, &dxvk );
            has_last = p->catalog_id && app_record_sessions( l->options->runtime_dir, p->catalog_id, &last, 1 );
            if (p->missing || !steam_prereq_missing( l->options->runtime_dir, p->path, &libraries )) libraries.count = 0;
            dirty = 0;
        }
        sh_frame_begin( l );
        request_art( l, index, ART_HERO );
        if (p->hero_icon)
        {
            SDL_Rect all = { 0, 0, ui->width, ui->height };

            ui_rounded_texture( ui, p->hero_icon, NULL, all, 0, (SDL_Color){ 90, 100, 115, 255 } );
            ui_gradient( ui, 0, 0, ui->width, ui->height, (SDL_Color){ 14, 20, 27, 245 }, (SDL_Color){ 14, 20, 27, 150 }, 1 );
        }
        sh_top( l, sh_tr( "Library" ), p->title );

        x = SH_MARGIN;
        y = 250;
        x += sh_chip( ui, x, y, sh_is_steam( l, index ) ? "Steam" : sh_tr( "Local" ), ui->dim );
        x += sh_chip( ui, x, y, p->machine == 0x8664 ? "x64" : p->machine == 0x014c ? "x86" : "ARM64", ui->dim );
        if (record->category[0]) x += sh_chip( ui, x, y, record->category, SH_BLUE );
        if (p->missing) sh_chip( ui, x, y, sh_tr( "File missing" ), SH_AMBER );
        sh_fit( ui, ui->large, SH_MARGIN, y + 44, 760, p->title, ui->value, 0 );
        y += 44 + TTF_FontHeight( ui->large ) + 16;
        snprintf( text, sizeof(text), "%s: %s", sh_tr( "Time in the application" ),
                  record->seconds ? sh_duration( record->seconds, length, sizeof(length) ) : sh_tr( "not started yet" ) );
        ui_text( ui, ui->small, SH_MARGIN, y, text, ui->text );
        y += TTF_FontHeight( ui->small ) + 6;
        snprintf( text, sizeof(text), "%s: %s", sh_tr( "Last start" ), sh_date( record->last_start, date, sizeof(date) ) );
        ui_text( ui, ui->small, SH_MARGIN, y, text, ui->text );
        y += TTF_FontHeight( ui->small ) + 6;
        snprintf( text, sizeof(text), "%s: %s", sh_tr( "Starts" ), p->dos );
        sh_fit( ui, ui->small, SH_MARGIN, y, 760, text, ui->dim, 0 );

        for (i = 0, x = SH_MARGIN, y = 560; i < 5; i++)
            x += sh_button( ui, x, i ? y + 5 : y, sh_tr( buttons[i] ), i == 0 ? 1 : i == 4 ? 2 : 0, focus == i, i ) + 14;

        /* What a start will use, and whether it is there. */
        {
            const char *labels[4], *values[4];
            SDL_Color colors[4];
            char library_text[96];
            /* From under the top bar, so that three cards end above the buttons. */
            int cy = 104, cx = 880, cw = 336, dxvk_wanted = s.resolved.values[APP_SET_RENDERER].effective != 0;

            labels[0] = sh_tr( "Windows components" );
            values[0] = sh_tr( launcher_dlls_is_ready( l->dlls ) ? "ready" : "to download" );
            colors[0] = launcher_dlls_is_ready( l->dlls ) ? SH_GREEN : SH_AMBER;
            labels[1] = "DXVK";
            if (!dxvk_wanted) { values[1] = sh_tr( "not used" ); colors[1] = ui->dim; }
            else
            {
                snprintf( dxvk_text, sizeof(dxvk_text), "%s", !dxvk.installed ? sh_tr( "to download" )
                          : dxvk.version[0] ? dxvk.version : sh_tr( "built in" ) );
                values[1] = dxvk_text;
                colors[1] = dxvk.installed ? SH_GREEN : SH_AMBER;
            }
            labels[2] = "Steam";
            values[2] = sh_tr( sh_is_steam( l, index ) ? "prepared at start" : "not needed" );
            colors[2] = sh_is_steam( l, index ) ? SH_GREEN : ui->dim;
            /* Only when something is asked for that is not there: the card has no room to spare. */
            if (libraries.count)
            {
                labels[3] = sh_tr( "Libraries" );
                if (libraries.count == 1) snprintf( library_text, sizeof(library_text), sh_tr( "no %s" ), libraries.names[0] );
                else snprintf( library_text, sizeof(library_text), sh_tr( "%u are missing" ), libraries.count );
                values[3] = library_text;
                colors[3] = SH_AMBER;
            }
            cy += sh_side_card( ui, cx, cy, cw, sh_tr( "DEPENDENCIES" ), labels, values, colors, libraries.count ? 4 : 3 );

            labels[0] = sh_tr( "Translator" );
            values[0] = p->machine == 0x014c || p->machine == 0x8664
                        ? app_setting_value_label( &s.resolved, APP_SET_TRANSLATOR, sh.language ) : sh_tr( "not needed" );
            labels[1] = sh_tr( "Renderer" );
            values[1] = app_setting_value_label( &s.resolved, APP_SET_RENDERER, sh.language );
            labels[2] = sh_tr( "Changed in its profile" );
            snprintf( length, sizeof(length), "%d", overrides );
            values[2] = length;
            colors[0] = colors[1] = colors[2] = ui->dim;
            cy += sh_side_card( ui, cx, cy, cw, sh_tr( "WILL BE USED" ), labels, values, colors, 3 );

            if (has_last)
            {
                char ended[64];

                labels[0] = sh_tr( "Length" );
                values[0] = sh_duration( last.seconds, date, sizeof(date) );
                labels[1] = sh_tr( "Exit code" );
                snprintf( ended, sizeof(ended), "0x%x", last.exit_code );
                values[1] = ended;
                colors[0] = ui->dim;
                colors[1] = last.exit_code ? SH_AMBER : SH_GREEN;
                sh_side_card( ui, cx, cy, cw, sh_tr( "LAST SESSION" ), labels, values, colors, 2 );
            }
        }
        ui_hints_right( ui, hints, 3, ui->width - SH_MARGIN, UI_FOOTER_Y - 6 );
        ui_draw_toast( ui );
        ui_present( ui );
        ui_wait( ui );
    }
    return 0;
}

/***********************************************************************
 * Pages that are lists
 */

/* Downloads: the one transfer Steam is making, as a bar with what is done and
 * what is left, and how fast it has gone over the last two minutes -- the
 * network as bars, what reaches the card as a line over them. */
#define SH_SPEED_SAMPLES 120

static const char *sh_bytes( unsigned long long bytes, char *out, size_t size )
{
    if (bytes >= 1024ull * 1048576) snprintf( out, size, sh_tr( "%.2f GB" ), bytes / (1024.0 * 1048576) );
    else snprintf( out, size, sh_tr( "%.0f MB" ), bytes / 1048576.0 );
    return out;
}

/* Downloads that were begun and not finished, by the folder each left on the
 * card: after the application is started again Steam remembers none of them. */
static int sh_unfinished( struct launcher *l, unsigned int *apps, int capacity )
{
    char path[512], installed[512];
    struct dirent *entry;
    struct stat st;
    DIR *dir;
    int count = 0;

    runtime_file( l, "steam/staging", path, sizeof(path) );
    if (!(dir = opendir( path ))) return 0;
    while (count < capacity && (entry = readdir( dir )))
    {
        char *end, item[800];
        unsigned long app = strtoul( entry->d_name, &end, 10 );

        if (!app || *end) continue;
        snprintf( item, sizeof(item), "%s/%s", path, entry->d_name );
        if (stat( item, &st ) || !S_ISDIR( st.st_mode )) continue;
        if (steam_storage_directory( l->options->runtime_dir, (uint32_t)app, installed )) continue;
        apps[count++] = (unsigned int)app;
    }
    closedir( dir );
    return count;
}

static const char *sh_unfinished_name( uint32_t app, char *out, size_t size )
{
    const char *name = sh_owned_name( app );

    if (strcmp( name, "Steam" )) return name;
    snprintf( out, size, sh_tr( "Steam game %u" ), app );
    return out;
}

static void sh_downloads( struct launcher *l )
{
    static float network[SH_SPEED_SAMPLES], card[SH_SPEED_SAMPLES];
    static uint32_t sampled_app;
    struct ui *ui = &l->ui;
    struct ui_input input;
    struct steam_download_view d;
    unsigned long long last_network = 0, last_card = 0;
    Uint32 sampled = 0;
    uint32_t asked_app = 0;
    unsigned int asked_attempts = 0, left[6];
    uint32_t wait_app = 0;
    Uint32 wait_since = 0;
    int have_last = 0, i, left_count = 0, left_selected = 0, left_scan = 1, last_state = -1;

    launcher_log( "[SHELL] Downloads" );
    ui_start_screen( ui );
    while (ui_begin_frame( ui ))
    {
        struct ui_hint hints[3];
        char text[200], a[40], b[40];
        int active, can_continue, stuck, idle, ask = 0, hint_count = 0, x = SH_MARGIN, w = ui->width - 2 * SH_MARGIN, y;
        Uint32 now = SDL_GetTicks();

        memset( &d, 0, sizeof(d) );
        steam_ui_download( &d );
        active = steam_download_active( d.state );
        /* A download that was cancelled and removed is no download any more. */
        idle = !d.app_id || d.state == STEAM_DOWNLOAD_IDLE || d.state == STEAM_DOWNLOAD_DISCARDED;
        if ((int)d.state != last_state) { last_state = (int)d.state; left_scan = 1; }
        if (left_scan)
        {
            left_scan = 0;
            left_count = idle ? sh_unfinished( l, left, 6 ) : 0;
            if (left_selected >= left_count) left_selected = left_count ? left_count - 1 : 0;
            if (left_count) launcher_log( "[SHELL] Downloads: %d unfinished on the card, the first is app %u", left_count, left[0] );
            /* Their names, and Continue at once, need the account. */
            if (left_count && !steam_ui_online()) steam_ui_autoresume( l->options->runtime_dir );
        }
        /* Continue was asked for before Steam was signed in. */
        if (wait_app)
        {
            if (!idle) wait_app = 0;
            else if (steam_ui_online())
            {
                sh_read_owned( l, 0 );
                if (steam_ui_resume( wait_app )) wait_app = 0;
            }
            if (wait_app && now - wait_since > 40000)
            {
                wait_app = 0;
                ui_toast( ui, "Could not continue: there is no connection to Steam. Sign in under Maintenance, Steam account.", 5000 );
            }
        }
        else if (idle && left_count && steam_ui_online()) sh_read_owned( l, 0 );
        can_continue = !active && d.app_id && (d.state == STEAM_DOWNLOAD_ERROR || d.state == STEAM_DOWNLOAD_CANCELLED);
        /* The same failure again with nothing more verified: Continue is of no
         * more use, so the choice is put once by itself and then on the button. */
        stuck = can_continue && d.state == STEAM_DOWNLOAD_ERROR && d.stuck;
        if (stuck && (asked_app != d.app_id || asked_attempts != d.attempts)) ask = 1;
        if (d.app_id != sampled_app)
        {
            memset( network, 0, sizeof(network) );
            memset( card, 0, sizeof(card) );
            sampled_app = d.app_id;
            have_last = 0;
        }
        if (!sampled || now - sampled >= 1000)
        {
            float seconds = sampled ? (now - sampled) / 1000.0f : 1;

            memmove( network, network + 1, sizeof(network) - sizeof(network[0]) );
            memmove( card, card + 1, sizeof(card) - sizeof(card[0]) );
            network[SH_SPEED_SAMPLES - 1] = active && have_last && d.network_bytes >= last_network
                                            ? (d.network_bytes - last_network) / seconds : 0;
            card[SH_SPEED_SAMPLES - 1] = active && have_last && d.downloaded >= last_card
                                         ? (d.downloaded - last_card) / seconds : 0;
            last_network = d.network_bytes;
            last_card = d.downloaded;
            have_last = active;
            sampled = now | 1;
        }

        while (ui_poll( ui, &input ))
        {
            switch (input.button)
            {
            case UI_MINUS:
            case UI_PLUS:
                sh_global_key( l, input.button );
                if (sh.pending >= 0) return;
                break;
            case UI_B: ui_sound( ui, LAUNCHER_SOUND_BACK ); ui_start_screen( ui ); return;
            case UI_A:
                if (active)
                {
                    ui_sound( ui, LAUNCHER_SOUND_ACCEPT );
                    steam_ui_stop();
                    ui_toast( ui, "The download is being cancelled; the unfinished files are removed", 3000 );
                }
                else if (stuck) ask = 1;
                else if (can_continue)
                {
                    ui_sound( ui, LAUNCHER_SOUND_ACCEPT );
                    if (!steam_ui_resume( d.app_id ))
                        ui_toast( ui, "Could not continue: there is no connection to Steam, or another operation is running.", 4000 );
                }
                else if (idle && left_count && !wait_app)
                {
                    ui_sound( ui, LAUNCHER_SOUND_ACCEPT );
                    launcher_log( "[SHELL] Downloads: continue the unfinished download of app %u (Steam %s)", left[left_selected],
                                  steam_ui_online() ? "online" : "not signed in yet" );
                    if (!steam_ui_online() || !steam_ui_resume( left[left_selected] ))
                    {
                        steam_ui_autoresume( l->options->runtime_dir );
                        wait_app = left[left_selected];
                        wait_since = now;
                        ui_toast( ui, "Connecting to Steam; the download then continues", 4000 );
                    }
                }
                break;
            case UI_UP: if (idle && left_selected > 0) left_selected--; break;
            case UI_DOWN: if (idle && left_selected + 1 < left_count) left_selected++; break;
            case UI_X:
                if (idle && left_count && !wait_app)
                {
                    char name[64], question[400];
                    unsigned int app = left[left_selected], fs_error = 0;

                    snprintf( question, sizeof(question),
                              sh_tr( "What was downloaded of \"%s\" is deleted from the card. The game can be installed again from its page." ),
                              sh_unfinished_name( app, name, sizeof(name) ) );
                    if (ui_confirm( ui, sh_tr( "Delete the unfinished download" ), question, sh_tr( "Delete" ) ))
                    {
                        if (steam_ui_busy( app ) || steam_storage_discard( l->options->runtime_dir, app, &fs_error ))
                            ui_toast( ui, "The unfinished download could not be deleted", 4000 );
                        else ui_toast( ui, "The unfinished download is deleted", 3000 );
                        launcher_log( "[SHELL] Downloads: the unfinished download of app %u deleted, errno %u", app, fs_error );
                        steam_ui_forget_install( app );
                        left_scan = 1;
                    }
                    ui_start_screen( ui );
                }
                break;
            default: break;
            }
        }
        if (!ui->running) return;
        if (ask && stuck)
        {
            const char *items[3] = { sh_tr( "Start the download over" ), sh_tr( "Cancel the download" ),
                                     sh_tr( "Try to continue once more" ) };
            char title[96];
            int chosen;

            asked_app = d.app_id;
            asked_attempts = d.attempts;
            snprintf( title, sizeof(title), sh_tr( "The download stopped: error %u" ), d.error );
            launcher_log( "[SHELL] Downloads: app %u failed %u times in a row with error %u (stage %u, depot %u, file \"%s\"); asking what to do",
                          d.app_id, d.attempts, d.error, d.failed_stage, d.depot_id, d.file );
            chosen = ui_menu( ui, title, items, 3, 0 );
            launcher_log( "[SHELL] Downloads: %s", chosen == 0 ? "start over" : chosen == 1 ? "cancel" : chosen == 2 ? "continue once more" : "left as it is" );
            if (chosen == 0)
            {
                if (steam_ui_restart()) ui_toast( ui, "Removing the unfinished files; the download then starts over", 4000 );
                else ui_toast( ui, "Could not continue. Open the game's page and install it again.", 3500 );
            }
            else if (chosen == 1)
            {
                steam_ui_stop();
                ui_toast( ui, "The download is being cancelled; the unfinished files are removed", 3000 );
            }
            else if (chosen == 2 && !steam_ui_resume( d.app_id ))
                ui_toast( ui, "Could not continue: there is no connection to Steam, or another operation is running.", 4000 );
            ui_start_screen( ui );
        }

        sh_frame_begin( l );
        sh_top( l, sh_tr( "Downloads" ), NULL );
        y = SH_TOP + 36;
        if (idle && left_count)
        {
            ui_text( ui, ui->small, x, y - 6, sh_tr( "UNFINISHED DOWNLOADS" ), ui->dim );
            y += TTF_FontHeight( ui->small ) + 8;
            for (i = 0; i < left_count; i++, y += 84)
            {
                char name[64];

                ui_rounded( ui, x, y, w, 72, 8, SH_PANEL );
                if (i == left_selected) sh_focus_ring( ui, x, y, w, 72, 8 );
                sh_fit( ui, ui->normal, x + 28, y + 20, w - 56 - 420, sh_unfinished_name( left[i], name, sizeof(name) ), ui->value, 0 );
                ui_text_right( ui, ui->small, x + w - 28, y + 24,
                               sh_tr( wait_app == left[i] ? "Connecting to Steam..." : "Stopped; what was downloaded is on the card" ),
                               wait_app == left[i] ? SH_BLUE : SH_AMBER );
            }
            ui_text_wrapped( ui, ui->small, x, y + 4, w, 2,
                             "Continue checks what is on the card and downloads the rest. It needs the Steam account to be signed in.",
                             ui->dim, 0 );
        }
        else if (idle)
        {
            ui_rounded( ui, x, y, w, 200, 8, SH_PANEL );
            ui_text( ui, ui->large, x + 32, y + 52, sh_tr( "Nothing is downloading" ), ui->value );
            ui_text_wrapped( ui, ui->small, x + 32, y + 52 + TTF_FontHeight( ui->large ) + 14, w - 64, 3,
                             "A Steam game is installed from its page: the Library, the Not installed tab. Its download appears here.",
                             ui->dim, 0 );
        }
        else
        {
            float ratio = d.total ? (float)((double)d.downloaded / (double)d.total) : 0, peak = 1;
            int bar_x = x + 32, bar_w = w - 64, bar_y, graph_y, graph_h = 170, column;
            const char *state = active ? (d.total ? "Downloading" : "Preparing")
                                : d.state == STEAM_DOWNLOAD_ERROR ? "Stopped with an error"
                                : d.state == STEAM_DOWNLOAD_CANCELLED ? "Stopped"
                                : d.state == STEAM_DOWNLOAD_COMPLETE ? "Installed" : "Waiting";
            SDL_Color tone = d.state == STEAM_DOWNLOAD_ERROR ? SH_AMBER : d.state == STEAM_DOWNLOAD_COMPLETE ? SH_GREEN : SH_BLUE;

            if (ratio > 1) ratio = 1;
            ui_rounded( ui, x, y, w, 210, 8, SH_PANEL );
            sh_fit( ui, ui->large, x + 32, y + 26, w - 64 - 260, sh_owned_name( d.app_id ), ui->value, 0 );
            ui_text_right( ui, ui->normal, x + w - 32, y + 34, sh_tr( state ), tone );
            bar_y = y + 26 + TTF_FontHeight( ui->large ) + 22;
            ui_rounded( ui, bar_x, bar_y, bar_w, 16, 4, (SDL_Color){ 44, 58, 75, 255 } );
            if (ratio > 0) ui_rounded( ui, bar_x, bar_y, (int)(bar_w * ratio) < 8 ? 8 : (int)(bar_w * ratio), 16, 4, tone );
            if (d.total)
            {
                snprintf( text, sizeof(text), sh_tr( "%s of %s" ), sh_bytes( d.downloaded, a, sizeof(a) ), sh_bytes( d.total, b, sizeof(b) ) );
                ui_text( ui, ui->small, bar_x, bar_y + 30, text, ui->text );
                snprintf( text, sizeof(text), "%d%%", (int)(ratio * 100) );
                ui_text_right( ui, ui->normal, bar_x + bar_w, bar_y + 26, text, ui->value );
            }
            if (active && d.bytes_per_second > 1 && d.total > d.downloaded)
            {
                unsigned long long left = (unsigned long long)((d.total - d.downloaded) / d.bytes_per_second);

                snprintf( text, sizeof(text), sh_tr( "%.1f MB/s  ·  about %s left" ), d.bytes_per_second / 1048576.0,
                          sh_duration( left, a, sizeof(a) ) );
                ui_text( ui, ui->small, bar_x, bar_y + 30 + TTF_FontHeight( ui->small ) + 8, text, ui->dim );
            }
            else if (d.state == STEAM_DOWNLOAD_ERROR)
            {
                snprintf( text, sizeof(text), sh_tr( stuck ? "Error %u, %u times in a row. It cannot be continued: start it over or cancel it."
                                                           : "Error %u. What was downloaded is kept; Continue takes it up from there." ),
                          d.error, d.attempts );
                ui_text( ui, ui->small, bar_x, bar_y + 30 + TTF_FontHeight( ui->small ) + 8, text, SH_AMBER );
            }

            /* The last two minutes. */
            graph_y = y + 210 + 16;
            ui_rounded( ui, x, graph_y, w, graph_h + 74, 8, SH_PANEL );
            for (i = 0; i < SH_SPEED_SAMPLES; i++)
            {
                if (network[i] > peak) peak = network[i];
                if (card[i] > peak) peak = card[i];
            }
            ui_text( ui, ui->small, x + 32, graph_y + 16, sh_tr( "SPEED, THE LAST TWO MINUTES" ), ui->dim );
            snprintf( text, sizeof(text), sh_tr( "peak %.1f MB/s" ), peak > 1 ? peak / 1048576.0 : 0.0 );
            ui_text_right( ui, ui->small, x + w - 32, graph_y + 16, text, ui->dim );
            column = bar_w / SH_SPEED_SAMPLES;
            ui_fill( ui, bar_x, graph_y + 48 + graph_h, column * SH_SPEED_SAMPLES, 1, SH_LINE );
            for (i = 0; i < SH_SPEED_SAMPLES; i++)
            {
                int h = (int)(network[i] / peak * graph_h), c = (int)(card[i] / peak * graph_h);

                if (h > 0) ui_fill( ui, bar_x + i * column, graph_y + 48 + graph_h - h, column - 2, h, (SDL_Color){ 26, 159, 255, 200 } );
                if (c > 0) ui_fill( ui, bar_x + i * column, graph_y + 48 + graph_h - c - 1, column, 3, SH_GREEN );
            }
            ui_fill( ui, bar_x, graph_y + 48 + graph_h + 12, 14, 8, SH_BLUE );
            ui_text( ui, ui->small, bar_x + 22, graph_y + 48 + graph_h + 6, sh_tr( "network" ), ui->dim );
            ui_fill( ui, bar_x + 150, graph_y + 48 + graph_h + 14, 14, 3, SH_GREEN );
            ui_text( ui, ui->small, bar_x + 172, graph_y + 48 + graph_h + 6, sh_tr( "written to the card" ), ui->dim );
        }
        /* It removes what was downloaded (steam_provider_cancel_download), and says so. */
        if (active) hints[hint_count++] = (struct ui_hint){ UI_A, "Cancel the download" };
        else if (stuck) hints[hint_count++] = (struct ui_hint){ UI_A, "Start over or cancel" };
        else if (can_continue) hints[hint_count++] = (struct ui_hint){ UI_A, "Continue" };
        else if (idle && left_count)
        {
            hints[hint_count++] = (struct ui_hint){ UI_X, "Delete" };
            hints[hint_count++] = (struct ui_hint){ UI_A, "Continue" };
        }
        hints[hint_count++] = (struct ui_hint){ UI_B, "Back" };
        ui_hints_right( ui, hints, hint_count, ui->width - SH_MARGIN, UI_FOOTER_Y - 6 );
        ui_draw_toast( ui );
        ui->busy_until = now + (active || wait_app ? 300 : 1000);   /* a transfer may start while this is open */
        ui_present( ui );
        ui_wait( ui );
    }
}

static void sh_maintenance( struct launcher *l )
{
    struct sh_row rows[8];
    char text[512];
    int selection = 0, chosen, tone;

    for (;;)
    {
        memset( rows, 0, sizeof(rows) );
        snprintf( rows[0].label, sizeof(rows[0].label), "Windows components" );
        snprintf( rows[0].value, sizeof(rows[0].value), "%s", launcher_dlls_status( l->dlls, text, sizeof(text), &tone ) );
        rows[0].tone = tone;
        rows[0].help = "Install, update and verify the Windows files programs run on. This does not change CorkDeck itself.";
        snprintf( rows[1].label, sizeof(rows[1].label), "First set-up" );
        rows[1].help = "The steps of a new card in order: the HOME menu entry, Windows components, and the Hekate boot entry with its patches.";
        snprintf( rows[2].label, sizeof(rows[2].label), "HOME menu entry (forwarder)" );
        snprintf( rows[2].value, sizeof(rows[2].value), "%s", l->options->own_forwarder ? "Installed; A installs it again" : l->options->install_forwarder ? "Not installed" : "Unavailable" );
        rows[2].help = "Installs the entry CorkDeck must be started from to have its 39-bit address space, with its current name and picture. Installing it again replaces an older one. Use emuMMC only.";
        rows[2].disabled = !l->options->install_forwarder;
        snprintf( rows[3].label, sizeof(rows[3].label), "Hekate boot entry and patches" );
        rows[3].help = "Shows what will change before anything is written. An existing HOC configuration is kept.";
        snprintf( rows[4].label, sizeof(rows[4].label), "Shared key mapping" );
        rows[4].help = "The keys every program's controls send, unless the program has its own.";
        snprintf( rows[5].label, sizeof(rows[5].label), "Steam account" );
        rows[5].help = "Sign in, the saved session, the language of downloads and the number of connections.";
        snprintf( rows[6].label, sizeof(rows[6].label), "Artwork source key (SteamGridDB)" );
        snprintf( rows[6].value, sizeof(rows[6].value), "%s",
                  launcher_kv_get( &l->look, "steamgriddb-key", text, sizeof(text) ) && text[0] ? "Set" : "Not set" );
        rows[6].help = "The key that lets covers be downloaded for programs that are not from Steam.";
        /* The version is told here, where it is always in sight: the build's full
         * name is in Diagnostics, which only developer mode shows. */
        snprintf( rows[7].label, sizeof(rows[7].label), "About CorkDeck" );
        snprintf( rows[7].value, sizeof(rows[7].value), "%s", sh_tr( "Version " CORKDECK_VERSION ) );
        rows[7].help = "The version of this application and the people and projects it is made of.";
        chosen = sh_rows( l, "Maintenance", "Components and system", rows, 8, &selection );
        if (chosen < 0) return;
        if (chosen == 0) launcher_dlls_open( l->dlls );
        else if (chosen == 1 || chosen == 3) quick_setup( l );
        else if (chosen == 2) make_forwarder( l );
        else if (chosen == 4)
        {
            runtime_file( l, "config/keys.txt", text, sizeof(text) );
            controls_screen( l, text, NULL, "Controls" );
        }
        else if (chosen == 5) steam_ui_sign_in( &l->ui, l->options->runtime_dir );
        else if (chosen == 6)
        {
            char key[512] = "";

            launcher_kv_get( &l->look, "steamgriddb-key", key, sizeof(key) );
            if (launcher_platform_prompt( sh_tr( "SteamGridDB API key (empty removes it)" ), key, key, sizeof(key) ))
            {
                launcher_kv_set( &l->look, "steamgriddb-key", key[0] ? key : NULL );
                save_look( l );
            }
        }
        else credits_screen( l );
        ui_start_screen( &l->ui );
    }
}

/***********************************************************************
 * Storage: what each game takes on the card
 *
 * One row for each folder a game is in, the largest first, with where it came
 * from. A Steam installation says its size in the installer's marker (builds
 * from 0.2c write it there); any other folder is counted once, on a thread of
 * its own, and the count is kept in corkdeck/sizes.txt so that it is not made
 * again. Y counts everything anew. A opens the game's page, where Manage
 * deletes it from the card.
 */

#define SH_STORE_ROWS 96

struct sh_store_row
{
    char folder[512];
    int program;                 /* the first entry of the library in that folder */
    int source;                  /* 0 local, 1 Steam, 2 USB, 3 unfinished downloads */
    int shared;                  /* the folder holds other things: no size is given */
    int counted;                 /* 1 from the marker or the kept counts, 2 by the thread just now */
    unsigned long long bytes;
    SDL_atomic_t done;           /* the thread: 1 counted, 2 could not be */
    unsigned long long thread_bytes;
};

static struct
{
    struct sh_store_row rows[SH_STORE_ROWS];
    int count, order[SH_STORE_ROWS];
    SDL_Thread *thread;
    SDL_atomic_t stop, at;
} sh_store;

static int sh_store_tree( const char *path, int depth, unsigned long long *bytes )
{
    char child[1024];
    struct dirent *entry;
    struct stat st;
    DIR *dir;
    int ok = 1;

    if (SDL_AtomicGet( &sh_store.stop ) || depth > 32 || lstat( path, &st )) return 0;
    if (!S_ISDIR( st.st_mode )) { *bytes += (unsigned long long)st.st_size; return 1; }
    if (!(dir = opendir( path ))) return 0;
    while (ok && (entry = readdir( dir )))
    {
        if (!strcmp( entry->d_name, "." ) || !strcmp( entry->d_name, ".." )) continue;
        if (snprintf( child, sizeof(child), "%s/%s", path, entry->d_name ) >= (int)sizeof(child)) continue;
        ok = sh_store_tree( child, depth + 1, bytes );
    }
    closedir( dir );
    return ok;
}

static int sh_store_worker( void *unused )
{
    int i;

    (void)unused;
    for (i = 0; i < sh_store.count && !SDL_AtomicGet( &sh_store.stop ); i++)
    {
        struct sh_store_row *row = &sh_store.rows[i];
        unsigned long long bytes = 0;

        if (row->counted || row->shared) continue;
        SDL_AtomicSet( &sh_store.at, i );
        if (sh_store_tree( row->folder, 0, &bytes ))
        {
            row->thread_bytes = bytes;
            SDL_AtomicSet( &row->done, 1 );
        }
        else if (!SDL_AtomicGet( &sh_store.stop )) SDL_AtomicSet( &row->done, 2 );
    }
    SDL_AtomicSet( &sh_store.at, -1 );
    return 0;
}

static void sh_store_stop(void)
{
    if (!sh_store.thread) return;
    SDL_AtomicSet( &sh_store.stop, 1 );
    SDL_WaitThread( sh_store.thread, NULL );
    sh_store.thread = NULL;
    SDL_AtomicSet( &sh_store.stop, 0 );
}

/* The counts kept from before: "bytes<TAB>folder" a line. */
static void sh_store_kept( struct launcher *l, int write )
{
    char path[600], line[700];
    FILE *file;
    int i;

    runtime_file( l, "corkdeck/sizes.txt", path, sizeof(path) );
    if (write)
    {
        if (!(file = fopen( path, "wb" ))) return;
        for (i = 0; i < sh_store.count; i++)
            if (sh_store.rows[i].counted && !sh_store.rows[i].shared && sh_store.rows[i].source != 3)
                fprintf( file, "%llu\t%s\n", sh_store.rows[i].bytes, sh_store.rows[i].folder );
        fclose( file );
        return;
    }
    if (!(file = fopen( path, "rb" ))) return;
    while (fgets( line, sizeof(line), file ))
    {
        char *tab = strchr( line, '\t' );

        if (!tab) continue;
        tab[strcspn( tab, "\r\n" )] = 0;
        for (i = 0; i < sh_store.count; i++)
            if (!sh_store.rows[i].counted && !strcmp( sh_store.rows[i].folder, tab + 1 ))
            {
                sh_store.rows[i].bytes = strtoull( line, NULL, 10 );
                sh_store.rows[i].counted = 1;
            }
    }
    fclose( file );
}

static int sh_store_compare( const void *a, const void *b )
{
    const struct sh_store_row *x = &sh_store.rows[*(const int *)a], *y = &sh_store.rows[*(const int *)b];

    if (x->counted && y->counted && x->bytes != y->bytes) return x->bytes < y->bytes ? 1 : -1;
    if (!x->counted != !y->counted) return x->counted ? -1 : 1;
    return *(const int *)a - *(const int *)b;
}

/* The rows as the library and the card are now. recount: forget what was kept. */
static void sh_store_build( struct launcher *l, int recount )
{
    char folder[512], other[128], path[600], line[128];
    int i, j;

    sh_store_stop();
    memset( sh_store.rows, 0, sizeof(sh_store.rows) );
    sh_store.count = 0;
    for (i = 0; i < l->program_count && sh_store.count < SH_STORE_ROWS - 1; i++)
    {
        struct program *p = &l->programs[i];
        struct sh_store_row *row;
        int steam;

        if (p->removed || p->missing) continue;
        steam = sh_steam_folder( l, i, folder, sizeof(folder) );
        if (!steam)
        {
            snprintf( folder, sizeof(folder), "%s", p->path );
            parent_dir( folder );
        }
        for (j = 0; j < sh_store.count; j++) if (!strcmp( sh_store.rows[j].folder, folder )) break;
        if (j < sh_store.count) continue;
        row = &sh_store.rows[sh_store.count++];
        snprintf( row->folder, sizeof(row->folder), "%s", folder );
        row->program = i;
        row->source = steam ? 1 : usb_program_path( p->path ) ? 2 : 0;
        if (!steam)
        {
            /* Another entry in the same folder is the same game's; anything else makes the folder no game's own. */
            const char *why = sh_folder_shared( l, i, folder, other, sizeof(other) );

            row->shared = why && !other[0];
        }
        else if (!recount)
        {
            FILE *file;

            snprintf( path, sizeof(path), "%s/autorun-steam-install.ini", folder );
            if ((file = fopen( path, "rb" )))
            {
                while (fgets( line, sizeof(line), file ))
                    if (!strncmp( line, "size=", 5 ) && (row->bytes = strtoull( line + 5, NULL, 10 ))) row->counted = 1;
                fclose( file );
            }
        }
    }
    /* What downloads left unfinished, as one row. */
    runtime_file( l, "steam/staging", path, sizeof(path) );
    if (directory_exists( path ))
    {
        struct sh_store_row *row = &sh_store.rows[sh_store.count++];

        snprintf( row->folder, sizeof(row->folder), "%s", path );
        row->program = -1;
        row->source = 3;
    }
    if (!recount) sh_store_kept( l, 0 );
    for (i = 0; i < sh_store.count; i++) sh_store.order[i] = i;
    qsort( sh_store.order, sh_store.count, sizeof(sh_store.order[0]), sh_store_compare );
    SDL_AtomicSet( &sh_store.at, -1 );
    for (i = 0; i < sh_store.count; i++)
        if (!sh_store.rows[i].counted && !sh_store.rows[i].shared)
        {
            sh_store.thread = SDL_CreateThreadWithStackSize( sh_store_worker, "corkdeck-sizes", 256 * 1024, NULL );
            break;
        }
}

/* Once a frame: takes what the thread counted. Returns 1 when a row changed. */
static int sh_store_pump( struct launcher *l )
{
    int i, changed = 0, pending = 0;

    for (i = 0; i < sh_store.count; i++)
    {
        struct sh_store_row *row = &sh_store.rows[i];
        int done = SDL_AtomicGet( &row->done );

        if (row->counted || row->shared) continue;
        if (done == 1) { row->bytes = row->thread_bytes; row->counted = 2; changed = 1; }
        else if (done == 2) { row->shared = 2; changed = 1; }
        else pending = 1;
    }
    if (changed)
    {
        qsort( sh_store.order, sh_store.count, sizeof(sh_store.order[0]), sh_store_compare );
        if (!pending) sh_store_kept( l, 1 );
    }
    if (!pending && sh_store.thread) { SDL_WaitThread( sh_store.thread, NULL ); sh_store.thread = NULL; }
    return changed;
}

/* Returns 1 with target set when a program is to start. */
static int sh_storage( struct launcher *l, char *target, size_t size )
{
    struct ui *ui = &l->ui;
    struct ui_input input;
    struct wine_nx_launcher_usb_volume volumes[LAUNCHER_MAX_USB_VOLUMES];
    struct statvfs vfs;
    unsigned long long card_free = 0, card_total = 0;
    int selection = 0, top = 0, card_known, usb_count, i, missing = 0;
    const int visible = 6, row_h = 62;

    launcher_log( "[SHELL] Storage" );
    sh_store_build( l, 0 );
    card_known = !statvfs( LAUNCHER_DRIVE_C, &vfs );
    if (card_known)
    {
        card_free = (unsigned long long)vfs.f_bavail * vfs.f_frsize;
        card_total = (unsigned long long)vfs.f_blocks * vfs.f_frsize;
    }
    usb_count = l->options->list_usb ? l->options->list_usb( volumes, LAUNCHER_MAX_USB_VOLUMES ) : 0;
    for (i = 0; i < l->program_count; i++) missing += !l->programs[i].removed && l->programs[i].missing;
    ui_start_screen( ui );
    while (ui_begin_frame( ui ))
    {
        static const struct ui_hint hints[] = { { UI_Y, "Count again" }, { UI_X, "Missing entries" }, { UI_B, "Back" }, { UI_A, "Open" } };
        unsigned long long games = 0;
        char text[240], a[40], b[40];
        int x = SH_MARGIN, w = ui->width - 2 * SH_MARGIN, y, counting = 0, at = SDL_AtomicGet( &sh_store.at );

        sh_store_pump( l );
        while (ui_poll( ui, &input ))
        {
            switch (input.button)
            {
            case UI_MINUS:
            case UI_PLUS:
                sh_global_key( l, input.button );
                if (sh.pending >= 0) { sh_store_stop(); return 0; }
                break;
            case UI_B: ui_sound( ui, LAUNCHER_SOUND_BACK ); sh_store_stop(); ui_start_screen( ui ); return 0;
            case UI_UP: if (selection > 0) selection--; break;
            case UI_DOWN: if (selection + 1 < sh_store.count) selection++; break;
            case UI_Y:
                ui_sound( ui, LAUNCHER_SOUND_ACCEPT );
                sh_store_build( l, 1 );
                ui_toast( ui, "Counting every folder again", 2500 );
                break;
            case UI_X:
                l->hide_missing = !l->hide_missing;
                save_look( l );
                ui_toast( ui, l->hide_missing ? "Entries whose file is missing are hidden from the library"
                                              : "Entries whose file is missing are shown in the library", 3000 );
                break;
            case UI_A:
                if (selection < sh_store.count && sh_store.rows[sh_store.order[selection]].program >= 0)
                {
                    int program = sh_store.rows[sh_store.order[selection]].program;

                    ui_sound( ui, LAUNCHER_SOUND_ACCEPT );
                    sh_store_stop();
                    if (sh_app_page( l, program, target, size )) return 1;
                    if (sh.pending >= 0) return 0;
                    rebuild_lists( l, -1 );
                    sh_store_build( l, 0 );
                    if (card_known && !statvfs( LAUNCHER_DRIVE_C, &vfs )) card_free = (unsigned long long)vfs.f_bavail * vfs.f_frsize;
                    if (selection >= sh_store.count) selection = sh_store.count ? sh_store.count - 1 : 0;
                    ui_start_screen( ui );
                }
                else if (selection < sh_store.count) { sh_store_stop(); sh.pending = PAGE_DOWNLOADS; return 0; }
                break;
            default: break;
            }
        }
        if (!ui->running) { sh_store_stop(); return 0; }
        if (selection < top) top = selection;
        if (selection >= top + visible) top = selection - visible + 1;
        for (i = 0; i < sh_store.count; i++)
        {
            if (sh_store.rows[i].counted && sh_store.rows[i].source != 2) games += sh_store.rows[i].bytes;
            else if (!sh_store.rows[i].shared) counting++;
        }

        sh_frame_begin( l );
        sh_top( l, sh_tr( "Storage" ), NULL );
        y = SH_TOP + 24;
        /* The card: what the games take of it, what everything else does, what is free. */
        ui_rounded( ui, x, y, w, 104, 8, SH_PANEL );
        ui_text( ui, ui->normal, x + 28, y + 16, sh_tr( "SD card" ), ui->value );
        if (card_known && card_total)
        {
            unsigned long long used = card_total - card_free, rest = used > games ? used - games : 0;
            int bar_x = x + 28, bar_w = w - 56, bar_y = y + 16 + TTF_FontHeight( ui->normal ) + 10;
            int games_w = (int)((double)games / card_total * bar_w), rest_w = (int)((double)rest / card_total * bar_w);

            snprintf( text, sizeof(text), sh_tr( "%s free of %s" ), sh_bytes( card_free, a, sizeof(a) ), sh_bytes( card_total, b, sizeof(b) ) );
            ui_text_right( ui, ui->normal, x + w - 28, y + 16, text, ui->value );
            ui_rounded( ui, bar_x, bar_y, bar_w, 12, 4, (SDL_Color){ 44, 58, 75, 255 } );
            if (rest_w + games_w > 0) ui_rounded( ui, bar_x, bar_y, games_w + rest_w, 12, 4, (SDL_Color){ 110, 124, 142, 255 } );
            if (games_w > 0) ui_rounded( ui, bar_x, bar_y, games_w < 6 ? 6 : games_w, 12, 4, SH_BLUE );
            snprintf( text, sizeof(text), sh_tr( counting ? "Games here: %s, still counting  ·  everything else on the card: %s"
                                                          : "Games here: %s  ·  everything else on the card: %s" ),
                      sh_bytes( games, a, sizeof(a) ), sh_bytes( rest, b, sizeof(b) ) );
            sh_fit( ui, ui->small, bar_x, bar_y + 20, bar_w, text, ui->dim, 0 );
        }
        else ui_text_right( ui, ui->normal, x + w - 28, y + 16, sh_tr( "no data" ), ui->dim );
        y += 104 + 10;
        for (i = 0; i < usb_count && i < 1; i++)
        {
            snprintf( text, sizeof(text), "USB %c: %s", volumes[i].drive, volumes[i].label );
            ui_text( ui, ui->small, x + 28, y, text, ui->dim );
            if (!statvfs( volumes[i].path, &vfs ))
            {
                snprintf( text, sizeof(text), sh_tr( "%s free of %s" ),
                          sh_bytes( (unsigned long long)vfs.f_bavail * vfs.f_frsize, a, sizeof(a) ),
                          sh_bytes( (unsigned long long)vfs.f_blocks * vfs.f_frsize, b, sizeof(b) ) );
                ui_text_right( ui, ui->small, x + w - 28, y, text, ui->dim );
            }
            y += TTF_FontHeight( ui->small ) + 8;
        }

        if (!sh_store.count)
            ui_text( ui, ui->normal, x + 28, y + 30, sh_tr( "No games on the card yet" ), ui->dim );
        for (i = top; i < sh_store.count && i < top + visible; i++, y += row_h)
        {
            const struct sh_store_row *row = &sh_store.rows[sh_store.order[i]];
            const char *source = row->source == 1 ? "Steam" : row->source == 2 ? "USB" : sh_tr( "Local" );
            const char *name = row->program >= 0 ? l->programs[row->program].title : sh_tr( "Unfinished downloads" );
            SDL_Color tone = ui->value;
            int right = x + w - 28;

            ui_rounded( ui, x, y, w, row_h - 8, 8, SH_PANEL );
            if (i == selection) sh_focus_ring( ui, x, y, w, row_h - 8, 8 );
            if (row->counted) sh_bytes( row->bytes, text, sizeof(text) );
            else if (row->shared == 1) { snprintf( text, sizeof(text), "%s", sh_tr( "in a shared folder" ) ); tone = ui->dim; }
            else if (row->shared) { snprintf( text, sizeof(text), "%s", sh_tr( "could not be counted" ) ); tone = SH_AMBER; }
            else { snprintf( text, sizeof(text), "%s", sh_tr( sh_store.order[i] == at ? "counting..." : "waiting to be counted" ) ); tone = ui->dim; }
            ui_text_right( ui, ui->normal, right, y + (row_h - 8 - TTF_FontHeight( ui->normal )) / 2, text, tone );
            if (row->source != 3)
                sh_chip( ui, x + 20, y + (row_h - 8 - TTF_FontHeight( ui->small ) - 8) / 2, source, row->source == 1 ? SH_GREEN : ui->dim );
            sh_fit( ui, ui->normal, x + 160, y + (row_h - 8 - TTF_FontHeight( ui->normal )) / 2, w - 160 - 300, name, ui->value, 0 );
        }
        if (sh_store.count > visible)
        {
            snprintf( text, sizeof(text), "%d / %d", selection + 1, sh_store.count );
            ui_text( ui, ui->small, x, UI_FOOTER_Y - 2, text, ui->dim );
        }
        else if (missing)
        {
            snprintf( text, sizeof(text), sh_tr( "Entries whose file is missing: %d" ), missing );
            ui_text( ui, ui->small, x, UI_FOOTER_Y - 2, text, SH_AMBER );
        }
        ui_hints_right( ui, hints, 4, ui->width - SH_MARGIN, UI_FOOTER_Y - 6 );
        ui_draw_toast( ui );
        ui->busy_until = SDL_GetTicks() + (sh_store.thread ? 250 : 1000);
        ui_present( ui );
        ui_wait( ui );
    }
    sh_store_stop();
    return 0;
}

static void sh_text_file( struct launcher *l, const char *title, const char *path, const char *empty )
{
    char text[1800];
    FILE *f = fopen( path, "rb" );
    size_t got = 0;

    if (f)
    {
        got = fread( text, 1, sizeof(text) - 1, f );
        fclose( f );
    }
    text[got] = 0;
    ui_message( &l->ui, sh_tr( title ), got ? text : sh_tr( empty ) );
    ui_start_screen( &l->ui );
}

static void sh_diagnostics( struct launcher *l )
{
    struct sh_row rows[6];
    struct app_settings s;
    char path[600];
    int selection = 0, chosen;

    for (;;)
    {
        app_settings_resolve( &s, &sh.global, NULL, NULL, NULL, NULL );
        memset( rows, 0, sizeof(rows) );
        snprintf( rows[0].label, sizeof(rows[0].label), "Mode" );
        snprintf( rows[0].value, sizeof(rows[0].value), "%s", app_setting_value_label( &s, APP_SET_DIAGNOSTICS_MODE, sh.language ) );
        rows[0].help = app_setting_def( APP_SET_DIAGNOSTICS_MODE )->purpose[sh.language];
        snprintf( rows[1].label, sizeof(rows[1].label), "Diagnostic settings" );
        rows[1].help = "Traces, the profiler, counters, snapshots and logs for every application without its own choice.";
        snprintf( rows[2].label, sizeof(rows[2].label), "Last start: what was applied" );
        rows[2].help = "The settings the last program ran with, the layer each came from and why.";
        snprintf( rows[3].label, sizeof(rows[3].label), "Last start: modules loaded" );
        rows[3].help = "The DLLs the last program loaded, with the folder each came from.";
        snprintf( rows[4].label, sizeof(rows[4].label), "Build" );
        snprintf( rows[4].value, sizeof(rows[4].value), "%s", l->options->build );
        rows[4].disabled = 1;
        chosen = sh_rows( l, "Diagnostics", NULL, rows, 5, &selection );
        if (chosen < 0) return;
        if (chosen == 0)
        {
            const struct app_setting_def *def = app_setting_def( APP_SET_DIAGNOSTICS_MODE );
            const char *items[3] = { def->choices[0].label[sh.language], def->choices[1].label[sh.language],
                                     def->choices[2].label[sh.language] };
            int mode = ui_menu( &l->ui, def->name[sh.language], items, 3, app_settings_get( &s, APP_SET_DIAGNOSTICS_MODE ) );

            ui_start_screen( &l->ui );
            if (mode >= 0 && app_settings_set_global( &sh.global, APP_SET_DIAGNOSTICS_MODE, mode ))
                wine_nx_config_save( &sh.global, sh.global_path );
        }
        else if (chosen == 1) sh_settings_screen( l, NULL );
        else if (chosen == 2)
        {
            runtime_file( l, "corkdeck/last-settings.txt", path, sizeof(path) );
            sh_text_file( l, "Last start: what was applied", path, "No program has been started yet, or diagnostics were off." );
        }
        else if (chosen == 3)
        {
            runtime_file( l, "corkdeck/last-modules.txt", path, sizeof(path) );
            sh_text_file( l, "Last start: modules loaded", path, "No program has been started yet, or diagnostics were off." );
        }
    }
}

/* Returns 1 when the shell is to close. */
static int sh_power( struct launcher *l )
{
    const char *items[3];
    int count = 0, chosen, home, restart = -1, reboot;

    items[home = count++] = sh_tr( "Close CorkDeck" );
    if (l->options->schedule_restart) items[restart = count++] = sh_tr( "Restart CorkDeck" );
    items[reboot = count++] = sh_tr( "Restart the console" );
    chosen = ui_menu( &l->ui, sh_tr( "Power" ), items, count, 0 );
    ui_start_screen( &l->ui );
    if (chosen < 0) return 0;
    if (chosen == home) return 1;
    if (chosen == restart)
    {
        if (l->options->schedule_restart()) return 1;
        ui_toast( &l->ui, "Could not restart; close CorkDeck and open it again", 4000 );
        return 0;
    }
    if (chosen == reboot &&
        ui_confirm( &l->ui, sh_tr( "Restart the console" ), sh_tr( "The console restarts now." ), sh_tr( "Restart" ) ))
    {
        l->options->reboot_requested = 1;
        return 1;
    }
    ui_start_screen( &l->ui );
    return 0;
}

/***********************************************************************
 * The two side panels
 */

static void sh_draw_root( struct launcher *l );

static int sh_main_menu( struct launcher *l )
{
    static const char *const names[PAGE_COUNT] = { "Home", "Library", "Downloads", "Settings", "Maintenance", "Storage",
                                                   "Diagnostics", "Power" };
    struct ui *ui = &l->ui;
    struct ui_input input;
    int focus = sh.page == PAGE_DIAGNOSTICS && !sh.developer ? PAGE_HOME : sh.page, i;
    Uint32 opened = SDL_GetTicks();

    while (ui_begin_frame( ui ))
    {
        float open = ui->animations ? (SDL_GetTicks() - opened) / 160.0f : 1;
        int w = 320, x, y;

        while (ui_poll( ui, &input ))
        {
            if (input.touch == UI_TOUCH_TAP)
            {
                int code = sh_hit_at( input.x, input.y );

                if (code >= 3000) { focus = code - 3000; input.button = UI_A; }
                else input.button = UI_B;
            }
            if (input.button == UI_UP && focus > 0) focus -= focus - 1 == PAGE_DIAGNOSTICS && !sh.developer ? 2 : 1;
            if (input.button == UI_DOWN && focus + 1 < PAGE_COUNT) focus += focus + 1 == PAGE_DIAGNOSTICS && !sh.developer ? 2 : 1;
            if (input.button == UI_A) { ui_sound( ui, LAUNCHER_SOUND_ACCEPT ); return focus; }
            if (input.button == UI_B || input.button == UI_MINUS) { ui_sound( ui, LAUNCHER_SOUND_BACK ); return -1; }
            if (!ui->running) return -1;
        }
        if (!ui->running) return -1;
        if (open > 1) open = 1;
        sh_draw_root( l );
        sh.hit_count = 0;
        ui_fill( ui, 0, 0, ui->width, ui->height, (SDL_Color){ 6, 9, 13, (Uint8)(160 * open) } );
        x = (int)(-w * (1 - open));
        ui_fill( ui, x, 0, w, ui->height, SH_SIDE );
        ui_fill( ui, x + w, 0, 1, ui->height, SH_LINE );
        ui_rounded( ui, x + 32, 30, 18, 24, 5, SH_CORK );
        ui_text( ui, ui->normal, x + 60, 42 - TTF_FontHeight( ui->normal ) / 2, "CorkDeck", ui->value );
        for (i = 0, y = 100; i < PAGE_COUNT; i++, y += 56)
        {
            if (i == PAGE_DIAGNOSTICS && !sh.developer) { y -= 56; continue; }
            if (i == PAGE_POWER) y = ui->height - 90;
            if (i == focus) ui_rounded( ui, x + 20, y, w - 40, 48, 8, SH_BLUE );
            ui_text( ui, ui->normal, x + 38, y + (48 - TTF_FontHeight( ui->normal )) / 2, names[i],
                     i == focus ? (SDL_Color){ 4, 18, 31, 255 } : ui->dim );
            sh_hit( x + 20, y, w - 40, 48, 3000 + i );
        }
        ui_draw_toast( ui );
        ui_present( ui );
        ui_wait( ui );
        if (open < 1) ui->deadline = SDL_GetTicks();
    }
    return -1;
}

static void sh_quick_panel( struct launcher *l )
{
    struct ui *ui = &l->ui;
    struct ui_input input;
    Uint32 opened = SDL_GetTicks();
    struct statvfs vfs;
    char free_text[48];
    int ready = launcher_dlls_is_ready( l->dlls ), online = steam_ui_online();

    if (!statvfs( LAUNCHER_DRIVE_C, &vfs ))
        snprintf( free_text, sizeof(free_text), sh_tr( "%.1f GB" ), (double)vfs.f_bavail * vfs.f_frsize / 1073741824.0 );
    else snprintf( free_text, sizeof(free_text), "%s", sh_tr( "no data" ) );
    while (ui_begin_frame( ui ))
    {
        float open = ui->animations ? (SDL_GetTicks() - opened) / 160.0f : 1;
        struct steam_download_view d;
        char text[200];
        int w = 400, x, y, i, hour, minute, battery, charging, status, active;

        while (ui_poll( ui, &input ))
        {
            if (input.button == UI_A && steam_ui_download( &d ) && steam_download_active( d.state ))
            {
                steam_ui_stop();
                ui_toast( ui, "The download is being cancelled; the unfinished files are removed", 3000 );
            }
            else if (input.button == UI_B || input.button == UI_PLUS || input.touch == UI_TOUCH_TAP)
            {
                ui_sound( ui, LAUNCHER_SOUND_BACK );
                return;
            }
            if (!ui->running) return;
        }
        if (!ui->running) return;
        if (open > 1) open = 1;
        sh_draw_root( l );
        ui_fill( ui, 0, 0, ui->width, ui->height, (SDL_Color){ 6, 9, 13, (Uint8)(160 * open) } );
        x = ui->width - (int)(w * open);
        ui_fill( ui, x, 0, w, ui->height, SH_SIDE );
        ui_fill( ui, x - 1, 0, 1, ui->height, SH_LINE );
        y = 34;
        ui_text( ui, ui->small, x + 26, y, "NOW", ui->dim );
        y += TTF_FontHeight( ui->small ) + 8;
        steam_ui_download( &d );
        active = steam_download_active( d.state );
        ui_rounded( ui, x + 20, y, w - 40, active ? 104 : 52, 10, SH_PANEL );
        if (active)
        {
            int percent = d.total ? (int)(d.downloaded * 100 / d.total) : 0;

            snprintf( text, sizeof(text), "%s · %d%%", sh_owned_name( d.app_id ), percent );
            sh_fit( ui, ui->small, x + 36, y + 12, w - 72, text, ui->text, 0 );
            ui_rounded( ui, x + 36, y + 44, w - 72, 6, 3, SH_LINE );
            ui_rounded( ui, x + 36, y + 44, (w - 72) * percent / 100 > 6 ? (w - 72) * percent / 100 : 6, 6, 3, SH_BLUE );
            snprintf( text, sizeof(text), sh_tr( "%.1f MB/s · A stops it" ), d.bytes_per_second / 1048576.0 );
            ui_text( ui, ui->small, x + 36, y + 62, text, ui->dim );
            y += 116;
        }
        else
        {
            ui_text( ui, ui->small, x + 36, y + 14, "Nothing is downloading", ui->dim );
            y += 64;
        }
        ui_text( ui, ui->small, x + 26, y, "STATE", ui->dim );
        y += TTF_FontHeight( ui->small ) + 8;
        ui_rounded( ui, x + 20, y, w - 40, 4 * (TTF_FontHeight( ui->small ) + 8) + 16, 10, SH_PANEL );
        status = sh_status( &hour, &minute, &battery, &charging );
        for (i = 0; i < 4; i++)
        {
            const char *label = i == 0 ? "Battery" : i == 1 ? "Steam" : i == 2 ? "Free on the card" : "Windows components";
            SDL_Color color = ui->dim;

            if (i == 0)
            {
                if (status & LAUNCHER_STATUS_BATTERY) snprintf( text, sizeof(text), "%d%%%s", battery, charging ? " +" : "" );
                else snprintf( text, sizeof(text), "%s", sh_tr( "no data" ) );
            }
            else if (i == 1)
            {
                snprintf( text, sizeof(text), "%s", sh_tr( online ? "signed in" : "not signed in" ) );
                if (online) color = SH_GREEN;
            }
            else if (i == 2) snprintf( text, sizeof(text), "%s", free_text );
            else
            {
                snprintf( text, sizeof(text), "%s", sh_tr( ready ? "ready" : "to download" ) );
                color = ready ? SH_GREEN : SH_AMBER;
            }
            ui_text( ui, ui->small, x + 36, y + 10 + i * (TTF_FontHeight( ui->small ) + 8), label, ui->text );
            ui_text_right( ui, ui->small, x + w - 36, y + 10 + i * (TTF_FontHeight( ui->small ) + 8), text, color );
        }
        y += 4 * (TTF_FontHeight( ui->small ) + 8) + 30;
        ui_text( ui, ui->small, x + 26, y, "NOTIFICATIONS", ui->dim );
        y += TTF_FontHeight( ui->small ) + 8;
        if (!ui->note_count) ui_text( ui, ui->small, x + 36, y + 6, "Nothing yet", ui->dim );
        for (i = 0; i < ui->note_count && y + 58 < ui->height; i++)
        {
            ui_rounded( ui, x + 20, y, w - 40, 52, 10, SH_PANEL );
            if (ui->notes[i].hour >= 0)
            {
                snprintf( text, sizeof(text), "%02d:%02d", ui->notes[i].hour, ui->notes[i].minute );
                ui_text_right( ui, ui->small, x + w - 36, y + 14, text, ui->dim );
            }
            sh_fit( ui, ui->small, x + 36, y + 14, w - 150, ui->notes[i].text, ui->text, 0 );
            y += 58;
        }
        ui_present( ui );
        ui_wait( ui );
        if (open < 1 || active) ui->deadline = SDL_GetTicks() + (active ? 500 : 0);
    }
}

/***********************************************************************
 * Home and the library
 */

/* What needs a person's attention, newest reason first. Returns how many lines. */
static int sh_attention( struct launcher *l, char lines[4][200], int actions[4], SDL_Color colors[4] )
{
    struct steam_download_view d;
    int count = 0, i;

    sh_download( &d );
    if (steam_download_active( d.state ) && d.total)
    {
        snprintf( lines[count], 200, sh_tr( "%s: downloading, %d%%" ), sh_owned_name( d.app_id ),
                  (int)(d.downloaded * 100 / d.total) );
        colors[count] = SH_BLUE;
        actions[count++] = -1;
    }
    else if (d.state == STEAM_DOWNLOAD_ERROR && d.app_id)
    {
        snprintf( lines[count], 200, sh_tr( "%s: the download stopped with an error" ), sh_owned_name( d.app_id ) );
        colors[count] = SH_AMBER;
        actions[count++] = -1;
    }
    if (sh_probe_text[0] == 'C')
    {
        snprintf( lines[count], 200, "%.199s", sh_probe_text );
        colors[count] = SH_AMBER;
        actions[count++] = -1;
    }
    if (!launcher_dlls_is_ready( l->dlls ))
    {
        snprintf( lines[count], 200, "%s", sh_tr( "Windows components are not installed: programs cannot start yet" ) );
        colors[count] = SH_AMBER;
        actions[count++] = -2;
    }
    if (l->options->address_space_bits != 39)
    {
        snprintf( lines[count], 200, "%s", sh_tr( "CorkDeck was not started from its HOME menu entry: programs cannot start" ) );
        colors[count] = SH_AMBER;
        actions[count++] = -3;
    }
    for (i = 0; i < l->program_count && count < 4; i++)
        if (!l->programs[i].removed && l->programs[i].missing)
        {
            snprintf( lines[count], 200, sh_tr( "%s: the program's file is missing" ), l->programs[i].title );
            colors[count] = SH_AMBER;
            actions[count++] = i;
        }
    return count;
}

static void sh_draw_home( struct launcher *l )
{
    struct ui *ui = &l->ui;
    static const struct ui_hint hints[] = { { UI_MINUS, "Menu" }, { UI_PLUS, "Panel" }, { UI_Y, "Search" },
                                            { UI_X, "Actions" }, { UI_A, "Open" } };
    char lines[4][200], text[256], length[48], date[40];
    int actions[4], favorites[5] = {0}, recents, favorite_count = 0, attention, i, x;
    SDL_Color colors[4];

    sh_frame_begin( l );
    sh_top( l, sh_tr( "Home" ), NULL );
    recents = l->history_count < 4 ? l->history_count : 4;
    for (i = 0; i < l->program_count && favorite_count < 5; i++)
        if (!l->programs[i].removed && l->programs[i].favorite) favorites[favorite_count++] = i;
    attention = sh_attention( l, lines, actions, colors );
    if (sh.home_row == 0 && !recents) sh.home_row = 1;
    if (sh.home_row == 1 && !favorite_count && !attention) sh.home_row = 0;
    if (sh.home_row == 0 && sh.home_col >= recents) sh.home_col = recents ? recents - 1 : 0;
    if (sh.home_row == 1 && sh.home_col >= favorite_count + attention)
        sh.home_col = favorite_count + attention ? favorite_count + attention - 1 : 0;

    ui_text( ui, ui->small, SH_MARGIN, SH_TOP + 18, "RECENT", ui->dim );
    if (!recents)
    {
        ui_text( ui, ui->large, SH_MARGIN, 190, "Nothing has been started yet", ui->value );
        ui_text_wrapped( ui, ui->small, SH_MARGIN, 190 + TTF_FontHeight( ui->large ) + 12, 700, 3,
                         "Open the library from the main menu (-) to add a program from the card or to sign in to Steam "
                         "and install your games.", ui->dim, 0 );
    }
    else
    {
        int shown = sh.home_row == 0 ? sh.home_col : 0, index = l->history[shown];
        struct program *p = &l->programs[index];
        struct app_record *record = sh_record( l, index );
        int tx = SH_MARGIN + 198 + 28, ty = 232;

        sh_card( l, l->history[0], (SDL_Rect){ SH_MARGIN, 124, 198, 297 }, sh.home_row == 0 && sh.home_col == 0, 4000 );
        sh_fit( ui, ui->large, tx, ty, 370, p->title, ui->value, 0 );
        ty += TTF_FontHeight( ui->large ) + 8;
        if (record->seconds)
            snprintf( text, sizeof(text), "%s · %s", sh_date( record->last_start, date, sizeof(date) ),
                      sh_duration( record->seconds, length, sizeof(length) ) );
        else snprintf( text, sizeof(text), "%s", sh_tr( "No session recorded yet" ) );
        sh_fit( ui, ui->small, tx, ty, 370, text, ui->dim, 0 );
        ty += TTF_FontHeight( ui->small ) + 12;
        x = tx;
        x += sh_chip( ui, x, ty, sh_tr( p->missing ? "File missing" : "Ready to start" ), p->missing ? SH_AMBER : SH_GREEN );
        x += sh_chip( ui, x, ty, sh_is_steam( l, index ) ? "Steam" : sh_tr( "Local" ), ui->dim );
        sh_chip( ui, x, ty, p->machine == 0x8664 ? "x64" : p->machine == 0x014c ? "x86" : "ARM64", ui->dim );
        for (i = 1; i < recents; i++)
            sh_card( l, l->history[i], (SDL_Rect){ 668 + (i - 1) * 176, 187, 156, 234 },
                     sh.home_row == 0 && sh.home_col == i, 4000 + i );
    }

    ui_text( ui, ui->small, SH_MARGIN, 448, favorite_count ? "FAVOURITES" : "NEEDS ATTENTION", ui->dim );
    for (i = 0; i < favorite_count; i++)
        sh_card( l, favorites[i], (SDL_Rect){ SH_MARGIN + i * 124, 480, 108, 162 },
                 sh.home_row == 1 && sh.home_col == i, 4100 + i );
    x = SH_MARGIN + favorite_count * 124 + (favorite_count ? 12 : 0);
    if (attention)
    {
        int w = ui->width - SH_MARGIN - x, line = 38;

        ui_rounded( ui, x, 480, w, 14 + attention * line, 10, SH_PANEL );
        for (i = 0; i < attention; i++)
        {
            int focused = sh.home_row == 1 && sh.home_col == favorite_count + i, y = 487 + i * line;

            if (focused) ui_rounded( ui, x + 6, y, w - 12, line, 8, SH_PANEL2 );
            ui_fill_circle( ui, x + 24, y + line / 2, 5, colors[i] );
            sh_fit( ui, ui->small, x + 42, y + (line - TTF_FontHeight( ui->small )) / 2, w - 60, lines[i], ui->text, focused );
            if (focused) sh_focus_ring( ui, x + 6, y, w - 12, line, 8 );
            sh_hit( x + 6, y, w - 12, line, 4100 + favorite_count + i );
        }
    }
    else if (!favorite_count)
        ui_text( ui, ui->small, SH_MARGIN, 486, "Everything is in order.", ui->dim );
    ui_hints_right( ui, hints, 5, ui->width - SH_MARGIN, UI_FOOTER_Y - 6 );
    ui_draw_toast( ui );
}

static void sh_draw_library( struct launcher *l )
{
    struct ui *ui = &l->ui;
    static const char *const tabs[TAB_COUNT] = { "All", "Favourites", "Installed ", "Not installed ", "Local", "Category" };
    static const char *const sorts[SORT_COUNT] = { "By title", "By recent", "By added", "By time played" };
    static const struct ui_hint hints[] = { { UI_ZL, "" }, { UI_ZR, "Page" }, { UI_Y, "Search" }, { UI_X, "Actions" },
                                            { UI_B, "Home" }, { UI_A, "Open" } };
    char text[200];
    int i, x = SH_MARGIN, y = SH_TOP + 14, columns = sh.list_view ? 1 : 7, rows_visible = sh.list_view ? 8 : 2;
    int first_row, w;

    sh_frame_begin( l );
    snprintf( text, sizeof(text), "%s · %d", sh_tr( "Library" ), sh.count );
    sh_top( l, text, l->search[0] ? l->search : NULL );
    for (i = 0; i < TAB_COUNT; i++)
    {
        const char *name = i == TAB_CATEGORY && sh.category[0] ? sh.category : sh_tr( tabs[i] );

        w = ui_text_width( ui, ui->small, name ) + 30;
        if (i == sh.tab) ui_rounded( ui, x, y, w, 38, 6, ui->text );
        ui_text( ui, ui->small, x + 15, y + (38 - TTF_FontHeight( ui->small )) / 2, name,
                 i == sh.tab ? (SDL_Color){ 14, 20, 27, 255 } : ui->dim );
        if (!sh.zone && sh.tool == 0 && i == sh.tab) sh_focus_ring( ui, x, y, w, 38, 19 );
        sh_hit( x, y, w, 38, 5000 + i );
        x += w + 6;
    }
    x = ui->width - SH_MARGIN;
    for (i = 2; i >= 1; i--)
    {
        const char *name = i == 1 ? sorts[sh.sort] : sh.list_view ? "As a list" : "As a grid";

        w = ui_text_width( ui, ui->small, name ) + 28;
        x -= w;
        ui_rounded( ui, x, y, w, 38, 8, SH_PANEL );
        ui_text( ui, ui->small, x + 14, y + (38 - TTF_FontHeight( ui->small )) / 2, name, ui->dim );
        if (!sh.zone && sh.tool == i) sh_focus_ring( ui, x, y, w, 38, 8 );
        sh_hit( x, y, w, 38, 5100 + i );
        x -= 10;
    }

    y = SH_TOP + 72;
    if (!sh.count)
    {
        const char *title = "Nothing here yet", *detail = "X adds a program from the card. Steam games appear once the account is signed in (Maintenance > Steam account).";

        if (l->search[0]) { title = "Nothing matches the search"; detail = "Y changes the search; an empty search shows everything."; }
        else if (sh.tab == TAB_AVAILABLE && !steam_ui_online()) { title = "Steam is not signed in"; detail = "Press A to sign in: scan the code with the Steam app on a phone, or enter the account's name and password."; }
        else if (sh.tab == TAB_AVAILABLE) { title = "No Steam games to install"; detail = "The account's library is loading, or everything in it is installed already."; }
        else if (sh.tab == TAB_CATEGORY && !sh.category[0]) { title = "No categories yet"; detail = "A category is a list of your own. Press A to make the first one, then add applications to it from a list."; }
        else if (sh.tab == TAB_CATEGORY) { title = "This category is empty"; detail = "Press A to add applications to it from a list."; }
        ui_text( ui, ui->large, SH_MARGIN, y + 60, title, ui->value );
        ui_text_wrapped( ui, ui->small, SH_MARGIN, y + 60 + TTF_FontHeight( ui->large ) + 12, 820, 3, detail, ui->dim, 0 );
    }
    first_row = sh.selection / columns;
    if (first_row < sh.top) sh.top = first_row;
    if (first_row >= sh.top + rows_visible) sh.top = first_row - rows_visible + 1;
    for (i = sh.top * columns; i < sh.count && i < (sh.top + rows_visible + (sh.list_view ? 0 : 1)) * columns; i++)
    {
        int focused = sh.zone && i == sh.selection, item = sh.items[i];

        if (sh.list_view)
        {
            int ry = y + (i - sh.top) * 62;
            const char *state = item < 0 ? "Not installed" : l->programs[item].missing ? "File missing"
                                : sh_is_steam( l, item ) ? "Steam" : "Local";
            char length[48];

            ui_rounded( ui, SH_MARGIN, ry, ui->width - 2 * SH_MARGIN, 56, 8, focused ? SH_PANEL2 : SH_PANEL );
            sh_fit( ui, ui->small, SH_MARGIN + 18, ry + (56 - TTF_FontHeight( ui->small )) / 2, 620,
                         sh_item_title( l, item ), item < 0 ? ui->dim : ui->text, focused );
            if (item >= 0 && sh_record( l, item )->seconds)
                ui_text_right( ui, ui->small, ui->width - SH_MARGIN - 220, ry + (56 - TTF_FontHeight( ui->small )) / 2,
                               sh_duration( sh_record( l, item )->seconds, length, sizeof(length) ), ui->dim );
            ui_text_right( ui, ui->small, ui->width - SH_MARGIN - 18, ry + (56 - TTF_FontHeight( ui->small )) / 2, state,
                           item >= 0 && l->programs[item].missing ? SH_AMBER : ui->dim );
            if (focused) sh_focus_ring( ui, SH_MARGIN, ry, ui->width - 2 * SH_MARGIN, 56, 8 );
            sh_hit( SH_MARGIN, ry, ui->width - 2 * SH_MARGIN, 56, 6000 + i );
        }
        else
        {
            int cw = 150, ch = 225, gap = 17, column = i % columns, row = i / columns - sh.top;
            SDL_Rect r = { SH_MARGIN + column * (cw + gap), y + row * (ch + 22), cw, ch };

            if (r.y + 40 > ui->height - 44) continue;
            sh_card( l, item, r, focused, 6000 + i );
        }
    }
    ui_hints_right( ui, hints, 6, ui->width - SH_MARGIN, UI_FOOTER_Y - 6 );
    ui_draw_toast( ui );
}

static void sh_draw_root( struct launcher *l )
{
    if (sh.page == PAGE_HOME) sh_draw_home( l );
    else sh_draw_library( l );
}

/* What stays on the screen while a program starts: its wide picture behind,
 * its cover and its name, and that it is starting. */
static void sh_start_screen( struct launcher *l, struct program *p )
{
    struct ui *ui = &l->ui;
    int x = SH_MARGIN + 40, cover_w = 220, cover_h = 330, y = (ui->height - cover_h) / 2, tx;

    ui_background( ui );
    if (p->hero_icon)
    {
        SDL_Rect all = { 0, 0, ui->width, ui->height };

        ui_rounded_texture( ui, p->hero_icon, NULL, all, 0, (SDL_Color){ 110, 120, 135, 255 } );
        ui_gradient( ui, 0, 0, ui->width, ui->height, (SDL_Color){ 14, 20, 27, 235 }, (SDL_Color){ 14, 20, 27, 120 }, 1 );
    }
    if (p->icon && p->icon_is_art) draw_cover( ui, p, (SDL_Rect){ x, y, cover_w, cover_h }, 6, 255, 255 );
    else sh_blank_cover( ui, p->title, (SDL_Rect){ x, y, cover_w, cover_h }, 0 );
    tx = x + cover_w + 44;
    sh_fit( ui, ui->large, tx, y + 96, ui->width - tx - SH_MARGIN, p->title, ui->value, 0 );
    ui_text( ui, ui->normal, tx, y + 96 + TTF_FontHeight( ui->large ) + 14, "Starting...", SH_BLUE );
    ui_text( ui, ui->small, tx, y + 96 + TTF_FontHeight( ui->large ) + 14 + TTF_FontHeight( ui->normal ) + 10,
             "The picture appears when the program draws its first frame.", ui->dim );
    ui_rounded( ui, ui->width - SH_MARGIN - 150, ui->height - 60, 18, 24, 5, SH_CORK );
    ui_text( ui, ui->normal, ui->width - SH_MARGIN - 122, ui->height - 48 - TTF_FontHeight( ui->normal ) / 2, "CorkDeck", ui->dim );
}

/* Minus opens the main menu and Plus the quick panel, from any screen. Returns
 * 1 when the menu chose a place to go, which is then waiting in sh.pending. */
static int sh_global_key( struct launcher *l, int button )
{
    int destination;

    if (button == UI_PLUS)
    {
        sh_quick_panel( l );
        ui_start_screen( &l->ui );
        return 0;
    }
    destination = sh_main_menu( l );
    ui_start_screen( &l->ui );
    if (destination < 0) return 0;
    sh.pending = destination;
    return 1;
}

static int sh_list_key( void *data, int button )
{
    return sh_global_key( data, button );
}

static void sh_search( struct launcher *l )
{
    edit_text( sh_tr( "Search the library (empty shows everything)" ), l->search, sizeof(l->search) );
    sh.page = PAGE_LIBRARY;
    sh.tab = TAB_ALL;
    sh.zone = 1;
    sh.selection = sh.top = 0;
    sh_rebuild( l, -1 );
}

static void sh_add( struct launcher *l )
{
    int added = add_game( l );

    if (added >= 0)
    {
        rebuild_lists( l, added );
        sh_rebuild( l, added );
        sh.page = PAGE_LIBRARY;
        sh.zone = 1;
    }
    ui_start_screen( &l->ui );
}

/* Opens what is in focus. Returns 1 with target set when a program starts. */
static int sh_open_item( struct launcher *l, int item, char *target, size_t size )
{
    int started;

    if (item < 0)
    {
        steam_ui_game( &l->ui, l->options->runtime_dir, &sh.owned[-1 - item] );
        sh_read_owned( l, 1 );
        rebuild_lists( l, -1 );
        sh_rebuild( l, -1 );
        ui_start_screen( &l->ui );
        return 0;
    }
    started = sh_app_page( l, item, target, size );
    if (!started)
    {
        rebuild_lists( l, item );
        sh_rebuild( l, item );
    }
    return started;
}

/* Choose the category the tab shows, or make a new one. */
static void sh_category_choose( struct launcher *l )
{
    static char names[16][48];
    struct sh_row rows[18];
    int n = 0, i, j, selection = 0, chosen;

    for (i = 0; i < l->program_count && n < 16; i++)
    {
        const char *c = sh_record( l, i )->category;

        if (l->programs[i].removed || !c[0]) continue;
        for (j = 0; j < n; j++) if (!strcasecmp( names[j], c )) break;
        if (j == n) snprintf( names[n++], sizeof(names[0]), "%s", c );
    }
    if (sh.category[0])
    {
        for (j = 0; j < n; j++) if (!strcasecmp( names[j], sh.category )) break;
        if (j == n && n < 16) snprintf( names[n++], sizeof(names[0]), "%s", sh.category );
    }
    memset( rows, 0, sizeof(rows) );
    for (i = 0; i < n; i++) snprintf( rows[i].label, sizeof(rows[i].label), "%s", names[i] );
    snprintf( rows[n].label, sizeof(rows[n].label), "New category..." );
    chosen = sh_rows( l, "Categories", "Choose one to show, or make a new one", rows, n + 1, &selection );
    if (chosen < 0) return;
    if (chosen == n)
    {
        char name[48] = "";

        if (!launcher_platform_prompt( sh_tr( "Name of the new category" ), "", name, sizeof(name) ) || !name[0]) return;
        snprintf( sh.category, sizeof(sh.category), "%s", name );
    }
    else snprintf( sh.category, sizeof(sh.category), "%s", names[chosen] );
    sh.tab = TAB_CATEGORY;
    sh.selection = sh.top = 0;
}

/* Put an application into the category shown, from a list of those not in it. */
static void sh_category_add( struct launcher *l )
{
    struct sh_row rows[24];
    int indexes[24], n = 0, i, selection = 0, chosen;

    if (!sh.category[0]) { sh_category_choose( l ); if (!sh.category[0]) return; }
    for (;;)
    {
        memset( rows, 0, sizeof(rows) );
        for (i = 0, n = 0; i < l->program_count && n < 24; i++)
        {
            if (l->programs[i].removed || !strcasecmp( sh_record( l, i )->category, sh.category )) continue;
            snprintf( rows[n].label, sizeof(rows[n].label), "%s", l->programs[i].title );
            snprintf( rows[n].value, sizeof(rows[n].value), "%s", sh_record( l, i )->category );
            indexes[n++] = i;
        }
        if (!n)
        {
            ui_message( &l->ui, sh.category, sh_tr( "Every application of the library is in this category already." ) );
            return;
        }
        chosen = sh_rows( l, sh.category, "Choose what to add; B when done", rows, n, &selection );
        if (chosen < 0) return;
        i = indexes[chosen];
        if (!l->programs[i].catalog_id) save_library( l );
        sh_record( l, i )->id = l->programs[i].catalog_id;
        snprintf( sh_record( l, i )->category, sizeof(sh.records[0].category), "%s", sh.category );
        if (!app_record_save( l->options->runtime_dir, sh_record( l, i ) ))
            ui_toast( &l->ui, "Could not save the category", 2500 );
        else ui_toast( &l->ui, "Added to the category", 1500 );
        if (selection >= n - 1 && selection > 0) selection--;
    }
}

static void sh_item_menu( struct launcher *l, int item )
{
    const char *items[7];
    int chosen, gone = 0, count = 0, add, search, category = -1, manage = -1, fill = -1, take = -1;

    if (sh.page == PAGE_LIBRARY && sh.tab == TAB_CATEGORY)
    {
        items[fill = count++] = sh_tr( "Add applications to this category" );
        if (item >= 0) items[take = count++] = sh_tr( "Take this application out of the category" );
    }
    if (item >= 0) items[manage = count++] = sh_tr( "Manage this application" );
    items[add = count++] = sh_tr( "Add a program from the card" );
    items[search = count++] = sh_tr( "Search" );
    if (sh.page == PAGE_LIBRARY) items[category = count++] = sh_tr( "Choose or create a category" );
    chosen = ui_menu( &l->ui, sh_tr( "Actions" ), items, count, 0 );
    ui_start_screen( &l->ui );
    if (chosen < 0) return;
    if (chosen == manage) sh_manage( l, item, &gone );
    else if (chosen == add) sh_add( l );
    else if (chosen == search) sh_search( l );
    else if (chosen == category) sh_category_choose( l );
    else if (chosen == fill) sh_category_add( l );
    else if (chosen == take)
    {
        sh_record( l, item )->category[0] = 0;
        sh_record( l, item )->id = l->programs[item].catalog_id;
        app_record_save( l->options->runtime_dir, sh_record( l, item ) );
    }
    rebuild_lists( l, item );
    sh_rebuild( l, item );
}

static int shell_run( struct launcher *l, char *target, size_t size )
{
    struct ui *ui = &l->ui;
    struct ui_input input;

    memset( &sh, 0, sizeof(sh) );
    sh_global_load( l );
    sh_apply_global( l );
    ui->translate = shell_translate;
    ui->note_clock = sh_note_clock;
    sh.page = l->history_count ? PAGE_HOME : PAGE_LIBRARY;
    sh.pending = -1;
    ui->trace = wine_nx_runtime_trace;
    launcher_log( "[SHELL] started: %d programs, language %s, Windows DLLs %s", l->program_count,
                  sh.language == APP_LANG_RU ? "ru" : "en", launcher_dlls_is_ready( l->dlls ) ? "ready" : "not ready" );
    launcher_start_screen = sh_start_screen;
    ui->global_key = sh_list_key;
    ui->global_data = l;
    sh.zone = 1;
    sh.sort = SORT_RECENT;
    sh_card_probe( l, "start" );
    {
        /* Builds 0.1.8b to 0.2b could give a program an overclocking profile, written into
         * Horizon OC as this application's own. CorkDeck sets no clocks now and does not
         * touch Horizon OC even to take that back: it says so once and leaves it to the owner. */
        char mark[600];

        runtime_file( l, "corkdeck/clock-profile.txt", mark, sizeof(mark) );
        if (file_exists( mark ))
        {
            launcher_log( "[SHELL] an overclocking profile of an earlier build may still be in Horizon OC; telling the owner once" );
            ui_message( ui, sh_tr( "Overclocking" ),
                        sh_tr( "An earlier CorkDeck could set an overclocking profile for a game, and one may still be stored in Horizon OC for the application CorkDeck. CorkDeck no longer changes the console's clocks at all. To be sure only your own profile applies, open the Horizon OC overlay and reset the profile of the application CorkDeck." ) );
            remove( mark );
            ui_start_screen( ui );
        }
    }
    sh_rebuild( l, -1 );
    sh_card_probe( l, "after the lists" );
    ui_start_screen( ui );
    while (ui_begin_frame( ui ))
    {
        if (l->prerequisite_target[0])
        {
            struct program prerequisite;
            char requested[512];

            snprintf( requested, sizeof(requested), "%s", l->prerequisite_target );
            l->prerequisite_target[0] = 0;
            if (describe_program( l, &prerequisite, requested ))
            {
                prerequisite.settings.d3d = LAUNCHER_D3D_WINE;
                if (start_program( l, &prerequisite, target, size )) { sh_art_stop(); ui->global_key = NULL; return 1; }
            }
        }
        pump_icons( l );
        sh_art_pump( l );
        while (ui_poll( ui, &input ))
        {
            int columns = sh.list_view ? 1 : 7, page_rows = sh.list_view ? 8 : 2, destination = -2, item = -1000;
            int recents = l->history_count < 4 ? l->history_count : 4;

            if (input.touch == UI_TOUCH_TAP)
            {
                int code = sh_hit_at( input.x, input.y );

                if (code >= 6000) { sh.zone = 1; if (sh.selection == code - 6000) input.button = UI_A; else sh.selection = code - 6000; }
                else if (code >= 5100) { sh.zone = 0; sh.tool = code - 5100; input.button = UI_A; }
                else if (code >= 5000) { sh.tab = code - 5000; sh.zone = 0; sh.tool = 0; sh.selection = sh.top = 0; sh_rebuild( l, -1 ); }
                else if (code >= 4100) { if (sh.home_row == 1 && sh.home_col == code - 4100) input.button = UI_A; sh.home_row = 1; sh.home_col = code - 4100; }
                else if (code >= 4000) { if (sh.home_row == 0 && sh.home_col == code - 4000) input.button = UI_A; sh.home_row = 0; sh.home_col = code - 4000; }
            }
            else if (sh.page == PAGE_LIBRARY && input.touch == UI_TOUCH_SCROLL_UP && sh.selection + columns < sh.count) sh.selection += columns;
            else if (sh.page == PAGE_LIBRARY && input.touch == UI_TOUCH_SCROLL_DOWN && sh.selection >= columns) sh.selection -= columns;

            if (sh.page == PAGE_HOME)
            {
                if (sh.home_row == 0 && sh.home_col < recents) item = l->history[sh.home_col];
                else if (sh.home_row == 1)
                {
                    int favorites[5], count = 0, i;

                    for (i = 0; i < l->program_count && count < 5; i++)
                        if (!l->programs[i].removed && l->programs[i].favorite) favorites[count++] = i;
                    if (sh.home_col < count) item = favorites[sh.home_col];
                    else item = -2000 - (sh.home_col - count);
                }
            }
            else if (sh.zone && sh.selection < sh.count) item = sh.items[sh.selection];

            switch (input.button)
            {
            case UI_LEFT:
                if (sh.page == PAGE_HOME) { if (sh.home_col > 0) sh.home_col--; }
                else if (!sh.zone) { if (sh.tool) sh.tool--; else if (sh.tab > 0) { sh.tab--; sh.selection = sh.top = 0; sh_rebuild( l, -1 ); } }
                else if (sh.selection % columns) sh.selection--;
                break;
            case UI_RIGHT:
                if (sh.page == PAGE_HOME) sh.home_col++;
                else if (!sh.zone) { if (sh.tool == 0 && sh.tab + 1 < TAB_COUNT) { sh.tab++; sh.selection = sh.top = 0; sh_rebuild( l, -1 ); } else if (sh.tool < 2) sh.tool++; }
                else if (sh.selection % columns != columns - 1 && sh.selection + 1 < sh.count) sh.selection++;
                break;
            case UI_UP:
                if (sh.page == PAGE_HOME) sh.home_row = 0;
                else if (sh.zone && sh.selection >= columns) sh.selection -= columns;
                else sh.zone = 0;
                break;
            case UI_DOWN:
                if (sh.page == PAGE_HOME) { sh.home_row = 1; }
                else if (!sh.zone) { if (sh.count) sh.zone = 1; }
                else if (sh.selection + columns < sh.count) sh.selection += columns;
                else if (sh.selection / columns < (sh.count - 1) / columns) sh.selection = sh.count - 1;
                break;
            case UI_L:
            case UI_R:
                if (sh.page == PAGE_HOME) { sh.page = PAGE_LIBRARY; ui_start_screen( ui ); }
                else
                {
                    int next = sh.tab + (input.button == UI_L ? -1 : 1);

                    if (next >= 0 && next < TAB_COUNT) { sh.tab = next; sh.selection = sh.top = 0; sh_rebuild( l, -1 ); }
                }
                break;
            case UI_ZL:
            case UI_ZR:
                if (sh.page == PAGE_LIBRARY && sh.count)
                {
                    int step = columns * page_rows * (input.button == UI_ZL ? -1 : 1), next = sh.selection + step;

                    sh.selection = next < 0 ? 0 : next >= sh.count ? sh.count - 1 : next;
                    sh.zone = 1;
                }
                break;
            case UI_A:
                ui_sound( ui, LAUNCHER_SOUND_ACCEPT );
                if (sh.page == PAGE_LIBRARY && sh.tab == TAB_CATEGORY && !sh.count)
                {
                    sh_category_add( l );
                    rebuild_lists( l, -1 );
                    sh_rebuild( l, -1 );
                    ui_start_screen( ui );
                }
                else if (sh.page == PAGE_LIBRARY && sh.tab == TAB_AVAILABLE && !sh.count && !steam_ui_online())
                {
                    steam_ui_sign_in( ui, l->options->runtime_dir );
                    sh_read_owned( l, 1 );
                    sh_rebuild( l, -1 );
                    ui_start_screen( ui );
                }
                else if (sh.page == PAGE_LIBRARY && !sh.zone)
                {
                    if (sh.tool == 1) { sh.sort = (sh.sort + 1) % SORT_COUNT; sh_rebuild( l, -1 ); }
                    else if (sh.tool == 2) { sh.list_view = !sh.list_view; sh.top = 0; }
                    else if (sh.count) sh.zone = 1;
                }
                else if (item <= -2000)
                {
                    char lines[4][200];
                    int actions[4], n;
                    SDL_Color colors[4];

                    n = sh_attention( l, lines, actions, colors );
                    if (-2000 - item < n)
                    {
                        int action = actions[-2000 - item];

                        if (action == -1) sh_downloads( l );
                        else if (action == -2) { launcher_dlls_open( l->dlls ); ui_start_screen( ui ); }
                        else if (action == -3) sh_maintenance( l );
                        else if (sh_open_item( l, action, target, size )) { sh_art_stop(); ui->global_key = NULL; return 1; }
                    }
                }
                else if (item != -1000 && sh_open_item( l, item, target, size )) { sh_art_stop(); ui->global_key = NULL; return 1; }
                break;
            case UI_X: sh_item_menu( l, item > -1000 && item >= 0 ? item : -1 ); break;
            case UI_Y: sh_search( l ); break;
            case UI_B:
                ui_sound( ui, LAUNCHER_SOUND_BACK );
                if (sh.page == PAGE_LIBRARY)
                {
                    if (l->search[0]) { l->search[0] = 0; sh_rebuild( l, -1 ); }
                    else if (l->history_count) { sh.page = PAGE_HOME; ui_start_screen( ui ); }
                }
                break;
            case UI_PLUS: sh_quick_panel( l ); break;
            case UI_MINUS: destination = sh_main_menu( l ); break;
            }
            if (sh.pending >= 0) destination = sh.pending;
            /* A screen the menu was opened from may send on to another: go until nothing is waiting. */
            for (; destination >= 0; destination = sh.pending)
            {
                sh.pending = -1;
                ui_start_screen( ui );
                if (destination == PAGE_HOME || destination == PAGE_LIBRARY) sh.page = destination;
                else if (destination == PAGE_DOWNLOADS) sh_downloads( l );
                else if (destination == PAGE_SETTINGS) sh_settings_screen( l, NULL );
                else if (destination == PAGE_MAINTENANCE) sh_maintenance( l );
                else if (destination == PAGE_STORAGE && sh_storage( l, target, size )) { sh_art_stop(); ui->global_key = NULL; return 1; }
                else if (destination == PAGE_DIAGNOSTICS) sh_diagnostics( l );
                else if (destination == PAGE_POWER && sh_power( l )) { sh_art_stop(); ui->global_key = NULL; return 0; }
                rebuild_lists( l, -1 );
                sh_rebuild( l, -1 );
                ui_start_screen( ui );
            }
            if (!ui->running) { sh_art_stop(); ui->global_key = NULL; return 0; }
        }
        if (!ui->running) break;
        sh_card_probe( l, "tick" );
        if (SDL_AtomicCAS( &l->usb_changed, 1, 0 ) && refresh_usb_programs( l ))
        {
            rebuild_lists( l, -1 );
            sh_rebuild( l, -1 );
            ui_toast( ui, "USB programs refreshed", 1800 );
        }
        if (sh.page == PAGE_LIBRARY && (sh.tab == TAB_ALL || sh.tab == TAB_AVAILABLE) &&
            SDL_GetTicks() - sh.owned_read > 15000)
            sh_rebuild( l, sh.zone && sh.selection < sh.count ? sh.items[sh.selection] : -1 );
        l->icon_frame = l->icon_use;
        sh_draw_root( l );
        ui_present( ui );
        ui_wait( ui );
    }
    sh_art_stop();
    ui->global_key = NULL;
    return 0;
}
