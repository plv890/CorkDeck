/*
 * The runtime's launcher, shown before Wine starts: the Windows programs on
 * the SD card, drawn with SDL2 (launcher_ui.c). The screens themselves are
 * CorkDeck's shell (shell/shell.inc); this file keeps what they stand on:
 *
 * - The library: the programs added to it (the catalog), their settings beside
 *   them (launcher_settings.h) and the launcher's own file (launcher.txt).
 * - The file browser adds a program from the SD card or a mounted USB volume:
 *   C: is drive_c, Z: the card's root and D: through H: are USB volumes.
 *
 * Icons are read and decoded on a worker thread and shown as they arrive.
 * When SDL cannot start, the text menu of launcher_console.c is shown instead.
 */
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "launcher.h"
#ifdef WINE_NX_HAS_EMBEDDED_LOGO
#include "forwarder.h"
#endif
#include "launcher_catalog.h"
#include "launcher_icons.h"
#include "launcher_list.h"
#include "launcher_pe.h"
#include "key_names.h"
#include "launcher_settings.h"
#include "launcher_ui.h"
#include "launcher_forwarder.h"
#include "launcher_image.h"
#include "launcher_dlls.h"
#include "launcher_setup.h"
#include "setup_boot.h"
#include "launcher_graphics.h"
#include "steamgriddb.h"
#include "steam/steam_ui.h"
#include "steam/steam_storage.h"
#include "steam/steam_prerequisites.h"
#include "steam/steam_prerequisites_ui.h"
#include "diagnostics/lan_log.h"
#include "steam/steam_local.h"
#include "dxvk_releases.h"
#include "box64_options.h"
#include "fex_options.h"

#define ICON_SIDE      128    /* icons are decoded no larger than this */
/* What decoded art may hold, in bytes of texture: every cover of a library
 * and Home's art stay once they have been shown, so going back to a screen or
 * a page shows them at once rather than decoding them again. Autorun runs as
 * an application, with the memory of one. */
#define ICON_BUDGET    (192ull * 1024 * 1024)
/* Decoded art made into textures per frame, as tico does on the Switch. */
#define ICON_UPLOADS   3
#define ICON_JOBS      64
#define MAX_FILES      1024
#define FOOTER_SPACE   38

/* Home and Library's header, laid out after GameHub's at 1280x720. */
enum shell_tab { SHELL_HOME, SHELL_LIBRARY, SHELL_STEAM, SHELL_DOWNLOADS, SHELL_SETTINGS, SHELL_TABS };
#define SHELL_Y        UI_HEADER_CENTRE
#define SHELL_ICON     30
#define SHELL_MARGIN   UI_HEADER_MARGIN   /* which Home's content keeps too */
#define SHELL_GAP      44
/* What the header's pill keeps between its outline and the item inside it. */
#define SHELL_PADDING  22

/* The line of the mark and the hints at the foot of the screen. */
#define HOME_HINT_Y       (720 - 50)

/* Where the D-pad and stick are: the header or the games. */
enum zone { ZONE_HEADER, ZONE_CONTENT };

/* The icons drawn from assets/ (launcher_icons.h), made once at the size they are shown. */
enum symbol { SYMBOL_HOME, SYMBOL_LIBRARY, SYMBOL_STEAM, SYMBOL_DOWNLOADS, SYMBOL_ADD, SYMBOL_SETTINGS, SYMBOL_COUNT };

enum icon_state { ICON_UNKNOWN, ICON_QUEUED, ICON_READY, ICON_MISSING };
enum art_kind { ART_PORTRAIT, ART_SQUARE, ART_HERO };

struct program
{
    char path[512];                    /* sdmc:/... */
    char dos[256];                     /* C:\... or Z:\... */
    char title[128];                   /* shown: its own title, the resources' or the file name */
    char resource_title[128];
    unsigned short machine;
    struct launcher_settings settings;
    int own_files;                     /* settings, arguments, controls or Box64 options beside it */
    int added;                         /* listed in launcher-library.txt */
    unsigned int catalog_id;
    unsigned int added_order;
    unsigned int launched_order;
    int favorite;
    int missing;
    char square_art[512];
    char portrait_art[512];
    char hero_art[512];
    int removed;
    enum icon_state icon_state;
    SDL_Texture *icon;
    int icon_width, icon_height;
    int icon_is_art;
    Uint32 icon_time;
    unsigned int icon_use, square_use, hero_use;   /* when each was last asked for */
    enum icon_state square_state, hero_state;
    SDL_Texture *square_icon, *hero_icon;
    int square_width, square_height, hero_width, hero_height;
    Uint32 square_time, hero_time;
};

struct icon_job
{
    int index;
    int kind;
    char path[512];
    char artwork[512];
};

struct icon_result
{
    int index;
    int kind;
    int width, height;
    unsigned char *rgba;
    int artwork;
};

struct file_entry
{
    char name[256];
    int is_dir;
    int supported;
    unsigned short machine;
};

struct launcher
{
    struct wine_nx_launcher_options *options;
    struct ui ui;
    struct launcher_dlls *dlls;
    struct launcher_graphics *graphics;
    char prerequisite_target[512];

    struct program programs[LAUNCHER_MAX_ENTRIES];
    int program_count;
    int visible[LAUNCHER_MAX_ENTRIES];
    int visible_count;
    int selection;
    struct launcher_catalog catalog;
    char search[128];
    int favorites_only;
    int sort_order;
    SDL_Texture *symbols[SYMBOL_COUNT];
    /* The mark in the corner of the shell, from assets/logo.png. */
    SDL_Texture *logo;
    /* Home's own list: the programs that have been started, most recent first. */
    int history[LAUNCHER_MAX_ENTRIES];
    int history_count, history_selection;
    /* What the D-pad moves, and which header item it is on. */
    int zone, header_focus;
    /* Where the header's pill stands and how wide it is, eased toward the item
     * in focus so it slides between them instead of jumping. */
    float pill_x, pill_w;
    /* How much of each header item's name is out, eased toward 1 for the view
     * being shown and 0 for the rest, so one name closes as the other opens. */
    float label_open[SHELL_TABS];
    /* Where the last frame drew what a tap can hit. */
    SDL_Rect shell_hits[SHELL_TABS];
    /* The header's clock and battery, read once a second. */
    Uint32 status_read;
    int status, clock_hour, clock_minute, battery, charging;

    struct launcher_kv look;
    int show_hidden, hide_missing;
    char browse_dir[512];

    SDL_Thread *thread;
    SDL_mutex *mutex;
    SDL_cond *cond;
    int stop;
    struct icon_job jobs[ICON_JOBS];
    int job_count;
    struct icon_result results[ICON_JOBS];
    int result_count;
    unsigned int icon_use;
    /* icon_use as the last frame began to draw: what stands above it was asked
     * for by that frame, so it is on the screen and must not be thrown away. */
    unsigned int icon_frame;
    Uint32 icon_event;
    Uint32 usb_event;
    SDL_atomic_t usb_changed;
};

static struct launcher launcher;
static struct file_entry files[MAX_FILES];
static struct ui_row file_rows[MAX_FILES + 8];

void wine_nx_launcher_usb_changed(void)
{
    SDL_Event event;

    if (!launcher.usb_event) return;
    SDL_AtomicSet( &launcher.usb_changed, 1 );
    memset( &event, 0, sizeof(event) );
    event.type = launcher.usb_event;
    SDL_PushEvent( &event );
}

extern int wine_nx_launcher_console_run( const char *drive_c, const char *runtime_dir, const char *build,
                                         int (*machine_of)( const char *path, unsigned short *machine ),
                                         int *verbose, int *profile, char *target, size_t target_size );

/***********************************************************************
 * Platform
 */

/* The last line the launcher logged, for a screen that has to say what came
 * before a failure when the card no longer takes the log. */
static char launcher_last_log[120];

static void launcher_log( const char *format, ... )
{
    char line[600];
    va_list args;

    va_start( args, format );
    vsnprintf( line, sizeof(line), format, args );
    va_end( args );
    if (strncmp( line, "[CARD]", 6 )) snprintf( launcher_last_log, sizeof(launcher_last_log), "%s", line );
    wine_nx_runtime_trace( line );
}

#ifdef __SWITCH__
static int font_service;

int launcher_platform_font( const void **data, size_t *size )
{
    PlFontData font;

    if (R_FAILED( plInitialize( PlServiceType_User ) )) return 0;
    font_service = 1;
    if (R_FAILED( plGetSharedFontByType( &font, PlSharedFontType_Standard ) ) || !font.address) return 0;
    *data = font.address;
    *size = font.size;
    return 1;
}

static int battery_service;    /* 1 open, -1 refused */

static void launcher_platform_font_release(void)
{
    /* The fonts read the shared memory until they are closed, so this comes after ui_quit. */
    if (font_service) plExit();
    font_service = 0;
    if (battery_service > 0) psmExit();
    battery_service = 0;
}

int launcher_platform_status( int *hour, int *minute, int *battery, int *charging )
{
    TimeCalendarTime calendar;
    TimeCalendarAdditionalInfo info;
    PsmChargerType charger;
    u64 now;
    u32 percent;
    int found = 0;

    /* The clock the user set, in the time zone they chose. */
    if (R_SUCCEEDED( timeGetCurrentTime( TimeType_LocalSystemClock, &now ) ) &&
        R_SUCCEEDED( timeToCalendarTimeWithMyRule( now, &calendar, &info ) ))
    {
        *hour = calendar.hour;
        *minute = calendar.minute;
        found |= LAUNCHER_STATUS_CLOCK;
    }
    if (!battery_service) battery_service = R_SUCCEEDED( psmInitialize() ) ? 1 : -1;
    if (battery_service > 0 && R_SUCCEEDED( psmGetBatteryChargePercentage( &percent ) ))
    {
        *battery = percent;
        *charging = R_SUCCEEDED( psmGetChargerType( &charger ) ) && charger != PsmChargerType_Unconnected;
        found |= LAUNCHER_STATUS_BATTERY;
    }
    return found;
}

int launcher_platform_prompt( const char *header, const char *initial, char *out, size_t size )
{
    SwkbdConfig keyboard;
    Result rc;

    if (R_FAILED( swkbdCreate( &keyboard, 0 ) )) return 0;
    swkbdConfigMakePresetDefault( &keyboard );
    swkbdConfigSetHeaderText( &keyboard, header );
    swkbdConfigSetGuideText( &keyboard, header );
    swkbdConfigSetInitialText( &keyboard, initial );
    swkbdConfigSetStringLenMax( &keyboard, size - 1 < 500 ? size - 1 : 500 );
    rc = swkbdShow( &keyboard, out, size );
    swkbdClose( &keyboard );
    return R_SUCCEEDED( rc );
}
#else
static void launcher_platform_font_release(void) {}
#endif

/***********************************************************************
 * Files
 */

static int file_exists( const char *path )
{
    struct stat st;

    return !stat( path, &st ) && S_ISREG( st.st_mode );
}

static void runtime_file( const struct launcher *l, const char *name, char *out, size_t size )
{
    snprintf( out, size, "%s/%s", l->options->runtime_dir, name );
}

/* The key map everything shares. It lives in the config folder; a card written
 * by an earlier build has it beside the launcher, and the runtime reads that
 * one too. Returns whether either is there, with the path of the one to show. */
static void controls_screen( struct launcher *l, const char *path, const char *under_path,
                             const char *title );

static int shared_keys( const struct launcher *l, char *out, size_t size )
{
    runtime_file( l, "config/keys.txt", out, size );
    if (file_exists( out )) return 1;
    runtime_file( l, "keys.txt", out, size );
    return file_exists( out );
}

/* Nonzero when the line reached the card, which the handoff in start_program
 * has to know before it gives the console away. */
static int write_line( const char *path, const char *text )
{
    FILE *file = fopen( path, "w" );

    if (!file) return 0;
    return fprintf( file, "%s\n", text ) > 0 && !fclose( file );
}

static int read_line( const char *path, char *out, size_t size )
{
    FILE *file = fopen( path, "r" );
    int ok;

    out[0] = 0;
    if (!file) return 0;
    ok = fgets( out, size, file ) != NULL;
    fclose( file );
    out[strcspn( out, "\r\n" )] = 0;
    return ok;
}

static const char *file_name( const char *path )
{
    const char *slash = strrchr( path, '/' );

    return slash ? slash + 1 : path;
}

static void parent_dir( char *dir )
{
    char *slash = strrchr( dir, '/' );

    if (!slash) return;
    if (slash == strchr( dir, '/' )) slash[1] = 0;  /* sdmc:/ stays */
    else *slash = 0;
}

static int is_root( const char *dir )
{
    const char *slash = strchr( dir, '/' );

    return !slash || !slash[1];
}

/***********************************************************************
 * Programs
 */

/* The folder a program sits in, when that is a name rather than a place to put
 * a binary: FalloutNV.exe says nothing, "Fallout New Vegas" says what it is.
 * Returns 0 for the usual containers, and for the drive itself. */
static int folder_title( const char *path, char *out, size_t size )
{
    static const char *const containers[] =
    {
        "bin", "bin32", "bin64", "binaries", "game", "games", "data", "app", "apps", "system",
        "win32", "win64", "x86", "x64", "release", "debug", "drive_c", "program files",
        "program files (x86)", "steamapps", "common", "exe", "build",
    };
    const char *end, *start;
    size_t length, i;

    if (!(end = strrchr( path, '/' )) && !(end = strrchr( path, '\\' ))) return 0;
    for (start = end; start > path; start--)
        if (start[-1] == '/' || start[-1] == '\\') break;
    if (start >= end) return 0;
    length = end - start;
    if (length >= size) return 0;
    for (i = 0; i < sizeof(containers) / sizeof(containers[0]); i++)
        if (strlen( containers[i] ) == length && !strncasecmp( start, containers[i], length )) return 0;
    memcpy( out, start, length );
    out[length] = 0;
    return 1;
}

static void load_program_settings( struct launcher *l, struct program *p )
{
    char path[768];
    struct launcher_kv kv;
    size_t len;

    p->own_files = 0;
    memset( &p->settings, 0, sizeof(p->settings) );
    p->settings.verbose = p->settings.profile = p->settings.framebuffer = -1;
    p->settings.fex = p->settings.four_cores = 1;
    if (launcher_program_settings_path( l->options->runtime_dir, p->path, path, sizeof(path) ) &&
        launcher_kv_load( &kv, path ))
    {
        launcher_settings_read( &kv, &p->settings );
        p->own_files |= kv.size && file_exists( path );
    }
    if (launcher_args_path( p->path, path, sizeof(path) )) p->own_files |= file_exists( path );
    if (launcher_keys_path( p->path, path, sizeof(path) )) p->own_files |= file_exists( path );
    if (launcher_sibling_path( p->path, ".box64.txt", path, sizeof(path) )) p->own_files |= file_exists( path );

    if (p->settings.title[0]) snprintf( p->title, sizeof(p->title), "%s", p->settings.title );
    else if (p->resource_title[0]) snprintf( p->title, sizeof(p->title), "%s", p->resource_title );
    else if (!folder_title( p->path, p->title, sizeof(p->title) ))
    {
        snprintf( p->title, sizeof(p->title), "%s", file_name( p->path ) );
        if ((len = strlen( p->title )) > 4 && !strcasecmp( p->title + len - 4, ".exe" )) p->title[len - 4] = 0;
    }
}

/* Fill in a program from its file; returns 0 when this runtime cannot start it. */
static int describe_program( struct launcher *l, struct program *p, const char *path )
{
    memset( p, 0, sizeof(*p) );
    p->settings.verbose = p->settings.profile = p->settings.framebuffer = -1;
    if ((size_t)snprintf( p->path, sizeof(p->path), "%s", path ) >= sizeof(p->path)) return 0;
    if (!launcher_dos_path( path, p->dos, sizeof(p->dos) )) return 0;
    if (l->options->machine_of( path, &p->machine )) return 0;
    launcher_pe_describe( path, 0, NULL, p->resource_title, sizeof(p->resource_title) );
    load_program_settings( l, p );
    return 1;
}

static void describe_catalog_program( struct launcher *l, struct program *p,
                                      const struct launcher_catalog_entry *entry )
{
    memset( p, 0, sizeof(*p) );
    p->settings.verbose = p->settings.profile = p->settings.framebuffer = -1;
    snprintf( p->path, sizeof(p->path), "%s", entry->path );
    launcher_dos_path( p->path, p->dos, sizeof(p->dos) );
    p->catalog_id = entry->id;
    p->added_order = entry->added_order;
    p->launched_order = entry->launched_order;
    p->favorite = entry->favorite;
    snprintf( p->square_art, sizeof(p->square_art), "%s", entry->square_art );
    snprintf( p->portrait_art, sizeof(p->portrait_art), "%s", entry->portrait_art );
    snprintf( p->hero_art, sizeof(p->hero_art), "%s", entry->hero_art );
    p->added = 1;
    if (file_exists( p->path ) && !l->options->machine_of( p->path, &p->machine ))
    {
        launcher_pe_describe( p->path, 0, NULL, p->resource_title, sizeof(p->resource_title) );
        load_program_settings( l, p );
    }
    else p->missing = 1;
    if (!p->title[0] && entry->title[0]) snprintf( p->title, sizeof(p->title), "%s", entry->title );
    if (!p->title[0] && folder_title( p->path, p->title, sizeof(p->title) )) return;
    if (!p->title[0])
    {
        size_t len;
        snprintf( p->title, sizeof(p->title), "%s", file_name( p->path ) );
        len = strlen( p->title );
        if (len > 4 && !strcasecmp( p->title + len - 4, ".exe" )) p->title[len - 4] = 0;
    }
}

static int find_program( const struct launcher *l, const char *path )
{
    int i;

    for (i = 0; i < l->program_count; i++)
        if (!l->programs[i].removed && !strcasecmp( l->programs[i].path, path )) return i;
    return -1;
}

static void draw_loading( struct launcher *l, int found )
{
    struct ui *ui = &l->ui;
    char text[64];

    ui_background( ui );
    ui_header( ui, "Library", NULL );
    ui_text_centered( ui, ui->large, ui->width / 2, ui->height / 2 - 48, "Looking for programs...", ui->value );
    snprintf( text, sizeof(text), found == 1 ? "%d program found" : "%d programs found", found );
    ui_text_centered( ui, ui->small, ui->width / 2, ui->height / 2 + 20, text, ui->dim );
    ui_present( ui );
}

static int save_library( struct launcher *l )
{
    char path[512];
    int i;
    unsigned int next_id = l->catalog.next_id, next_order = l->catalog.next_order;
    launcher_catalog_init( &l->catalog );
    l->catalog.next_id = next_id;
    l->catalog.next_order = next_order;
    for (i = 0; i < l->program_count; i++)
    {
        struct program *p = &l->programs[i];
        struct launcher_catalog_entry *entry;
        if (p->removed || !p->added) continue;
        entry = &l->catalog.entries[l->catalog.count++];
        if (!p->catalog_id) p->catalog_id = l->catalog.next_id++;
        if (!p->added_order) p->added_order = l->catalog.next_order++;
        entry->id = p->catalog_id;
        entry->added_order = p->added_order;
        snprintf( entry->path, sizeof(entry->path), "%s", p->path );
        snprintf( entry->title, sizeof(entry->title), "%s", p->title );
        entry->launched_order = p->launched_order;
        entry->favorite = p->favorite;
        snprintf( entry->square_art, sizeof(entry->square_art), "%s", p->square_art );
        snprintf( entry->portrait_art, sizeof(entry->portrait_art), "%s", p->portrait_art );
        snprintf( entry->hero_art, sizeof(entry->hero_art), "%s", p->hero_art );
        if (entry->id >= l->catalog.next_id) l->catalog.next_id = entry->id + 1;
        if (entry->added_order >= l->catalog.next_order) l->catalog.next_order = entry->added_order + 1;
        if (entry->launched_order >= l->catalog.next_order) l->catalog.next_order = entry->launched_order + 1;
    }
    runtime_file( l, LAUNCHER_CATALOG_FILE, path, sizeof(path) );
    if (!launcher_catalog_save( &l->catalog, path ))
    {
        ui_toast( &l->ui, "Could not save the game library", 2500 );
        return 0;
    }
    return 1;
}

static void load_library( struct launcher *l )
{
    char path[512], legacy[512];
    enum launcher_catalog_result result;
    int i;

    draw_loading( l, 0 );
    runtime_file( l, LAUNCHER_CATALOG_FILE, path, sizeof(path) );
    result = launcher_catalog_load( &l->catalog, path );
    if (result == LAUNCHER_CATALOG_MISSING)
    {
        launcher_catalog_init( &l->catalog );
        runtime_file( l, "launcher-library.txt", legacy, sizeof(legacy) );
        if (!launcher_catalog_import_legacy( &l->catalog, legacy ) || !launcher_catalog_save( &l->catalog, path ))
            ui_message( &l->ui, "Library", "The saved game library could not be migrated." );
    }
    else if (result != LAUNCHER_CATALOG_OK)
    {
        launcher_catalog_init( &l->catalog );
        ui_message( &l->ui, "Library", "The game library is unreadable. It was preserved and no games were loaded." );
    }
    for (i = 0; i < l->catalog.count && l->program_count < LAUNCHER_MAX_ENTRIES; i++)
        describe_catalog_program( l, &l->programs[l->program_count++], &l->catalog.entries[i] );
}

static struct launcher *sort_launcher;

static int contains_case( const char *text, const char *needle )
{
    size_t len = strlen( needle );
    if (!len) return 1;
    while (*text)
    {
        if (!strncasecmp( text, needle, len )) return 1;
        text++;
    }
    return 0;
}

static int compare_visible( const void *a, const void *b )
{
    const struct program *x = &sort_launcher->programs[*(const int *)a];
    const struct program *y = &sort_launcher->programs[*(const int *)b];
    int order;

    if (sort_launcher->sort_order == 1 && x->added_order != y->added_order)
        return x->added_order < y->added_order ? 1 : -1;
    if (sort_launcher->sort_order == 2 && x->launched_order != y->launched_order)
        return x->launched_order < y->launched_order ? 1 : -1;
    order = strcasecmp( x->title, y->title );

    return order ? order : strcasecmp( x->dos, y->dos );
}

/* The programs shown, sorted by title; keep_index stays selected when it is shown. */
static void rebuild_visible( struct launcher *l, int keep_index )
{
    int i;

    l->visible_count = 0;
    for (i = 0; i < l->program_count; i++)
    {
        const struct program *p = &l->programs[i];

        if (p->removed || (l->hide_missing && p->missing) || (p->settings.hidden && !l->show_hidden) ||
            (l->favorites_only && !p->favorite) || !contains_case( p->title, l->search )) continue;
        l->visible[l->visible_count++] = i;
    }
    sort_launcher = l;
    qsort( l->visible, l->visible_count, sizeof(l->visible[0]), compare_visible );
    for (i = 0; i < l->visible_count; i++)
        if (l->visible[i] == keep_index) l->selection = i;
    if (l->selection >= l->visible_count) l->selection = l->visible_count ? l->visible_count - 1 : 0;
}

/* Home lists what has been played, most recently started first; nothing else. */
static int compare_history( const void *a, const void *b )
{
    const struct program *x = &sort_launcher->programs[*(const int *)a];
    const struct program *y = &sort_launcher->programs[*(const int *)b];

    if (x->launched_order != y->launched_order) return x->launched_order < y->launched_order ? 1 : -1;
    return strcasecmp( x->title, y->title );
}

static void rebuild_history( struct launcher *l, int keep_index )
{
    int i;

    l->history_count = 0;
    for (i = 0; i < l->program_count; i++)
    {
        const struct program *p = &l->programs[i];

        if (p->removed || !p->launched_order || (l->hide_missing && p->missing) ||
            (p->settings.hidden && !l->show_hidden)) continue;
        l->history[l->history_count++] = i;
    }
    sort_launcher = l;
    qsort( l->history, l->history_count, sizeof(l->history[0]), compare_history );
    for (i = 0; i < l->history_count; i++)
        if (l->history[i] == keep_index) l->history_selection = i;
    if (l->history_selection >= l->history_count) l->history_selection = l->history_count ? l->history_count - 1 : 0;
}

static void save_program_settings( struct launcher *l, struct program *p )
{
    struct launcher_kv kv;
    char path[768], folder[768];
    int ready = 1;

    if (launcher_settings_on_usb( p->path ))
    {
        runtime_file( l, "program-settings", folder, sizeof(folder) );
        ready = !mkdir( folder, 0777 ) || errno == EEXIST;
    }
    if (!ready || !launcher_program_settings_path( l->options->runtime_dir, p->path, path, sizeof(path) ) ||
        !launcher_kv_load( &kv, path ) || !launcher_settings_write( &kv, &p->settings ) ||
        !launcher_kv_save( &kv, path ))
        ui_toast( &l->ui, "Could not save the program's settings", 2500 );
    load_program_settings( l, p );
}

static void apply_new_game_renderer( struct launcher *l, struct program *p )
{
    char path[768], value[64];
    struct launcher_kv kv;

    if (l->options->dxvk_on_add || !launcher_dxvk_directory( p->machine, p->settings.dxvk_source ) ||
        !launcher_program_settings_path( l->options->runtime_dir, p->path, path, sizeof(path) ) ||
        !launcher_kv_load( &kv, path ) || launcher_kv_get( &kv, "d3d", value, sizeof(value) ) ||
        launcher_kv_get( &kv, "d3d9", value, sizeof(value) )) return;
    p->settings.d3d = LAUNCHER_D3D_WINE;
    save_program_settings( l, p );
}

/***********************************************************************
 * Icons
 */

static int decode_png( struct launcher_icon *icon )
{
    struct launcher_icon decoded = {0};
    if (!launcher_image_decode( icon->data, icon->size, &decoded )) return 0;
    if (decoded.width > LAUNCHER_ICON_MAX_SIDE || decoded.height > LAUNCHER_ICON_MAX_SIDE)
    {
        launcher_icon_free( &decoded );
        return 0;
    }
    launcher_icon_free( icon );
    *icon = decoded;
    return 1;
}

/* The mark the shell shows in its corner, carried inside the runtime. */
static SDL_Texture *load_logo( struct launcher *l )
{
#ifdef WINE_NX_HAS_EMBEDDED_LOGO
    struct launcher_icon icon;
    SDL_Texture *texture = NULL;
    SDL_Surface *surface;

    memset( &icon, 0, sizeof(icon) );
    icon.size = wine_nx_logo_size;
    if (!icon.size || !(icon.data = malloc( icon.size ))) return NULL;
    memcpy( icon.data, wine_nx_logo, icon.size );
    if (!decode_png( &icon )) { free( icon.data ); return NULL; }
    if ((surface = SDL_CreateRGBSurfaceWithFormatFrom( icon.data, icon.width, icon.height, 32,
                                                       icon.width * 4, SDL_PIXELFORMAT_ABGR8888 )))
    {
        texture = SDL_CreateTextureFromSurface( l->ui.renderer, surface );
        SDL_FreeSurface( surface );
    }
    free( icon.data );
    return texture;
#else
    (void)l;
    return NULL;
#endif
}

/* Cover files are optional. Decode on the existing worker, with bounded input. */
static int read_cover( const char *path, struct launcher_icon *icon )
{
    if (!launcher_image_load( path, icon )) return 0;
    if (!launcher_icon_fit( icon, 512 )) { launcher_icon_free( icon ); return 0; }
    return 1;
}

static int icon_thread( void *arg )
{
    struct launcher *l = arg;

    SDL_LockMutex( l->mutex );
    while (!l->stop)
    {
        struct launcher_icon icon;
        struct icon_result result;
        struct icon_job job;
        SDL_Event event;

        if (!l->job_count || l->result_count == ICON_JOBS)
        {
            SDL_CondWait( l->cond, l->mutex );
            continue;
        }
        /* The newest request first: what the screen shows now, not what it
         * showed while the list scrolled past. */
        job = l->jobs[--l->job_count];
        SDL_UnlockMutex( l->mutex );

        memset( &icon, 0, sizeof(icon) );
        result.artwork = read_cover( job.artwork, &icon );
        if (!result.artwork && job.kind == ART_PORTRAIT)
        {
            launcher_pe_describe( job.path, ICON_SIDE, &icon, NULL, 0 );
            if (icon.kind == LAUNCHER_ICON_PNG && !decode_png( &icon )) launcher_icon_free( &icon );
            if (icon.kind == LAUNCHER_ICON_RGBA && !launcher_icon_fit( &icon, ICON_SIDE )) launcher_icon_free( &icon );
        }
        result.index = job.index;
        result.kind = job.kind;
        result.width = icon.width;
        result.height = icon.height;
        result.rgba = icon.kind == LAUNCHER_ICON_RGBA ? icon.data : NULL;
        if (!result.rgba) launcher_icon_free( &icon );

        SDL_LockMutex( l->mutex );
        l->results[l->result_count++] = result;
        memset( &event, 0, sizeof(event) );
        event.type = l->icon_event;
        SDL_PushEvent( &event );
    }
    SDL_UnlockMutex( l->mutex );
    return 0;
}

static void start_icons( struct launcher *l )
{
    l->icon_event = SDL_RegisterEvents( 1 );
    if (l->icon_event == (Uint32)-1) l->icon_event = SDL_USEREVENT;
    l->mutex = SDL_CreateMutex();
    l->cond = SDL_CreateCond();
    if (l->mutex && l->cond) l->thread = SDL_CreateThreadWithStackSize( icon_thread, "launcher icons", 256 * 1024, l );
}

static void stop_icons( struct launcher *l )
{
    int i;

    if (l->thread)
    {
        SDL_LockMutex( l->mutex );
        l->stop = 1;
        SDL_CondSignal( l->cond );
        SDL_UnlockMutex( l->mutex );
        SDL_WaitThread( l->thread, NULL );
    }
    for (i = 0; i < l->result_count; i++) free( l->results[i].rgba );
    l->result_count = l->job_count = 0;
    for (i = 0; i < l->program_count; i++)
    {
        if (l->programs[i].icon) SDL_DestroyTexture( l->programs[i].icon );
        if (l->programs[i].square_icon) SDL_DestroyTexture( l->programs[i].square_icon );
        if (l->programs[i].hero_icon) SDL_DestroyTexture( l->programs[i].hero_icon );
        l->programs[i].icon = NULL;
        l->programs[i].square_icon = l->programs[i].hero_icon = NULL;
    }
    if (l->cond) SDL_DestroyCond( l->cond );
    if (l->mutex) SDL_DestroyMutex( l->mutex );
    l->thread = NULL;
    l->cond = NULL;
    l->mutex = NULL;
}

/* One of a program's three pictures, as the cache sees it. */
struct art
{
    SDL_Texture **texture;
    int *width, *height;
    enum icon_state *state;
    unsigned int *use;
};

static struct art art_of( struct program *p, int kind )
{
    if (kind == ART_SQUARE)
        return (struct art){ &p->square_icon, &p->square_width, &p->square_height, &p->square_state, &p->square_use };
    if (kind == ART_HERO)
        return (struct art){ &p->hero_icon, &p->hero_width, &p->hero_height, &p->hero_state, &p->hero_use };
    return (struct art){ &p->icon, &p->icon_width, &p->icon_height, &p->icon_state, &p->icon_use };
}

static unsigned long long art_bytes( int width, int height )
{
    return (unsigned long long)width * height * 4;
}

static void pump_icons( struct launcher *l )
{
    struct icon_result results[ICON_JOBS];
    unsigned long long held = 0;
    int count, left, i, kind, oldest;

    if (!l->thread) return;
    /* A few to the GPU each frame, the oldest first: making every cover that
     * came in at once into a texture in one frame is a frame that hitches
     * while the list moves. The rest wait for the next ones. */
    SDL_LockMutex( l->mutex );
    count = l->result_count < ICON_UPLOADS ? l->result_count : ICON_UPLOADS;
    memcpy( results, l->results, count * sizeof(results[0]) );
    l->result_count -= count;
    memmove( l->results, l->results + count, l->result_count * sizeof(results[0]) );
    left = l->result_count;
    SDL_CondSignal( l->cond );
    SDL_UnlockMutex( l->mutex );
    /* Those left wake the loop again for the next frame. */
    if (left)
    {
        SDL_Event event;

        memset( &event, 0, sizeof(event) );
        event.type = l->icon_event;
        SDL_PushEvent( &event );
    }

    for (i = 0; i < count; i++)
    {
        struct program *p = &l->programs[results[i].index];
        SDL_Texture *texture = NULL;

        if (results[i].rgba &&
            (texture = SDL_CreateTexture( l->ui.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                          results[i].width, results[i].height )))
        {
            SDL_UpdateTexture( texture, NULL, results[i].rgba, results[i].width * 4 );
            SDL_SetTextureBlendMode( texture, SDL_BLENDMODE_BLEND );
        }
        free( results[i].rgba );
        if (results[i].kind == ART_SQUARE)
        {
            p->square_icon = texture; p->square_width = results[i].width; p->square_height = results[i].height;
            p->square_time = SDL_GetTicks(); p->square_state = texture ? ICON_READY : ICON_MISSING;
        }
        else if (results[i].kind == ART_HERO)
        {
            p->hero_icon = texture; p->hero_width = results[i].width; p->hero_height = results[i].height;
            p->hero_time = SDL_GetTicks(); p->hero_state = texture ? ICON_READY : ICON_MISSING;
        }
        else
        {
            p->icon = texture; p->icon_width = results[i].width; p->icon_height = results[i].height;
            p->icon_is_art = results[i].artwork; p->icon_time = SDL_GetTicks();
            p->icon_state = texture ? ICON_READY : ICON_MISSING;
        }
    }

    /* Keep what was shown most recently, up to the budget, and never what the
     * frame being drawn asked for. */
    for (i = 0; i < l->program_count; i++)
        for (kind = 0; kind < 3; kind++)
        {
            struct art a = art_of( &l->programs[i], kind );
            if (*a.texture) held += art_bytes( *a.width, *a.height );
        }
    while (held > ICON_BUDGET)
    {
        int oldest_kind = 0;
        unsigned int oldest_use = 0;

        oldest = -1;
        for (i = 0; i < l->program_count; i++)
            for (kind = 0; kind < 3; kind++)
            {
                struct art a = art_of( &l->programs[i], kind );
                if (!*a.texture || *a.use > l->icon_frame || (oldest >= 0 && *a.use >= oldest_use)) continue;
                oldest = i;
                oldest_kind = kind;
                oldest_use = *a.use;
            }
        /* Everything held is on the screen now: keep it and let the frame be
         * over the budget rather than throw away a cover about to be drawn. */
        if (oldest < 0) break;
        {
            struct art a = art_of( &l->programs[oldest], oldest_kind );
            held -= art_bytes( *a.width, *a.height );
            SDL_DestroyTexture( *a.texture );
            *a.texture = NULL;
            *a.state = ICON_UNKNOWN;
        }
    }
}

static void request_art( struct launcher *l, int index, enum art_kind kind )
{
    struct program *p = &l->programs[index];
    struct art a = art_of( p, kind );
    enum icon_state *state = a.state;
    const char *art = kind == ART_SQUARE ? p->square_art : kind == ART_HERO ? p->hero_art : p->portrait_art;

    *a.use = ++l->icon_use;
    if (*state != ICON_UNKNOWN || !l->thread) return;
    SDL_LockMutex( l->mutex );
    /* The worker takes the newest first; a full queue lets go of the oldest,
     * asked for by a screen long gone, which is asked for again if it comes back. */
    if (l->job_count == ICON_JOBS)
    {
        *art_of( &l->programs[l->jobs[0].index], l->jobs[0].kind ).state = ICON_UNKNOWN;
        memmove( l->jobs, l->jobs + 1, --l->job_count * sizeof(l->jobs[0]) );
    }
    {
        l->jobs[l->job_count].index = index;
        l->jobs[l->job_count].kind = kind;
        memcpy( l->jobs[l->job_count].path, p->path, sizeof(p->path) );
        if (art[0])
            snprintf( l->jobs[l->job_count].artwork, sizeof(l->jobs[0].artwork), "%s",
                      art );
        else if (kind == ART_PORTRAIT)
        {
            char folder[512];
            snprintf( folder, sizeof(folder), "%s", p->path );
            parent_dir( folder );
            if (snprintf( l->jobs[l->job_count].artwork, sizeof(l->jobs[0].artwork), "%s/cover.png", folder ) >=
                (int)sizeof(l->jobs[0].artwork)) l->jobs[l->job_count].artwork[0] = 0;
        }
        else l->jobs[l->job_count].artwork[0] = 0;
        l->job_count++;
        *state = ICON_QUEUED;
        SDL_CondSignal( l->cond );
    }
    SDL_UnlockMutex( l->mutex );
}

static void request_icon( struct launcher *l, int index ) { request_art( l, index, ART_PORTRAIT ); }

/***********************************************************************
 * Library grid
 */

struct grid
{
    int card, gap_x, gap_y, caption, x0, y0, columns, rows, first;
};

static void draw_footprint( struct launcher *l );
static void draw_cover( struct ui *ui, const struct program *p, SDL_Rect rect, int radius, int brightness,
                        int alpha );

#define GRID_CARD_TARGET 200

static void grid_layout( const struct launcher *l, struct grid *g )
{
    const struct ui *ui = &l->ui;
    int width = ui->width - 2 * SHELL_MARGIN, available = ui->height - UI_HEADER_HEIGHT - FOOTER_SPACE, h, page_size;

    g->gap_x = 22;
    g->gap_y = 16;
    g->caption = 36;
    g->columns = (width + g->gap_x) / (GRID_CARD_TARGET + g->gap_x);
    if (g->columns < 1) g->columns = 1;
    g->card = (width - (g->columns - 1) * g->gap_x) / g->columns;
    if (g->card < 64) g->card = 64;
    g->rows = (available - 24 + g->gap_y) / (g->card + g->caption + g->gap_y);
    if (g->rows < 1) g->rows = 1;
    page_size = g->columns * g->rows;
    g->first = l->selection / page_size * page_size;
    h = g->rows * (g->card + g->caption) + (g->rows - 1) * g->gap_y;
    g->x0 = SHELL_MARGIN;
    g->y0 = UI_HEADER_HEIGHT + (available - h) / 2 + 4;
}

/* What a card will draw, asked for ahead of it. */
static void prefetch_card( struct launcher *l, int index )
{
    struct program *p = &l->programs[l->visible[index]];

    request_art( l, l->visible[index], ART_SQUARE );
    if (!p->square_art[0] || p->square_state == ICON_MISSING) request_icon( l, l->visible[index] );
}

static int draw_symbol( struct launcher *l, enum symbol symbol, int x, int cy, int alpha )
{
    SDL_Texture *texture = l->symbols[symbol];
    SDL_Rect dst;

    if (!texture || SDL_QueryTexture( texture, NULL, NULL, &dst.w, &dst.h )) return 0;
    dst.x = x;
    dst.y = cy - dst.h / 2;
    SDL_SetTextureAlphaMod( texture, alpha );
    SDL_RenderCopy( l->ui.renderer, texture, NULL, &dst );
    return dst.w;
}

static void draw_battery( struct ui *ui, int x, int cy, int percent, int charging )
{
    const SDL_Color line = { 255, 255, 255, 235 };
    const int w = 26, h = 13, y = cy - h / 2;
    int level = (w - 6) * (percent < 0 ? 0 : percent > 100 ? 100 : percent) / 100;

    ui_fill( ui, x + 1, y, w - 2, 2, line );
    ui_fill( ui, x + 1, y + h - 2, w - 2, 2, line );
    ui_fill( ui, x, y + 1, 2, h - 2, line );
    ui_fill( ui, x + w - 2, y + 1, 2, h - 2, line );
    ui_fill( ui, x + w + 1, y + 4, 2, h - 8, line );
    ui_fill( ui, x + 3, y + 3, level, h - 6, charging ? (SDL_Color){ 124, 222, 146, 255 } : line );
}

static const char *const shell_labels[SHELL_TABS] = { "Home", "Library", "Steam", "Downloads" };

/* How wide a header item is: its icon, and as much of the name as is out. */
static int shell_width( struct launcher *l, int tab )
{
    int width = 0;

    if (l->symbols[tab]) SDL_QueryTexture( l->symbols[tab], NULL, NULL, &width, NULL );
    if (shell_labels[tab])
        width += (int)((12 + ui_text_width( &l->ui, l->ui.normal, shell_labels[tab] )) * l->label_open[tab] + 0.5f);
    return width;
}

/* The clock and the battery, at the right of whichever header asks for them.
 * Returns where the left of what it drew is. */
static int draw_status( struct launcher *l, int right, int cy )
{
    struct ui *ui = &l->ui;
    Uint32 now = SDL_GetTicks();
    char text[32];

    if (!l->status_read || now - l->status_read >= 1000)
    {
        l->status = launcher_platform_status( &l->clock_hour, &l->clock_minute, &l->battery, &l->charging );
        l->status_read = now ? now : 1;
    }
    if (l->status & LAUNCHER_STATUS_BATTERY)
    {
        snprintf( text, sizeof(text), "%d%%", l->battery );
        right -= ui_text_width( ui, ui->small, text );
        ui_text( ui, ui->small, right, cy - TTF_FontHeight( ui->small ) / 2, text, ui->value );
        right -= 8 + 29;
        draw_battery( ui, right, cy, l->battery, l->charging );
        right -= 24;
    }
    if (l->status & LAUNCHER_STATUS_CLOCK)
    {
        snprintf( text, sizeof(text), "%02d:%02d", l->clock_hour, l->clock_minute );
        right -= ui_text_width( ui, ui->small, text );
        ui_text( ui, ui->small, right, cy - TTF_FontHeight( ui->small ) / 2, text, ui->value );
    }
    return right;
}

/* What a header outside the shell asks for, through the ui. */
static void header_status( void *data, int right, int y )
{
    struct launcher *l = data;

    l->ui.status_left = draw_status( l, right, y );
}

/* The header over Home and Library: the current view's icon and name, the other
 * views and Add Game as icons, and Settings, the clock and the battery at the right. */
static void draw_shell_active( struct launcher *l, int home, int active )
{
    static const struct { enum symbol symbol; const char *label; } tabs[] =
    {
        [SHELL_HOME] = { SYMBOL_HOME, "Home" },
        [SHELL_LIBRARY] = { SYMBOL_LIBRARY, "Library" },
        [SHELL_STEAM] = { SYMBOL_STEAM, "Steam" },
        [SHELL_DOWNLOADS] = { SYMBOL_DOWNLOADS, "Downloads" },
    };
    struct ui *ui = &l->ui;
    int x = SHELL_MARGIN, right = ui->width - SHELL_MARGIN, i, width;

    ui_gradient( ui, 0, 0, ui->width, 150, (SDL_Color){ 0, 0, 0, 150 }, (SDL_Color){ 0, 0, 0, 0 }, 0 );
    /* One name closes as the other opens, and everything after them moves with
     * it: the widths below are measured from where they have got to. */
    for (i = SHELL_HOME; i < SHELL_TABS; i++)
    {
        float target = i == active ? 1.0f : 0.0f;
        if (i == SHELL_STEAM || i == SHELL_DOWNLOADS) target = 1.0f;

        if (!ui->animations) l->label_open[i] = target;
        else l->label_open[i] += (target - l->label_open[i]) * 0.22f;
        if (fabsf( target - l->label_open[i] ) < 0.004f) l->label_open[i] = target;
    }

    /* The clock and the battery first: Settings stands to the left of whatever
     * they take, and the pill has to know where that is before it goes there. */
    ui->status_left = right = draw_status( l, right, SHELL_Y );
    right -= SHELL_GAP;
    width = l->symbols[SYMBOL_SETTINGS] ? SHELL_ICON - 4 : 0;
    right -= width;

    /* Where the pill is going, before anything is drawn over it. */
    {
        int target_x = 0, target_w = 0, at = SHELL_MARGIN;

        for (i = SHELL_HOME; i <= SHELL_DOWNLOADS; i++)
        {
            int item_w = shell_width( l, i );

            if (l->zone == ZONE_HEADER && l->header_focus == i)
            {
                target_x = at - SHELL_PADDING;
                target_w = item_w + 2 * SHELL_PADDING;
            }
            at += item_w + SHELL_GAP;
        }
        if (l->zone == ZONE_HEADER && l->header_focus == SHELL_SETTINGS)
        {
            target_x = right - SHELL_PADDING;
            target_w = width + 2 * SHELL_PADDING;
        }
        if (!target_w) l->pill_w = 0;      /* focus left the header; it starts again where it returns */
        else
        {
            if (!ui->animations || l->pill_w <= 0)
            {
                l->pill_x = target_x;
                l->pill_w = target_w;
            }
            else
            {
                l->pill_x += (target_x - l->pill_x) * 0.30f;
                l->pill_w += (target_w - l->pill_w) * 0.30f;
            }
            ui_animated_border( ui, (int)(l->pill_x + 0.5f), SHELL_Y - 24, (int)(l->pill_w + 0.5f), 48,
                                14, 2, (SDL_Color){ 150, 160, 176, 90 },
                                (SDL_Color){ 244, 247, 250, 255 } );
        }
    }
    for (i = SHELL_HOME; i <= SHELL_DOWNLOADS; i++)
    {
        int focused = l->zone == ZONE_HEADER && l->header_focus == i;
        float lit = focused ? 1.0f : l->label_open[i];
        int start = x;

        x += draw_symbol( l, tabs[i].symbol, x, SHELL_Y, 150 + (int)(105 * lit) );
        if (tabs[i].label && l->label_open[i] > 0.01f)
        {
            x += (int)(12 * l->label_open[i] + 0.5f);
            ui_text_opening( ui, ui->normal, x, SHELL_Y - TTF_FontHeight( ui->normal ) / 2, tabs[i].label,
                             ui->value, l->label_open[i] );
            x += (int)(ui_text_width( ui, ui->normal, tabs[i].label ) * l->label_open[i] + 0.5f);
        }
        l->shell_hits[i] = (SDL_Rect){ start - SHELL_GAP / 2, 0, x - start + SHELL_GAP, UI_HEADER_HEIGHT };
        x += SHELL_GAP;
    }

    draw_symbol( l, SYMBOL_SETTINGS, right, SHELL_Y,
                 l->zone == ZONE_HEADER && l->header_focus == SHELL_SETTINGS ? 255 : 190 );
    l->shell_hits[SHELL_SETTINGS] = (SDL_Rect){ right - SHELL_GAP / 2, 0, width + SHELL_GAP, UI_HEADER_HEIGHT };
    if (home) draw_footprint( l );
}

static void draw_footprint( struct launcher *l )
{
    struct ui *ui = &l->ui;
    const int line = HOME_HINT_Y - 12;
    int height = 44, x = SHELL_MARGIN, width, h;

    if (l->logo)
    {
        SDL_Rect rect;

        SDL_QueryTexture( l->logo, NULL, NULL, &width, &h );
        rect.h = height;
        rect.w = h ? width * height / h : height;
        rect.x = x;
        rect.y = line - height / 2;
        SDL_RenderCopy( ui->renderer, l->logo, NULL, &rect );
    }
}

/* What the settings screens ask for through the ui. */
static void footer_mark( void *data )
{
    draw_footprint( data );
}

static SDL_Rect cover_crop( const struct program *p, int width, int height )
{
    SDL_Rect src = {0, 0, p->icon_width, p->icon_height};
    if (src.w * height > src.h * width)
    {
        src.w = src.h * width / height;
        src.x = (p->icon_width - src.w) / 2;
    }
    else
    {
        src.h = src.w * height / width;
        src.y = (p->icon_height - src.h) / 2;
    }
    return src;
}

static void draw_cover( struct ui *ui, const struct program *p, SDL_Rect rect, int radius, int brightness, int alpha )
{
    SDL_Rect src = cover_crop( p, rect.w, rect.h );

    ui_rounded_texture( ui, p->icon, &src, rect, radius, (SDL_Color){ brightness, brightness, brightness, alpha } );
}

/***********************************************************************
 * A program's menu
 */

static int file_browser_pick( struct launcher *l, char *target, size_t size );
static int steam_register_executable( void *context, const char *path, const char *title );
static void steam_shell_header( void *context, int section, int focused );
static int steam_shell_hit( void *context, int x, int y );
static int steam_launch_prerequisite(void *,const char *);
static int steam_unregister_directory( void *, const char * );
static int steam_choose_executable( void *context, char *path, unsigned int size );
static int pick_forwarder_icon( void *opaque, char *target, size_t size );
static void save_look( struct launcher *l );

static void quick_setup( struct launcher *l )
{
    int result = launcher_setup_run( &l->ui, l->options, l->logo, l->dlls );
    launcher_kv_set( &l->look, "setup-offered", "1" );
    if (result) launcher_kv_set( &l->look, "setup-complete", "1" );
    save_look( l );
    if (result == 2)
    {
        l->options->reboot_requested = 1;
        l->ui.running = 0;
    }
}

static void offer_quick_setup( struct launcher *l )
{
    const struct setup_boot_manifest *manifest;
    char bundle_id[40], prompted[40] = "";
    if (!l->options->install_forwarder) return;
    if (!launcher_kv_get_int( &l->look, "setup-offered", 0 ))
    {
        quick_setup( l );
        return;
    }
    manifest = setup_boot_bundled_manifest();
    if (!setup_boot_needs_update( "sdmc:", manifest, NULL )) return;
    setup_boot_bundle_id( manifest, bundle_id );
    /* The same patches under the entry's new name are another thing to offer:
     * a card asked about them once still has its entry called Autorun. */
    strcat( bundle_id, "+name" );
    launcher_kv_get( &l->look, "setup-boot-prompted", prompted, sizeof(prompted) );
    if (!strcmp( prompted, bundle_id )) return;
    launcher_kv_set( &l->look, "setup-boot-prompted", bundle_id );
    save_look( l );
    quick_setup( l );
}

static int confirm_forwarder( struct launcher *l )
{
    struct ui *ui = &l->ui;
    char message[320];

    const char *warning = "The HOME menu entry is installed as an application. Consoles have been banned for "
                          "homebrew in that list. Use emuMMC only.";
    const char *where = l->options->emummc > 0 ? "This console is on emuMMC." :
                        l->options->emummc == 0 ? "This console is NOT on emuMMC." :
                        "Atmosphere did not say which this console is on.";

    /* Put together from parts, so each part is translated before it is. */
    snprintf( message, sizeof(message), "%s\n\n%s", ui->translate ? ui->translate( warning ) : warning,
              ui->translate ? ui->translate( where ) : where );
    return ui_confirm( ui, "Install forwarder", message, "Install" );
}

static void make_forwarder( struct launcher *l )
{
    struct ui *ui = &l->ui;
    const char *step = NULL;
    char message[256];
    unsigned int rc;

    if (!l->options->install_forwarder) return;
    if (!confirm_forwarder( l )) { ui_start_screen( ui ); return; }

    /* One frame saying what is happening: building the three parts and writing
     * them takes a moment, and nothing is drawn while it does. */
    ui_start_screen( ui );
    ui_background( ui );
    ui_header_back( ui, "Install forwarder", "CorkDeck" );
    ui_text_centered( ui, ui->large, ui->width / 2, ui->height / 2 - 30, "Installing...", ui->value );
    ui_present( ui );

    rc = l->options->install_forwarder( &step );
    ui_start_screen( ui );
    if (rc)
    {
        snprintf( message, sizeof(message), "The console refused while %s.\n\nResult 0x%X.",
                  step ? step : "working", rc );
        ui_message( ui, "Could not install", message );
        ui_start_screen( ui );
        return;
    }
    ui_message( ui, "Installed", "CorkDeck is on the home menu." );
    ui_start_screen( ui );
}


static void download_artwork( struct launcher *l, struct program *p )
{
    char key[512], folder[512], square[512], portrait[512], hero[512], matched[192], message[512];
    struct steamgriddb_game games[STEAMGRIDDB_MAX_GAMES];
    struct ui_row rows[STEAMGRIDDB_MAX_GAMES];
    struct ui_list list = {0};
    int count = 0, i;
    enum steamgriddb_result result;

    if (!launcher_kv_get( &l->look, "steamgriddb-key", key, sizeof(key) ) || !key[0])
    {
        if (!launcher_platform_prompt( "SteamGridDB API key", "", key, sizeof(key) ) || !key[0]) return;
        launcher_kv_set( &l->look, "steamgriddb-key", key );
        save_look( l );
    }
    snprintf( folder, sizeof(folder), "%s/artwork", l->options->runtime_dir );
    if (mkdir( folder, 0777 ) && errno != EEXIST)
    { ui_message( &l->ui, "Artwork download failed", "The artwork cache directory could not be created." ); return; }
    if (snprintf( square, sizeof(square), "%s/%u-square.png", folder, p->catalog_id ) >= (int)sizeof(square) ||
        snprintf( portrait, sizeof(portrait), "%s/%u-portrait.png", folder, p->catalog_id ) >= (int)sizeof(portrait) ||
        snprintf( hero, sizeof(hero), "%s/%u-hero.png", folder, p->catalog_id ) >= (int)sizeof(hero)) return;

    ui_background( &l->ui );
    ui_header( &l->ui, "Downloading artwork", p->title );
    ui_text_centered( &l->ui, l->ui.normal, l->ui.width / 2, l->ui.height / 2 - 12,
                      "Searching SteamGridDB...", l->ui.text );
    ui_present( &l->ui );
    result = steamgriddb_search_games( key, p->title, games, STEAMGRIDDB_MAX_GAMES, &count );
    if (result == STEAMGRIDDB_NO_KEY)
    {
        launcher_kv_set( &l->look, "steamgriddb-key", NULL );
        save_look( l );
    }
    if (result != STEAMGRIDDB_OK)
    { ui_message( &l->ui, "Artwork download failed", steamgriddb_result_message( result ) ); return; }

    memset( rows, 0, sizeof(rows) );
    for (i = 0; i < count; i++)
    {
        snprintf( rows[i].label, sizeof(rows[i].label), "%s", games[i].name );
        snprintf( rows[i].value, sizeof(rows[i].value), "SteamGridDB" );
        rows[i].help = "Download the highest-rated square, portrait and hero artwork for this match.";
    }
    if (count > 1 && ui_list_run( &l->ui, &list, "Choose SteamGridDB game", p->title, rows, count, 0 ) != UI_ACTION_CHOOSE)
        return;
    snprintf( matched, sizeof(matched), "%s", games[list.selection].name );

    ui_background( &l->ui );
    ui_header( &l->ui, "Downloading artwork", matched );
    ui_text_centered( &l->ui, l->ui.normal, l->ui.width / 2, l->ui.height / 2 - 12,
                      "Square, portrait and hero from SteamGridDB...", l->ui.text );
    ui_present( &l->ui );
    result = steamgriddb_download_game_bundle( key, games[list.selection].id, square, portrait, hero );
    if (result == STEAMGRIDDB_NO_KEY)
    {
        launcher_kv_set( &l->look, "steamgriddb-key", NULL );
        save_look( l );
    }
    if (result != STEAMGRIDDB_OK)
    { ui_message( &l->ui, "Artwork download failed", steamgriddb_result_message( result ) ); return; }

    snprintf( p->square_art, sizeof(p->square_art), "%s", square );
    snprintf( p->portrait_art, sizeof(p->portrait_art), "%s", portrait );
    snprintf( p->hero_art, sizeof(p->hero_art), "%s", hero );
    if (p->icon) SDL_DestroyTexture( p->icon );
    if (p->square_icon) SDL_DestroyTexture( p->square_icon );
    if (p->hero_icon) SDL_DestroyTexture( p->hero_icon );
    p->icon = NULL; p->icon_state = ICON_UNKNOWN; p->icon_is_art = 0;
    p->square_icon = p->hero_icon = NULL;
    p->square_state = p->hero_state = ICON_UNKNOWN;
    save_library( l );
    snprintf( message, sizeof(message), "Downloaded the highest-rated square, portrait and hero artwork for %s.", matched );
    ui_message( &l->ui, "Artwork downloaded", message );
}

static int address_space_fits( struct launcher *l, struct program *p )
{
    return l->options->address_space_bits == 39 &&
           (l->options->low_window || launcher_program_address_space( p->path ) != LAUNCHER_ADDRESS_LOW);
}

static int prepare_program_graphics( struct launcher *l, struct program *p )
{
    char dxvk[32], vkd3d[32];

    strcpy( dxvk, p->settings.dxvk_version );
    strcpy( vkd3d, p->settings.vkd3d_version );
    if (!launcher_graphics_ensure( l->graphics, p->machine,
                                  p->settings.d3d == LAUNCHER_D3D_DXVK_VKD3D,
                                  p->settings.dxvk_source, dxvk, vkd3d )) return 0;
    strcpy( p->settings.dxvk_version, dxvk );
    strcpy( p->settings.vkd3d_version, vkd3d );
    return 1;
}


/* Set by the shell: draws the screen a program starts behind. */
static void (*launcher_start_screen)( struct launcher *l, struct program *p );

static int start_program( struct launcher *l, struct program *p, char *target, size_t size )
{
    struct ui *ui = &l->ui;
    char path[512];

    launcher_log( "[SHELL] Play: %s", p->path );
    if (!file_exists( p->path ) || l->options->machine_of( p->path, &p->machine ))
    {
        struct stat st;
        int error = stat( p->path, &st ) ? errno : 0;

        launcher_log( "[SHELL] Play refused: the file %s (errno %d)", error ? "cannot be asked about" :
                      "is there but is not a program this build starts", error );
        p->missing = 1;
        ui_message( ui, "Game unavailable", "The executable is missing or is not supported by this build." );
        return 0;
    }
    if (!address_space_fits( l, p ))
    {
        ui_message( ui, "Address space unavailable", l->options->address_space_bits != 39
                    ? "Start Autorun from its 39-bit forwarder. You can install it in Settings > System."
                    : "This game requires the Atmosphere low-address patch. Restart with the patched loader and Mesosphere." );
        return 0;
    }
    /* Autorun ships no Windows DLLs: a card without them is offered them here. */
    if (!launcher_dlls_ready( l->dlls ))
    {
        launcher_log( "[SHELL] Play stopped: the Windows DLLs are not there" );
        return 0;
    }
    {
        int result = steam_local_prepare( l->options->runtime_dir, p->path, p->machine );
        if (result == STEAM_LOCAL_PAYLOAD_MISSING)
            launcher_log( "[STEAM LOCAL] optional component not installed; keeping original SDK" );
        else if (result > STEAM_LOCAL_READY)
        {
            ui_message( ui, "Local Steamworks", steam_local_error( result ) );
            return 0;
        }
    }
    if (p->settings.d3d != LAUNCHER_D3D_WINE && !prepare_program_graphics( l, p )) return 0;
    p->missing = 0;
    p->launched_order = l->catalog.next_order++;
    save_library( l );

    /* The last thing the shell shows: the program that is starting. */
    if (launcher_start_screen) launcher_start_screen( l, p );
    else
    {
        ui_background( ui );
        ui_text_centered( ui, ui->large, ui->width / 2, (ui->height - TTF_FontHeight( ui->large )) / 2,
                          "Starting game...", ui->value );
    }
    ui->hide_overlays = 1;
    ui_present( ui );

    snprintf( target, size, "%s", p->path );
    runtime_file( l, "target.txt", path, sizeof(path) );
    write_line( path, target );
    return 1;
}

static void edit_text( const char *header, char *value, size_t size )
{
    char edited[896];

    if (!launcher_platform_prompt( header, value, edited, size < sizeof(edited) ? size : sizeof(edited) )) return;
    snprintf( value, size, "%s", edited );
}

static int box64_configured_count( const struct launcher_kv *kv, int advanced )
{
    char value[64];
    int count = 0, i;

    for (i = 0; i < NX_BOX64_OPTION_COUNT; i++)
        if (nx_box64_options[i].advanced == advanced &&
            launcher_kv_get( kv, nx_box64_options[i].name, value, sizeof(value) )) count++;
    return count;
}

static int box64_option_value( const struct launcher_kv *kv, const struct nx_box64_option *option,
                               long *value, char *text, size_t size )
{
    int choice;

    if (!launcher_kv_get( kv, option->name, text, size ))
    {
        *value = option->default_value;
        choice = nx_box64_option_choice( option, *value );
        snprintf( text, size, "%s", option->value_names[choice] );
        return choice;
    }
    if (!nx_box64_option_parse_value( text, value ) ||
        (choice = nx_box64_option_choice( option, *value )) < 0)
    {
        char invalid[64];

        snprintf( invalid, sizeof(invalid), "%s", text );
        snprintf( text, size, "Unsupported (%s)", invalid );
        return -1;
    }
    snprintf( text, size, "%s", option->value_names[choice] );
    return choice;
}

static int box64_option_rows( struct ui_row *rows, int *ids, int count,
                              const struct launcher_kv *kv, int advanced )
{
    int i;

    for (i = 0; i < NX_BOX64_OPTION_COUNT; i++)
    {
        long value;

        if (nx_box64_options[i].advanced != advanced) continue;
        ids[count] = i;
        snprintf( rows[count].label, sizeof(rows[count].label), "%s", nx_box64_options[i].name );
        box64_option_value( kv, nx_box64_options + i, &value,
                            rows[count].value, sizeof(rows[count].value) );
        rows[count].help = nx_box64_options[i].help;
        rows[count].kind = UI_ROW_VALUE;
        rows[count].adjustable = 1;
        count++;
    }
    return count;
}

static void box64_options_menu( struct launcher *l, struct program *p )
{
    struct ui_row rows[NX_BOX64_OPTION_COUNT + 1];
    int ids[NX_BOX64_OPTION_COUNT + 1];
    struct ui_list list = {0};
    struct launcher_kv kv;
    char path[520];
    int count, expanded = 0, i;

    if (!launcher_sibling_path( p->path, ".box64.txt", path, sizeof(path) ) ||
        !launcher_kv_load( &kv, path ))
    {
        ui_message( &l->ui, "Box64 options", "The options file is too large to edit." );
        return;
    }
    for (;;)
    {
        enum ui_action action;
        int set;

        memset( rows, 0, sizeof(rows) );
        count = box64_option_rows( rows, ids, 0, &kv, 0 );
        set = box64_configured_count( &kv, 1 );
        ids[count] = -1;
        snprintf( rows[count].label, sizeof(rows[count].label), "Advanced flags" );
        snprintf( rows[count].value, sizeof(rows[count].value), expanded ? "Hide (%d set)" : "Show (%d set)", set );
        rows[count].help = "Compatibility and lower-level DynaRec controls supported by Wine-NX's embedded Box64 backend.";
        rows[count].kind = UI_ROW_DROPDOWN;
        rows[count].on = expanded;
        count++;
        if (expanded) count = box64_option_rows( rows, ids, count, &kv, 1 );
        action = ui_list_run( &l->ui, &list, "Box64 options", p->title, rows, count, 1 );
        if (action == UI_ACTION_BACK || action == UI_ACTION_QUIT) return;
        i = ids[list.selection];
        if (i < 0)
        {
            if (action == UI_ACTION_CHOOSE) expanded = !expanded;
            continue;
        }
        else
        {
            const struct nx_box64_option *option = nx_box64_options + i;
            char current_text[64], number[16];
            long current_value;
            int current = box64_option_value( &kv, option, &current_value,
                                               current_text, sizeof(current_text) );
            int next;

            if (action == UI_ACTION_RESET) next = nx_box64_option_choice( option, option->default_value );
            else
            {
                if (current < 0) current = nx_box64_option_choice( option, option->default_value );
                next = (current + (action == UI_ACTION_LEFT ? option->value_count - 1 : 1)) % option->value_count;
            }
            snprintf( number, sizeof(number), "%d", option->values[next] );
            if (!launcher_kv_set( &kv, option->name,
                                  option->values[next] == option->default_value ? NULL : number ) ||
                !launcher_kv_save( &kv, path ))
            {
                ui_message( &l->ui, "Box64 options", "The options could not be saved." );
                if (!launcher_kv_load( &kv, path )) return;
                continue;
            }
            load_program_settings( l, p );
        }
    }
}

static int fex_configured_count( const struct launcher_kv *kv, int advanced )
{
    char value[64];
    int count = 0, i;

    for (i = 0; i < NX_FEX_OPTION_COUNT; i++)
        if (nx_fex_options[i].advanced == advanced &&
            launcher_kv_get( kv, nx_fex_options[i].name, value, sizeof(value) )) count++;
    return count;
}

static int fex_option_value( const struct launcher_kv *kv, const struct nx_fex_option *option,
                             char *text, size_t size )
{
    char value[64];
    int choice;

    if (!launcher_kv_get( kv, option->name, value, sizeof(value) ))
        choice = option->default_choice;
    else if ((choice = nx_fex_option_choice( option, value )) < 0)
    {
        snprintf( text, size, "Unsupported (%s)", value );
        return -1;
    }
    snprintf( text, size, "%s", option->value_names[choice] );
    return choice;
}

static int fex_option_rows( struct ui_row *rows, int *ids, int count,
                            const struct launcher_kv *kv, int advanced )
{
    int i;

    for (i = 0; i < NX_FEX_OPTION_COUNT; i++)
    {
        if (nx_fex_options[i].advanced != advanced) continue;
        ids[count] = i;
        snprintf( rows[count].label, sizeof(rows[count].label), "%s", nx_fex_options[i].name );
        fex_option_value( kv, nx_fex_options + i, rows[count].value, sizeof(rows[count].value) );
        rows[count].help = nx_fex_options[i].help;
        rows[count].kind = UI_ROW_VALUE;
        rows[count].adjustable = 1;
        count++;
    }
    return count;
}

static void fex_options_menu( struct launcher *l, struct program *p )
{
    struct ui_row rows[NX_FEX_OPTION_COUNT + 1];
    int ids[NX_FEX_OPTION_COUNT + 1];
    struct ui_list list = {0};
    struct launcher_kv kv;
    char path[768];
    int count, expanded = 0, i;

    if (!launcher_program_settings_path( l->options->runtime_dir, p->path, path, sizeof(path) ) ||
        !launcher_kv_load( &kv, path ))
    {
        ui_message( &l->ui, "FEX options", "The program settings file is too large to edit." );
        return;
    }
    for (;;)
    {
        const struct nx_fex_option *option;
        enum ui_action action;
        char current_text[64];
        int current, next, set;

        memset( rows, 0, sizeof(rows) );
        count = fex_option_rows( rows, ids, 0, &kv, 0 );
        set = fex_configured_count( &kv, 1 );
        ids[count] = -1;
        snprintf( rows[count].label, sizeof(rows[count].label), "Advanced flags" );
        snprintf( rows[count].value, sizeof(rows[count].value), expanded ? "Hide (%d set)" : "Show (%d set)", set );
        rows[count].help = "Lower-level performance and compatibility controls supported by Autorun's FEX backend.";
        rows[count].kind = UI_ROW_DROPDOWN;
        rows[count].on = expanded;
        count++;
        if (expanded) count = fex_option_rows( rows, ids, count, &kv, 1 );
        action = ui_list_run( &l->ui, &list, "FEX options", p->title, rows, count, 1 );
        if (action == UI_ACTION_BACK || action == UI_ACTION_QUIT) return;
        i = ids[list.selection];
        if (i < 0)
        {
            if (action == UI_ACTION_CHOOSE) expanded = !expanded;
            continue;
        }
        option = nx_fex_options + i;
        current = fex_option_value( &kv, option, current_text, sizeof(current_text) );
        if (action == UI_ACTION_RESET) next = option->default_choice;
        else
        {
            if (current < 0) current = option->default_choice;
            next = (current + (action == UI_ACTION_LEFT ? option->value_count - 1 : 1)) % option->value_count;
        }
        if (!launcher_kv_set( &kv, option->name,
                              next == option->default_choice ? NULL : option->values[next] ) ||
            !launcher_kv_save( &kv, path ))
        {
            ui_message( &l->ui, "FEX options", "The options could not be saved." );
            if (!launcher_kv_load( &kv, path )) return;
            continue;
        }
        load_program_settings( l, p );
    }
}

/* Returns 1 when the program is to be started. */
/* The sections of Game Settings, in the order they stand in the list. */
/***********************************************************************
 * Settings
 */

static void save_look( struct launcher *l )
{
    char path[512];

    launcher_kv_set( &l->look, "theme", NULL );
    /* What the library shows is worked out from the screen now, and the
     * animations are simply on, so none of these are kept. */
    launcher_kv_set( &l->look, "animations", NULL );
    launcher_kv_set( &l->look, "columns", NULL );
    launcher_kv_set( &l->look, "rows", NULL );
    launcher_kv_set( &l->look, "show-hidden", l->show_hidden ? "1" : "0" );
    launcher_kv_set( &l->look, "hide-missing", l->hide_missing ? "1" : NULL );
#ifdef WINE_NX_SWAP_POC
    launcher_kv_set( &l->look, "swap-poc-mb", NULL );
    launcher_kv_set( &l->look, "swap-in-game", NULL );
#endif
    launcher_kv_set( &l->look, "browse", l->browse_dir );
    runtime_file( l, "launcher.txt", path, sizeof(path) );
    launcher_kv_save( &l->look, path );
}

static const struct { const char *name, *by; } credits[] =
{
    { "CORKDECK", NULL },
    { "CorkDeck", "Maj0ra'sMask  /  the shell, the settings and this fork" },
    { "Autorun", "ticoverse.com  /  the project this fork grew from" },
    { "Manrope", "Mikhail Sharanda and contributors  /  OFL-1.1" },
    { "RUNTIME & TRANSLATION", NULL },
    { "Wine", "WineHQ  /  LGPL-2.1+" },
    { "Box64", "ptitSeb  /  MIT" },
    { "FEX", "Ryan Houdek (Sonicadvance1) and contributors  /  MIT" },
    { "GRAPHICS", NULL },
    { "DXVK", "Philip Rebohle and contributors  /  zlib" },
    { "VKD3D-Proton", "Hans-Kristian Arntzen, Philip Rebohle and contributors  /  LGPL-2.1" },
    { "Mesa", "Mesa3D  /  MIT" },
    { "mesa-switch", "danfromtico, NaGaa95 and contributors" },
    { "Switch Mesa & libdrm_nouveau", "fincs, Subv, Jules Blok  /  MIT" },
    { "LSFG-VK", "Pancake (PancakeTAS) and contributors  /  GPL-3.0+" },
    { "PLATFORM", NULL },
    { "libnx", "switchbrew  /  ISC" },
    { "devkitPro", "devkitA64 and portlibs" },
    { "Atmosphere", "Atmosphere-NX  /  Horizon platform reference" },
    { "INTERFACE & LIBRARIES", NULL },
    { "SDL2 & SDL2_ttf", "Sam Lantinga and contributors  /  zlib" },
    { "FreeType", "FreeType Project  /  FTL" },
    { "HarfBuzz", "HarfBuzz authors  /  MIT" },
    { "libpng, zlib & bzip2", "libpng, zlib and BSD licenses" },
    { "TOOLS", NULL },
    { "llvm-mingw", "Martin Storsjo  /  Apache-2.0" },
    { "7-Zip", "Igor Pavlov  /  LGPL-2.1" },
    { "sphaira", "ITotalJustice, NaGaa95  /  Home menu forwarders" },
    { "REFERENCES & INSPIRATION", NULL },
    { "dolphin-nx", "NaGaa95" },
    { "tico-dolphin", "ticohq" },
    { "WineBox64 NX", "Ibnuard" },
};
#define CREDIT_COUNT (sizeof(credits) / sizeof(credits[0]))

/* The keys a controller sends, as a screen rather than a file to be written by
 * hand. One row for each control, the key it sends beside it; left and right
 * step through the keys, A opens the whole list, and Y puts a control back to
 * what it sends with no line of its own.
 *
 * What is written is the same keys.txt the runtime has always read -- a
 * NAME=code line for each control that differs -- so a file written here can
 * still be edited on a computer, and one edited there opens here. */
static unsigned short control_key( const struct launcher_kv *keys, const struct launcher_kv *under,
                                   int control )
{
    const char *name = wine_nx_controls[control].name;
    char value[64];

    if (launcher_kv_get( keys, name, value, sizeof(value) ) && value[0])
        return (unsigned short)strtoul( value, NULL, 0 );
    /* A program's own keys are applied over the shared ones, so a control the
     * program says nothing about shows what the shared file gives it. */
    if (under && launcher_kv_get( under, name, value, sizeof(value) ) && value[0])
        return (unsigned short)strtoul( value, NULL, 0 );
    return wine_nx_controls[control].sends;
}

static void set_control_key( struct launcher_kv *keys, int control, int code )
{
    char value[32];

    /* Back to the default is the line taken away, not a line saying the
     * default: a later build that changes what a control sends should reach a
     * controller nobody has touched. */
    if (code < 0) launcher_kv_set( keys, wine_nx_controls[control].name, NULL );
    else
    {
        snprintf( value, sizeof(value), "0x%02x", code );
        launcher_kv_set( keys, wine_nx_controls[control].name, value );
    }
}

/* What one of the three that point is set to do. The keys file says it in
 * words -- LSTICK=mouse -- because it is not a key. */
enum device_choice { DEVICE_MOUSE, DEVICE_DPAD, DEVICE_ARROWS, DEVICE_WASD, DEVICE_CUSTOM };

static const char *const device_choice_names[] =
    { "Mouse", "Same as d-pad", "Arrow keys", "W A S D", "Its own keys" };

static int device_points( const struct launcher_kv *keys, const struct launcher_kv *under, int device )
{
    const char *name = wine_nx_devices[device].name;
    char value[32];

    if (launcher_kv_get( keys, name, value, sizeof(value) ) && value[0])
        return strcasecmp( value, "keys" ) == 0 ? 0 : 1;
    if (under && launcher_kv_get( under, name, value, sizeof(value) ) && value[0])
        return strcasecmp( value, "keys" ) == 0 ? 0 : 1;
    return wine_nx_devices[device].points;
}

static enum device_choice device_choice( const struct launcher_kv *keys, const struct launcher_kv *under,
                                         int device )
{
    const struct wine_nx_device *d = &wine_nx_devices[device];
    unsigned short code[4];
    int i, unset = 1;

    if (device_points( keys, under, device )) return DEVICE_MOUSE;
    for (i = 0; i < 4; i++)
    {
        code[i] = control_key( keys, under, d->first + i );
        if (code[i]) unset = 0;
    }
    if (unset && d->follows_dpad) return DEVICE_DPAD;
    if (!memcmp( code, wine_nx_preset_arrows, sizeof(code) )) return DEVICE_ARROWS;
    if (!memcmp( code, wine_nx_preset_wasd, sizeof(code) )) return DEVICE_WASD;
    return DEVICE_CUSTOM;
}

static void set_device_choice( struct launcher_kv *keys, int device, enum device_choice choice )
{
    const struct wine_nx_device *d = &wine_nx_devices[device];
    const unsigned short *preset = choice == DEVICE_WASD ? wine_nx_preset_wasd : wine_nx_preset_arrows;
    int i;

    launcher_kv_set( keys, d->name, choice == DEVICE_MOUSE ? "mouse" : "keys" );
    if (choice == DEVICE_MOUSE || choice == DEVICE_CUSTOM) return;
    for (i = 0; i < 4; i++)
        set_control_key( keys, d->first + i, choice == DEVICE_DPAD ? -1 : preset[i] );
}

/* Round the choices for this one, leaving out those it does not have. */
static enum device_choice next_device_choice( int device, enum device_choice from, int forward )
{
    int at = from == DEVICE_CUSTOM ? DEVICE_CUSTOM : from;

    for (;;)
    {
        at += forward ? 1 : -1;
        if (at < 0) at = DEVICE_CUSTOM;
        if (at > DEVICE_CUSTOM) at = DEVICE_MOUSE;
        /* Its own keys is where a file written by hand puts it, not somewhere
         * to step into; the d-pad is the left stick's alone. */
        if (at == DEVICE_CUSTOM) continue;
        if (at == DEVICE_DPAD && !wine_nx_devices[device].follows_dpad) continue;
        return (enum device_choice)at;
    }
}

/* The whole list of keys, starting on the one the control sends now. Returns
 * the code chosen, or -1 for the way out. */
static int key_screen( struct launcher *l, const char *control_label, unsigned short current )
{
    static struct ui_row rows[WINE_NX_KEY_NAME_COUNT];
    struct ui_list list = {0};
    char title[128];
    int i, at = wine_nx_key_index( current );

    memset( rows, 0, sizeof(rows) );
    for (i = 0; i < WINE_NX_KEY_NAME_COUNT; i++)
    {
        snprintf( rows[i].label, sizeof(rows[i].label), "%s", wine_nx_key_names[i].name );
        if (wine_nx_key_names[i].code)
            snprintf( rows[i].value, sizeof(rows[i].value), "0x%02x", wine_nx_key_names[i].code );
    }
    snprintf( title, sizeof(title), "%s sends", control_label );
    list.selection = at > 0 ? at : 0;
    for (;;)
    {
        enum ui_action action = ui_list_run( &l->ui, &list, title, "Controls", rows,
                                             WINE_NX_KEY_NAME_COUNT, 0 );

        if (action == UI_ACTION_BACK || action == UI_ACTION_QUIT) return -1;
        if (action == UI_ACTION_CHOOSE) return wine_nx_key_names[list.selection].code;
    }
}

static void controls_screen( struct launcher *l, const char *path, const char *under_path,
                             const char *title )
{
    struct ui_row rows[WINE_NX_DEVICE_COUNT_UI + WINE_NX_CONTROL_COUNT];
    struct launcher_kv keys, under;
    struct ui_list list = {0};
    int changed = 0, i;

    if (under_path) launcher_kv_load( &under, under_path );
    if (!launcher_kv_load( &keys, path ))
    {
        ui_message( &l->ui, "Controls", "That file is too large to open here; edit it on a computer." );
        ui_start_screen( &l->ui );
        return;
    }
    for (;;)
    {
        const struct launcher_kv *base = under_path ? &under : NULL;
        enum ui_action action;
        char label[64];
        unsigned short code;
        int device;

        memset( rows, 0, sizeof(rows) );
        for (device = 0; device < WINE_NX_DEVICE_COUNT_UI; device++)
        {
            snprintf( rows[device].label, sizeof(rows[0].label), "%s", wine_nx_devices[device].label );
            snprintf( rows[device].value, sizeof(rows[0].value), "%s",
                      device_choice_names[device_choice( &keys, base, device )] );
            rows[device].adjustable = 1;
            rows[device].kind = UI_ROW_VALUE;
            rows[device].help = "Move the mouse, or send four keys. A game played with the mouse "
                                "wants both sticks on it; one played with the keyboard wants the "
                                "keys it walks with.";
        }
        for (i = 0; i < WINE_NX_CONTROL_COUNT; i++)
        {
            struct ui_row *row = &rows[WINE_NX_DEVICE_COUNT_UI + i];

            snprintf( row->label, sizeof(row->label), "%s", wine_nx_controls[i].label );
            snprintf( row->value, sizeof(row->value), "%s",
                      wine_nx_key_label( i, control_key( &keys, base, i ), label, sizeof(label) ) );
            row->adjustable = 1;
            row->kind = UI_ROW_VALUE;
            /* A stick on the mouse has no keys to give: say so rather than
             * offer four rows that do nothing. */
            for (device = 0; device < WINE_NX_DEVICE_COUNT_UI; device++)
            {
                if (i < wine_nx_devices[device].first || i >= wine_nx_devices[device].first + 4) continue;
                if (!device_points( &keys, base, device )) continue;
                snprintf( row->value, sizeof(row->value), "Moves the mouse" );
                row->adjustable = 0;
                row->disabled = 1;
            }
        }
        rows[WINE_NX_DEVICE_COUNT_UI].help =
            "A and B are the mouse buttons until they are given a key of their own.";
        action = ui_list_run( &l->ui, &list, title, "Controls", rows,
                              WINE_NX_DEVICE_COUNT_UI + WINE_NX_CONTROL_COUNT, 1 );
        if (action == UI_ACTION_BACK || action == UI_ACTION_QUIT) break;
        if (list.selection < WINE_NX_DEVICE_COUNT_UI)
        {
            enum device_choice choice = device_choice( &keys, base, list.selection );

            if (action == UI_ACTION_LEFT || action == UI_ACTION_RIGHT || action == UI_ACTION_CHOOSE)
            {
                set_device_choice( &keys, list.selection,
                                   next_device_choice( list.selection, choice,
                                                       action != UI_ACTION_LEFT ) );
                changed = 1;
            }
            else if (action == UI_ACTION_RESET)
            {
                launcher_kv_set( &keys, wine_nx_devices[list.selection].name, NULL );
                changed = 1;
            }
            continue;
        }
        i = list.selection - WINE_NX_DEVICE_COUNT_UI;
        code = control_key( &keys, base, i );
        switch (action)
        {
        case UI_ACTION_CHOOSE:
        {
            int picked = key_screen( l, wine_nx_controls[i].label, code );

            ui_start_screen( &l->ui );
            if (picked >= 0 && picked != code) { set_control_key( &keys, i, picked ); changed = 1; }
            break;
        }
        case UI_ACTION_LEFT:
        case UI_ACTION_RIGHT:
        {
            int at = wine_nx_key_index( code );

            /* A code the list does not name steps from the start rather than
             * nowhere, so a hand-written file can be changed here too. */
            if (at < 0) at = 0;
            else at += action == UI_ACTION_RIGHT ? 1 : -1;
            if (at < 0) at = WINE_NX_KEY_NAME_COUNT - 1;
            if (at >= WINE_NX_KEY_NAME_COUNT) at = 0;
            set_control_key( &keys, i, wine_nx_key_names[at].code );
            changed = 1;
            break;
        }
        case UI_ACTION_RESET:
            set_control_key( &keys, i, -1 );
            changed = 1;
            break;
        default: break;
        }
    }
    if (changed && !launcher_kv_save( &keys, path ))
    {
        ui_message( &l->ui, "Controls", "The keys could not be saved to the card." );
        ui_start_screen( &l->ui );
    }
}

static void credits_screen( struct launcher *l )
{
    struct ui *ui = &l->ui;
    struct ui_input input;
    const SDL_Color background = { 12, 17, 23, 255 }, accent = { 26, 159, 255, 255 };
    const int logo_width = 320, title_gap = 256, entry_height = 92, section_height = 132;
    int logo_height = 0, width, height, y, end_y = ui->height / 2 - 50 + title_gap;
    int old_hide_overlays = ui->hide_overlays, done = 0, manual = 0;
    Uint32 previous = SDL_GetTicks(), finished = 0;
    float scroll = 0, distance;
    size_t i;

    if (l->logo && !SDL_QueryTexture( l->logo, NULL, NULL, &width, &height ) && width > 0)
        logo_height = height * logo_width / width;
    for (i = 0; i < CREDIT_COUNT; i++)
        end_y += credits[i].by ? entry_height : section_height;
    end_y += ui->height / 2;
    distance = end_y - (ui->height - logo_height - 100) / 2;
    ui->hide_overlays = 1;
    ui->footer_count = 0;
    ui_start_screen( ui );
    while (!done && ui_begin_frame( ui ))
    {
        Uint32 now = SDL_GetTicks();
        float elapsed = now - previous, speed = 0;
        previous = now;
        if (elapsed > 50) elapsed = 50;

        while (ui_poll( ui, &input ))
        {
            if (input.button == UI_B || input.button == UI_PLUS || input.touch == UI_TOUCH_TAP ||
                (scroll >= distance && input.button == UI_A)) done = 1;
            if ((input.button == UI_UP || input.button == UI_DOWN) && abs(ui->axes[SDL_CONTROLLER_AXIS_LEFTY]) < 6000)
            {
                manual = 1;
                scroll += input.button == UI_UP ? -72 : 72;
            }
            if (input.touch == UI_TOUCH_SCROLL_UP || input.touch == UI_TOUCH_SCROLL_DOWN)
            {
                manual = 1;
                scroll += (input.touch == UI_TOUCH_SCROLL_UP ? 1 : -1) * input.steps * 36;
            }
        }
        for (i = 0; i < 4; i++) if (abs(ui->axes[i]) > 6000) manual = 1;
        if (done || !ui->running) break;
        if (!manual) speed = 0.08184f;
        else if (abs(ui->axes[SDL_CONTROLLER_AXIS_LEFTY]) > 6000)
        {
            int axis = ui->axes[SDL_CONTROLLER_AXIS_LEFTY];
            speed = (axis > 0 ? 1 : -1) * 0.65f * (abs(axis) - 6000) / (32768 - 6000);
        }
        scroll = fmaxf(0, fminf(distance, scroll + speed * elapsed));
        if (manual) finished = 0;
        else if (scroll >= distance)
        {
            scroll = distance;
            if (!finished) finished = now;
            if (now - finished >= 5000) break;
        }
        ui_fill( ui, 0, 0, ui->width, ui->height, background );
        y = ui->height / 2 - 50 - (int)scroll;
        if (y > -100)
        {
            ui_text_centered( ui, ui->large, ui->width / 2, y, "CorkDeck", ui->value );
            ui_text_centered( ui, ui->normal, ui->width / 2, y + 52, "People and projects behind it", ui->dim );
        }
        y += title_gap;
        for (i = 0; i < CREDIT_COUNT; i++)
        {
            int section = !credits[i].by;
            int block_height = section ? section_height : entry_height;

            if (y + block_height > 0 && y < ui->height)
            {
                if (section)
                {
                    ui_fill( ui, ui->width / 2 - 24, y + 32, 48, 2, accent );
                    ui_text_centered( ui, ui->small, ui->width / 2, y + 56, credits[i].name, accent );
                }
                else
                {
                    ui_text_centered( ui, ui->normal, ui->width / 2, y, credits[i].name, ui->value );
                    ui_text_centered( ui, ui->small, ui->width / 2, y + 36, credits[i].by, ui->dim );
                }
            }
            y += block_height;
        }
        y = end_y - (int)scroll;
        if (y < ui->height)
        {
            if (logo_height)
            {
                SDL_Rect rect = { (ui->width - logo_width) / 2, y, logo_width, logo_height };

                SDL_SetTextureColorMod( l->logo, 255, 255, 255 );
                SDL_SetTextureAlphaMod( l->logo, 255 );
                SDL_RenderCopy( ui->renderer, l->logo, NULL, &rect );
            }
            ui_text_centered( ui, ui->large, ui->width / 2, y + logo_height + 16, "CorkDeck", ui->value );
            ui_text_centered( ui, ui->normal, ui->width / 2, y + logo_height + 64, "A fork of Autorun (ticoverse.com)", accent );
        }
        ui_gradient( ui, 0, 0, ui->width, 80, background, (SDL_Color){ 12, 17, 23, 0 }, 0 );
        ui_gradient( ui, 0, ui->height - 80, ui->width, 80, (SDL_Color){ 12, 17, 23, 0 }, background, 0 );
        ui_fade( ui );
        if (finished && now - finished > 4600)
            ui_fill( ui, 0, 0, ui->width, ui->height, (SDL_Color){ 12, 17, 23, (now - finished - 4600) * 255 / 400 } );
        ui->scrolling_text = !manual || speed != 0;
        ui_present( ui );
        ui_wait( ui );
    }
    if (done) ui_sound( ui, LAUNCHER_SOUND_BACK );
    ui->hide_overlays = old_hide_overlays;
    ui_start_screen( ui );
}

/***********************************************************************
 * File browser
 */

static int compare_files( const void *a, const void *b )
{
    const struct file_entry *x = a, *y = b;

    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
    return strcasecmp( x->name, y->name );
}

static int read_dir( struct launcher *l, const char *dir, int *count, int images )
{
    struct dirent *entry;
    DIR *handle;

    *count = 0;
    if (!(handle = opendir( dir ))) return 0;
    while (*count < MAX_FILES && (entry = readdir( handle )))
    {
        struct file_entry *file = files + *count;
        char path[512];
        struct stat st;

        if (entry->d_name[0] == '.') continue;
        if ((size_t)snprintf( path, sizeof(path), "%s%s%s", dir, is_root( dir ) && dir[strlen( dir ) - 1] == '/' ? "" : "/",
                              entry->d_name ) >= sizeof(path)) continue;
        if (entry->d_type == DT_DIR) file->is_dir = 1;
        else if (entry->d_type == DT_REG) file->is_dir = 0;
        else if (stat( path, &st )) continue;
        else file->is_dir = S_ISDIR( st.st_mode );
        if (!file->is_dir)
        {
            const char *extension = strrchr( entry->d_name, '.' );
            if (images ? (!extension || (strcasecmp( extension, ".png" ) && strcasecmp( extension, ".jpg" ) &&
                                        strcasecmp( extension, ".jpeg" ))) : !launcher_is_exe( entry->d_name )) continue;
        }
        snprintf( file->name, sizeof(file->name), "%s", entry->d_name );
        file->supported = images || file->is_dir || !l->options->machine_of( path, &file->machine );
        (*count)++;
    }
    closedir( handle );
    qsort( files, *count, sizeof(files[0]), compare_files );
    return 1;
}

static void join_path( char *out, size_t size, const char *dir, const char *name )
{
    snprintf( out, size, "%s%s%s", dir, dir[strlen( dir ) - 1] == '/' ? "" : "/", name );
}

static int directory_exists( const char *path )
{
    struct stat st;

    return !stat( path, &st ) && S_ISDIR( st.st_mode );
}

static int path_below( const char *path, const char *root )
{
    size_t length = strlen( root );

    return !strncmp( path, root, length );
}

/* Choose the filesystem before showing any folders. The saved directory is
 * kept within the chosen filesystem, but never skips this screen. */
static int file_browser_storage( struct launcher *l, char *dir, size_t size, const char *title )
{
    struct ui *ui = &l->ui;
    static const char *const usb_paths[] = { "ums0:/", "ums1:/", "ums2:/", "ums3:/", "ums4:/" };
    static const char *const usb_names[] = { "USB 1", "USB 2", "USB 3", "USB 4", "USB 5" };
    struct wine_nx_launcher_usb_volume mounted[LAUNCHER_MAX_USB_VOLUMES];
    int mounted_count, i;

    for (;;)
    {
        struct ui_list list = {0};
        struct ui_row rows[2] = {0};
        enum ui_action action;

        memset( mounted, 0, sizeof(mounted) );
        if (l->options->list_usb)
            mounted_count = l->options->list_usb( mounted, LAUNCHER_MAX_USB_VOLUMES );
        else
        {
            mounted_count = 0;
            for (i = 0; i < LAUNCHER_USB_DRIVES; i++)
                if (directory_exists( usb_paths[i] ))
                {
                    snprintf( mounted[mounted_count].path, sizeof(mounted[mounted_count].path), "%s", usb_paths[i] );
                    snprintf( mounted[mounted_count].label, sizeof(mounted[mounted_count].label), "%s", usb_names[i] );
                    mounted[mounted_count++].drive = 'D' + i;
                }
        }

        snprintf( rows[0].label, sizeof(rows[0].label), "SD Card" );
        snprintf( rows[0].value, sizeof(rows[0].value), "C: and Z:" );
        snprintf( rows[1].label, sizeof(rows[1].label), "USB" );
        if (mounted_count)
            snprintf( rows[1].value, sizeof(rows[1].value), mounted_count == 1 ? "1 volume" : "%d volumes",
                      mounted_count );
        else
            snprintf( rows[1].value, sizeof(rows[1].value), "Not connected" );

        action = ui_list_run( ui, &list, title, "Choose storage", rows, 2, 0 );
        if (action == UI_ACTION_BACK || action == UI_ACTION_QUIT) return 0;
        if (action != UI_ACTION_CHOOSE) continue;
        if (!list.selection)
        {
            if (path_below( l->browse_dir, "sdmc:/" ) && directory_exists( l->browse_dir ))
                snprintf( dir, size, "%s", l->browse_dir );
            else if (directory_exists( LAUNCHER_DRIVE_C ))
                snprintf( dir, size, "%s", LAUNCHER_DRIVE_C );
            else snprintf( dir, size, "sdmc:/" );
            return 1;
        }
        if (list.selection == 1 && !mounted_count)
        {
            ui_message( ui, "USB", "No mounted USB volume was found. Connect a drive, then try again." );
            continue;
        }
        if (list.selection == 1 && mounted_count == 1)
        {
            if (path_below( l->browse_dir, mounted[0].path ) && directory_exists( l->browse_dir ))
                snprintf( dir, size, "%s", l->browse_dir );
            else snprintf( dir, size, "%s", mounted[0].path );
            return 1;
        }
        if (list.selection == 1)
        {
            struct ui_list volumes = {0};
            enum ui_action volume_action;

            for (i = 0; i < mounted_count; i++)
            {
                memset( file_rows + i, 0, sizeof(file_rows[0]) );
                snprintf( file_rows[i].label, sizeof(file_rows[i].label), "%s", mounted[i].label );
                snprintf( file_rows[i].value, sizeof(file_rows[i].value), "%c:", mounted[i].drive );
            }
            volume_action = ui_list_run( ui, &volumes, "USB", "Choose a volume", file_rows,
                                         mounted_count, 0 );
            if (volume_action == UI_ACTION_QUIT) return 0;
            if (volume_action == UI_ACTION_BACK) continue;
            if (volume_action != UI_ACTION_CHOOSE) continue;
            if (path_below( l->browse_dir, mounted[volumes.selection].path ) && directory_exists( l->browse_dir ))
                snprintf( dir, size, "%s", l->browse_dir );
            else snprintf( dir, size, "%s", mounted[volumes.selection].path );
            return 1;
        }
    }
}

static int file_browser_pick_kind( struct launcher *l, char *target, size_t size, int images )
{
    struct ui *ui = &l->ui;
    char dir[512], came_from[256] = "", dos[512], path[512];

    for (;;)
    {
        int choose_storage = 0;

        if (!file_browser_storage( l, dir, sizeof(dir), images ? "Choose icon" : "Add Game" ))
        {
            save_look( l );
            return 0;
        }
        came_from[0] = 0;
        while (!choose_storage)
        {
            struct ui_list list = {0};
            int count, has_up, readable, rows, i, reload = 0;

            while (!(readable = read_dir( l, dir, &count, images )) && !is_root( dir )) parent_dir( dir );
            if (!readable)
            {
                choose_storage = 1;
                continue;
            }
            snprintf( l->browse_dir, sizeof(l->browse_dir), "%s", dir );
            has_up = !is_root( dir );
            if (!launcher_dos_path( dir, dos, sizeof(dos) )) snprintf( dos, sizeof(dos), "%s", dir );

            rows = 0;
            if (has_up)
            {
                char parent[512], parent_dos[512];

                snprintf( parent, sizeof(parent), "%s", dir );
                parent_dir( parent );
                memset( file_rows, 0, sizeof(file_rows[0]) );
                snprintf( file_rows[0].label, sizeof(file_rows[0].label), "Up one folder" );
                if (launcher_dos_path( parent, parent_dos, sizeof(parent_dos) ))
                    snprintf( file_rows[0].value, sizeof(file_rows[0].value), "%s", parent_dos );
                rows = 1;
            }
            for (i = 0; i < count; i++, rows++)
            {
                struct ui_row *row = file_rows + rows;

                memset( row, 0, sizeof(*row) );
                snprintf( row->label, sizeof(row->label), "%s", files[i].name );
                if (files[i].is_dir) snprintf( row->value, sizeof(row->value), "Folder" );
                else if (!files[i].supported)
                {
                    snprintf( row->value, sizeof(row->value), "Cannot run here" );
                    row->disabled = 1;
                }
                else snprintf( row->value, sizeof(row->value), "%s", images ? "Image" : launcher_machine_name( files[i].machine ) );
                if (came_from[0] && !strcasecmp( files[i].name, came_from )) list.selection = rows;
            }
            if (!rows)
            {
                memset( file_rows, 0, sizeof(file_rows[0]) );
                snprintf( file_rows[0].label, sizeof(file_rows[0].label), "%s", images ? "No folders or images here" : "No folders or programs here" );
                file_rows[0].disabled = 1;
                rows = 1;
            }
            /* A folder opens on its first entry; going up returns to the folder left. */
            if (!came_from[0] && has_up && rows > 1) list.selection = 1;
            came_from[0] = 0;

            while (!reload)
            {
                enum ui_action action = ui_list_run( ui, &list, images ? "Choose icon" : "Files", dos, file_rows, rows, 0 );
                int index = list.selection - has_up;

                if (action == UI_ACTION_QUIT) return 0;
                if (action == UI_ACTION_BACK || (action == UI_ACTION_CHOOSE && index < 0))
                {
                    if (!has_up)
                    {
                        choose_storage = 1;
                        break;
                    }
                    snprintf( came_from, sizeof(came_from), "%s", file_name( dir ) );
                    parent_dir( dir );
                    reload = 1;
                    break;
                }
                if (action != UI_ACTION_CHOOSE) continue;
                if (index < 0 || index >= count) continue;
                join_path( path, sizeof(path), dir, files[index].name );
                launcher_log( "[LAUNCHER] Browser chose %s (%s)", path,
                              files[index].is_dir ? "folder" : images ? "image" : "program" );
                if (files[index].is_dir)
                {
                    snprintf( dir, sizeof(dir), "%s", path );
                    reload = 1;
                }
                else
                {
                    snprintf( target, size, "%s", path );
                    save_look( l );
                    return 1;
                }
            }
        }
    }
}

static int file_browser_pick( struct launcher *l, char *target, size_t size )
{
    return file_browser_pick_kind( l, target, size, 0 );
}

static int pick_forwarder_icon( void *opaque, char *target, size_t size )
{
    struct launcher *l = opaque;
    char previous[sizeof(l->browse_dir)];
    int result;
    strcpy( previous, l->browse_dir );
    result = file_browser_pick_kind( l, target, size, 1 );
    strcpy( l->browse_dir, previous );
    save_look( l );
    return result;
}

/* Steam ends at files and catalog registration. Execution uses the existing
 * describe/save/settings functions, exactly as manually added programs do. */
static int steam_choose_executable( void *context, char *path, unsigned int size )
{
    return file_browser_pick( context, path, size );
}
/* Steam metadata must not persist implicit launch defaults. In particular,
 * writing all settings here would suppress the normal first-add renderer rule. */
static int steam_save_title( struct launcher *l, struct program *p, const char *title )
{
    struct launcher_kv kv;
    char path[768];

    if (!launcher_program_settings_path( l->options->runtime_dir, p->path, path, sizeof(path) ) ||
        !launcher_kv_load( &kv, path ) || !launcher_kv_set( &kv, "title", title ) ||
        !launcher_kv_save( &kv, path )) return 0;
    load_program_settings( l, p );
    return 1;
}
static void steam_shell_header( void *context, int section, int focused )
{
    struct launcher *l = context;
    int zone = l->zone, focus = l->header_focus;
    l->zone = focused ? ZONE_HEADER : ZONE_CONTENT; l->header_focus = section;
    draw_shell_active( l, 0, section == SHELL_DOWNLOADS ? SHELL_DOWNLOADS : SHELL_STEAM );
    l->zone = zone; l->header_focus = focus;
}
static int steam_shell_hit( void *context, int x, int y )
{
    struct launcher *l = context;
    SDL_Point point = { x, y };
    int i;
    for (i = 0; i < SHELL_TABS; ++i)
        if (SDL_PointInRect( &point, &l->shell_hits[i] )) return i;
    return -1;
}

static int steam_register_executable( void *context, const char *path, const char *title )
{
    struct launcher *l = context;
    struct program program;
    int index = find_program( l, path );
    if (index >= 0)
    {
        struct program *existing = &l->programs[index];
        if (title && title[0] && !existing->settings.title[0])
        {
            if (!steam_save_title( l, existing, title )) return 0;
            if (!save_library(l)) return 0;
            rebuild_visible(l,index);rebuild_history(l,index);
        }
        return 1;
    }
    if (l->program_count >= LAUNCHER_MAX_ENTRIES || !describe_program( l, &program, path )) return 0;
    if (title && title[0] && !program.settings.title[0])
    {
        if (!steam_save_title( l, &program, title )) return 0;
    }
    index = l->program_count;
    program.added = 1;
    program.catalog_id = l->catalog.next_id;
    program.added_order = l->catalog.next_order;
    l->programs[l->program_count++] = program;
    if (!save_library( l )) { l->program_count--; return 0; }
    apply_new_game_renderer( l, &l->programs[index] );
    rebuild_visible( l, index );
    rebuild_history( l, index );
    return 1;
}
static int steam_launch_prerequisite(void *context,const char *path)
{
    struct launcher *l=context;
    struct program program;
    if(l->prerequisite_target[0] || !describe_program(l,&program,path))return 0;
    snprintf(l->prerequisite_target,sizeof(l->prerequisite_target),"%s",path);
    launcher_log("[PREREQUISITES] queued ordinary Windows executable");
    return 1;
}
static int steam_unregister_directory( void *context, const char *directory )
{
    struct launcher *l = context;
    unsigned char changed[LAUNCHER_MAX_ENTRIES] = {0};
    size_t len = strlen(directory);
    int i;
    for (i = 0; i < l->program_count; ++i)
        if (!l->programs[i].removed && !strncmp(l->programs[i].path,directory,len) && l->programs[i].path[len] == '/')
        { l->programs[i].removed = 1; changed[i] = 1; }
    if (!save_library(l))
    { for (i = 0; i < l->program_count; ++i) if (changed[i]) l->programs[i].removed = 0; return 0; }
    rebuild_visible(l,-1);rebuild_history(l,-1);return 1;
}
static int add_game( struct launcher *l )
{
    struct program program;
    char path[512], message[800];
    int index;

    if (!file_browser_pick( l, path, sizeof(path) )) return -1;
    launcher_log( "[LAUNCHER] Add Game selected %s", path );
    if ((index = find_program( l, path )) >= 0)
    {
        ui_message( &l->ui, "Add Game", "This game is already in your library." );
        return index;
    }
    if (!describe_program( l, &program, path ))
    {
        ui_message( &l->ui, "Add Game", "Autorun cannot run this executable." );
        return -1;
    }
    for (;;)
    {
        static const struct ui_hint hints[] = { { UI_A, "Add" }, { UI_X, "Rename" }, { UI_B, "Cancel" } };
        char name[128];
        int answer;

        snprintf( message, sizeof(message), "%s\n\n%s\n\nAdd this game to your library?",
                  program.title, program.dos );
        answer = ui_ask( &l->ui, "Review Game", message, hints, 3 );
        if (answer == UI_A) break;
        if (answer != UI_X)
        {
            launcher_log( "[LAUNCHER] Add Game cancelled during review" );
            return -1;
        }
        /* Named before it is added, because the name is what the library is
         * sorted and searched by. It is kept in the file beside the program. */
        snprintf( name, sizeof(name), "%s", program.title );
        if (launcher_platform_prompt( "Name", name, name, sizeof(name) ) && name[0])
        {
            snprintf( program.settings.title, sizeof(program.settings.title), "%s", name );
            snprintf( program.title, sizeof(program.title), "%s", name );
            save_program_settings( l, &program );
        }
        ui_start_screen( &l->ui );
    }
    if (l->program_count >= LAUNCHER_MAX_ENTRIES)
    {
        ui_message( &l->ui, "Add Game", "The library is full." );
        return -1;
    }
    index = l->program_count;
    l->programs[index] = program;
    l->programs[index].added = 1;
    l->programs[index].catalog_id = l->catalog.next_id;
    l->programs[index].added_order = l->catalog.next_order;
    l->program_count++;
    if (!save_library( l ))
    {
        l->program_count--;
        return -1;
    }
    apply_new_game_renderer( l, &l->programs[index] );
    launcher_log( "[LAUNCHER] Added %s to the library", path );
    ui_toast( &l->ui, "Game added to the library", 1800 );
    return index;
}

/***********************************************************************
 * The library and the entry point
 */

/* A game can be in either list, so both are rebuilt together. */
static void rebuild_lists( struct launcher *l, int keep_index )
{
    rebuild_visible( l, keep_index );
    rebuild_history( l, keep_index );
}

static int usb_program_path( const char *path )
{
    return !strncasecmp( path, "ums", 3 ) && path[3] >= '0' && path[3] <= '4' &&
           path[4] == ':' && (path[5] == '/' || path[5] == '\\');
}

static int refresh_usb_programs( struct launcher *l )
{
    int changed = 0, i;

    for (i = 0; i < l->program_count; i++)
    {
        struct program *p = l->programs + i;
        unsigned short machine;
        int available;

        if (p->removed || !usb_program_path( p->path )) continue;
        available = file_exists( p->path ) && !l->options->machine_of( p->path, &machine );
        if (available == !p->missing) continue;
        p->missing = !available;
        if (available)
        {
            p->machine = machine;
            p->resource_title[0] = 0;
            launcher_pe_describe( p->path, 0, NULL, p->resource_title, sizeof(p->resource_title) );
            load_program_settings( l, p );
            if (!p->icon) p->icon_state = ICON_UNKNOWN;
            if (!p->square_icon) p->square_state = ICON_UNKNOWN;
            if (!p->hero_icon) p->hero_state = ICON_UNKNOWN;
        }
        changed++;
    }
    return changed;
}

/* Art asked for before the first frame, as tico does, so neither Home nor the
 * library opens on bare cards: the least wanted first, since the worker takes
 * the newest request first -- the library's first pages, then Home's covers,
 * then the backdrop Home opens on. */
static void preload_art( struct launcher *l )
{
    struct grid g;
    int i;

    if (!l->thread) return;
    grid_layout( l, &g );
    for (i = g.first + 3 * g.columns * g.rows; i-- > g.first;)
        if (i < l->visible_count) prefetch_card( l, i );
    for (i = l->history_count; i-- > 0;) request_icon( l, l->history[i] );
    for (i = 2; i >= 0; i--)
    {
        if (l->history_selection + i < l->history_count) request_art( l, l->history[l->history_selection + i], ART_HERO );
        if (i && l->history_selection - i >= 0) request_art( l, l->history[l->history_selection - i], ART_HERO );
    }
}

#include "shell/shell.inc"

/* What runs between frames: the DLL check and the Steam service. */
static void background_tick( void *data )
{
    struct launcher *l = data;

    launcher_dlls_tick( l->dlls );
    steam_ui_tick( &l->ui, l->options->runtime_dir );
}

int wine_nx_launcher_run( struct wine_nx_launcher_options *options, char *target, size_t target_size )
{
    struct launcher *l = &launcher;
    const void *font = NULL;
    size_t font_size = 0;
    char path[512];
    int ret, i, added, missing;
    Uint32 started;

    memset( l, 0, sizeof(*l) );
    l->options = options;
    runtime_file( l, "launcher.txt", path, sizeof(path) );
    launcher_kv_load( &l->look, path );
    l->show_hidden = launcher_kv_get_int( &l->look, "show-hidden", 0 ) == 1;
    l->hide_missing = launcher_kv_get_int( &l->look, "hide-missing", 0 ) == 1;
    if (!launcher_kv_get( &l->look, "browse", l->browse_dir, sizeof(l->browse_dir) ) || !l->browse_dir[0])
        snprintf( l->browse_dir, sizeof(l->browse_dir), "%s", LAUNCHER_DRIVE_C );

    started = SDL_GetTicks();
    /* The steps of coming up, on the card before each is taken: the launcher
     * that froze a console said nothing between the settings and its first
     * frame, and the log has to name the call that did not return. */
    launcher_log( "[LAUNCHER] bringing the screen up: the shared font" );
    {
        /* CorkDeck's own face (assets/fonts, built into the program); the
         * console's shared font only when it could not be opened. */
        extern const unsigned char corkdeck_font[];
        extern const size_t corkdeck_font_size;

        if (ui_init( &l->ui, corkdeck_font, corkdeck_font_size, 1 )) font = corkdeck_font;
    }
    if (!font && (!launcher_platform_font( &font, &font_size ) ||
        !ui_init( &l->ui, font, font_size, 1 )))
    {
        launcher_platform_font_release();
        /* libnx's console cannot draw once EGL has had the screen. */
        if (ui_screen_used())
        {
            launcher_log( "[LAUNCHER] SDL could not start (%s) after taking the screen; closing", ui_error() );
            return 0;
        }
        launcher_log( "[LAUNCHER] SDL could not start (%s); showing the text menu",
                      font ? ui_error() : "no shared font from the pl service" );
#ifdef __SWITCH__
        consoleInit( NULL );
        ret = wine_nx_launcher_console_run( LAUNCHER_DRIVE_C, options->runtime_dir, options->build, options->machine_of,
                                            &options->verbose, &options->profile, target, target_size );
        consoleExit( NULL );
        return ret;
#else
        return 0;
#endif
    }
    /* ui_init starts from a cleared screen, so the clock and the battery are
     * handed to it once it stands. */
    l->ui.header_status = header_status;
    l->ui.header_status_data = l;
    l->ui.footer_mark = footer_mark;
    l->usb_event = SDL_RegisterEvents( 1 );
    if (l->usb_event == (Uint32)-1) l->usb_event = SDL_USEREVENT;

    {
        SDL_RendererInfo info;

        if (SDL_GetRendererInfo( l->ui.renderer, &info )) info.name = "unknown";
        launcher_log( "[LAUNCHER] SDL %s video, %s renderer, font %zu bytes, ready in %u ms",
                      SDL_GetCurrentVideoDriver(), info.name, font_size,
                      SDL_GetTicks() - started );
    }
    {
        static const struct { const struct launcher_svg_icon *icon; int size; } symbols[SYMBOL_COUNT] =
        {
            [SYMBOL_HOME] = { &icon_gamepad_modern, SHELL_ICON },
            [SYMBOL_LIBRARY] = { &icon_grid, SHELL_ICON - 4 },
            [SYMBOL_STEAM] = { &icon_gamepad_modern, SHELL_ICON - 4 },
            [SYMBOL_DOWNLOADS] = { &icon_download, SHELL_ICON - 4 },
            [SYMBOL_ADD] = { &icon_plus, SHELL_ICON - 4 },
            [SYMBOL_SETTINGS] = { &icon_sliders, SHELL_ICON - 4 },
        };

        for (i = 0; i < SYMBOL_COUNT; i++)
            l->symbols[i] = ui_svg_texture( &l->ui, symbols[i].icon->d, symbols[i].icon->x, symbols[i].icon->y,
                                            symbols[i].icon->width, symbols[i].icon->height, symbols[i].size );
    }
    l->logo = load_logo( l );
    start_icons( l );
    started = SDL_GetTicks();
    load_library( l );
    rebuild_visible( l, -1 );
    rebuild_history( l, -1 );
    for (i = 0, added = 0; i < l->program_count; i++) added += l->programs[i].added;
    launcher_log( "[LAUNCHER] %d catalog games (%d registered, %d shown) loaded in %u ms; icons %s",
                  l->program_count, added, l->visible_count, SDL_GetTicks() - started,
                  l->thread ? "load on a worker thread" : "off: no worker thread" );
    for (i = 0; i < l->visible_count; i++)
    {
        const struct program *p = &l->programs[l->visible[i]];

        if (!strcasecmp( p->path, target ) || !strcasecmp( p->dos, target )) l->selection = i;
    }
    for (i = 0; i < l->history_count; i++)
    {
        const struct program *p = &l->programs[l->history[i]];

        if (!strcasecmp( p->path, target ) || !strcasecmp( p->dos, target )) l->history_selection = i;
    }
    preload_art( l );
    l->dlls = launcher_dlls_create( &l->ui, options->runtime_dir );
    l->graphics = launcher_graphics_create( &l->ui, options->runtime_dir );
    l->ui.background_tick = background_tick;
    l->ui.background_data = l;
    {
        const struct steam_launcher_adapter adapter = { l, steam_register_executable, steam_choose_executable, steam_unregister_directory, steam_shell_header, steam_shell_hit, steam_launch_prerequisite };
        steam_ui_set_launcher( &adapter );
        steam_ui_prepare( options->runtime_dir );
    }
    offer_quick_setup( l );
    if (options->launch_error) ui_message( &l->ui, "Game unavailable", options->launch_error );
    ret = l->ui.running ? shell_run( l, target, target_size ) : 0;
    l->ui.background_tick = NULL;
    launcher_dlls_destroy( l->dlls );
    l->dlls = NULL;
    launcher_graphics_destroy( l->graphics );
    l->graphics = NULL;
    for (i = 0, added = 0, missing = 0; i < l->program_count; i++)
    {
        added += l->programs[i].icon_state == ICON_READY;
        missing += l->programs[i].icon_state == ICON_MISSING;
    }
    launcher_log( "[LAUNCHER] %s; %d icons shown, %d programs without one", ret ? "starting a program" : "closed",
                  added, missing );
    steam_ui_shutdown();
    stop_icons( l );
    for (i = 0; i < SYMBOL_COUNT; i++)
        if (l->symbols[i]) SDL_DestroyTexture( l->symbols[i] );
    if (l->logo) SDL_DestroyTexture( l->logo );
    l->usb_event = 0;
    ui_quit( &l->ui );
    launcher_platform_font_release();
    return ret;
}
