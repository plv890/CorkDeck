/*
 * The settings model against the reader the runtime has always used.
 *
 * For thousands of settings files made of known words, older spellings, words
 * that mean nothing and missing keys, what app_settings_resolve says a program
 * runs with must be exactly what launcher_settings_read says: the model is a
 * second description of the same files, and the runtime's behaviour must not
 * depend on which of the two is asked. Then the layers the old reader never
 * had: general settings, limits, test files and the diagnostics switch.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/app/app_settings.h"
#include "../source/config_json.h"
#include "../source/launcher_settings.h"

static unsigned int seed = 20261009;
static unsigned int next( unsigned int below )
{
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) % below;
}

static const char *const keys[] =
{
    "d3d", "d3d9", "dxvk-source", "dxvk-version", "vkd3d-version", "dxvk-hud", "frame-limit", "vsync",
    "upscaling", "upscale", "upscaling-sharpness", "sharpness", "lsfg", "lsfg-performance", "lsfg-flow",
    "cpu", "four-cores", "sync", "own-controls", "verbose", "profile", "windows", "title", "hidden",
};
static const char *const words[] =
{
    "", "0", "1", "2", "on", "off", "ON", "Off", "true", "wine", "Wine", "dxvk", "DXVK", "dxvk+vkd3d", "vkd3d",
    "sarek", "Sarek", "gplasync", "official", "2.0", "2.1", "2.7.1", "1.10.3", "3.1.1", "v2.3", "2.4-", "bad version",
    "fps", "api,fps,frametimes", "version,api,devinfo,fps,memory,frametimes,compiler", "full",
    "30", "40", "45", "60", "75", "90", "120", "144", "fsr", "FSR", "integer", "0%", "20%", "40%", "60%", "80%",
    "100%", "50%", "0.125", "0.25", "0.5", "0.75", "box64", "Box64", "fex", "horizon", "Horizon", "standard",
    "framebuffer", "compositor", "Compositor", "nonsense", " 1", "1 ",
};

static void random_file( struct launcher_kv *kv )
{
    unsigned int lines = next( 14 ), i;

    kv->size = 0;
    kv->text[0] = 0;
    for (i = 0; i < lines; i++)
    {
        const char *key = keys[next( sizeof(keys) / sizeof(keys[0]) )];
        const char *word = words[next( sizeof(words) / sizeof(words[0]) )];

        kv->size += (size_t)snprintf( kv->text + kv->size, sizeof(kv->text) - kv->size, "%s%s=%s%s\n",
                                      next( 8 ) ? "" : "  ", key, word, next( 10 ) ? "" : "\r" );
    }
}

#define SAME(old, id) \
    do { if ((int)(old) != app_settings_get( &now, (id) )) { \
        fprintf( stderr, "%s: old %d, model %d, for:\n%s", #id, (int)(old), app_settings_get( &now, (id) ), kv.text ); \
        exit( 1 ); } } while (0)
#define SAME_LAYER(old, id) \
    do { if ((int)(old) != now.values[(id)].layer[APP_LAYER_PROFILE]) { \
        fprintf( stderr, "%s: old %d, profile layer %d, for:\n%s", #id, (int)(old), \
                 now.values[(id)].layer[APP_LAYER_PROFILE], kv.text ); exit( 1 ); } } while (0)

static void compare( const struct launcher_kv *file )
{
    struct launcher_kv kv = *file;
    struct launcher_settings old;
    struct app_settings now;

    launcher_settings_read( &kv, &old );
    app_settings_resolve( &now, NULL, &kv, NULL, NULL, NULL );
    SAME( old.d3d, APP_SET_RENDERER );
    SAME( old.dxvk_source, APP_SET_DXVK_SOURCE );
    SAME( old.dxvk_hud, APP_SET_DXVK_HUD );
    SAME( old.frame_limit, APP_SET_FRAME_LIMIT );
    SAME( old.vsync, APP_SET_VSYNC );
    SAME( old.upscaling, APP_SET_UPSCALING );
    SAME( old.upscaling_sharpness, APP_SET_SHARPNESS );
    SAME( old.lsfg_enabled, APP_SET_LSFG );
    SAME( old.lsfg_performance, APP_SET_LSFG_PERFORMANCE );
    SAME( old.lsfg_flow, APP_SET_LSFG_FLOW );
    SAME( old.fex, APP_SET_TRANSLATOR );
    SAME( old.four_cores, APP_SET_FOUR_CORES );
    SAME( old.fast_sync, APP_SET_SYNC );
    SAME_LAYER( old.own_controls, APP_SET_OWN_CONTROLS );
    SAME_LAYER( old.verbose, APP_SET_VERBOSE );
    SAME_LAYER( old.profile, APP_SET_PROFILER );
    SAME_LAYER( old.framebuffer, APP_SET_WINDOWS );
    if (strcmp( old.dxvk_version, now.values[APP_SET_DXVK_VERSION].text ) ||
        strcmp( old.vkd3d_version, now.values[APP_SET_VKD3D_VERSION].text ))
    {
        fprintf( stderr, "versions: old '%s' '%s', model '%s' '%s', for:\n%s", old.dxvk_version, old.vkd3d_version,
                 now.values[APP_SET_DXVK_VERSION].text, now.values[APP_SET_VKD3D_VERSION].text, kv.text );
        exit( 1 );
    }
}

/* What the model writes, the old reader reads back as the same choices. */
static void written_reads_back( void )
{
    static const enum app_setting ids[] =
    {
        APP_SET_RENDERER, APP_SET_DXVK_SOURCE, APP_SET_DXVK_HUD, APP_SET_FRAME_LIMIT, APP_SET_VSYNC,
        APP_SET_UPSCALING, APP_SET_SHARPNESS, APP_SET_LSFG, APP_SET_LSFG_PERFORMANCE, APP_SET_LSFG_FLOW,
        APP_SET_TRANSLATOR, APP_SET_FOUR_CORES, APP_SET_SYNC, APP_SET_OWN_CONTROLS, APP_SET_VERBOSE,
        APP_SET_PROFILER, APP_SET_WINDOWS,
    };
    unsigned int round, i;

    for (round = 0; round < 4000; round++)
    {
        struct launcher_kv kv = { .size = 0 }, rewritten;
        struct launcher_settings old;
        int chosen[sizeof(ids) / sizeof(ids[0])];

        kv.text[0] = 0;
        if (next( 3 )) assert( launcher_kv_set( &kv, "# kept", "a comment-like line stays" ) );
        for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
        {
            const struct app_setting_def *def = app_setting_def( ids[i] );

            chosen[i] = (int)next( def->choice_count + 1u ) - 1;
            assert( app_settings_set_profile( &kv, ids[i], chosen[i], NULL ) );
        }
        assert( app_settings_set_profile( &kv, APP_SET_DXVK_VERSION, 0, next( 2 ) ? "2.7.1" : "" ) );
        launcher_settings_read( &kv, &old );
        compare( &kv );
        /* And the old writer over the same file changes nothing the model reads. */
        rewritten = kv;
        assert( launcher_settings_write( &rewritten, &old ) );
        compare( &rewritten );
        if (next( 3 )) assert( strstr( kv.text, "# kept=" ) || !strstr( kv.text, "#" ) );
    }
    {
        struct launcher_kv kv = { .size = 0 };

        kv.text[0] = 0;
        /* The default of a setting only the program can give leaves no line. */
        assert( app_settings_set_profile( &kv, APP_SET_VSYNC, 1, NULL ) && launcher_kv_empty( &kv ) );
        assert( app_settings_set_profile( &kv, APP_SET_VSYNC, 0, NULL ) && !strcmp( kv.text, "vsync=0\n" ) );
        assert( app_settings_set_profile( &kv, APP_SET_VSYNC, -1, NULL ) && launcher_kv_empty( &kv ) );
        /* One a general setting can give too is written either way, to say "this program differs". */
        assert( app_settings_set_profile( &kv, APP_SET_VERBOSE, 0, NULL ) && !strcmp( kv.text, "verbose=0\n" ) );
        assert( !app_settings_set_profile( &kv, APP_SET_LANGUAGE, 1, NULL ) );
        assert( !app_settings_set_profile( &kv, APP_SET_DXVK_VERSION, 0, "not a version" ) );
    }
}

static const char *experiment_files;
static int experiment( void *context, const char *file, char *line, size_t size )
{
    const char *at = experiment_files, *end;
    size_t length = strlen( file );

    (void)context;
    while (at && *at)
    {
        end = strchr( at, ';' );
        if (!strncmp( at, file, length ) && at[length] == '=')
        {
            snprintf( line, size, "%.*s", (int)((end ? end : at + strlen( at )) - at - length - 1), at + length + 1 );
            return 1;
        }
        at = end ? end + 1 : NULL;
    }
    return 0;
}

static void layers( void )
{
    struct app_settings s;
    struct wine_nx_config global;
    struct launcher_kv kv;
    struct app_settings_limits limits = { 0x8664, 1, 1, 1, 1, 0 };

    /* Nothing said anywhere: the defaults, each named as one. */
    app_settings_resolve( &s, NULL, NULL, NULL, NULL, NULL );
    assert( app_settings_get( &s, APP_SET_RENDERER ) == 1 && s.values[APP_SET_RENDERER].source == APP_LAYER_DEFAULT );
    /* Diagnostics are a developer's: without the mode nothing of them runs, and each says why. */
    assert( app_settings_get( &s, APP_SET_DEVELOPER ) == 0 && app_settings_get( &s, APP_SET_DIAGNOSTICS_MODE ) == 0 );
    assert( s.values[APP_SET_DIAGNOSTICS_MODE].reason == APP_REASON_DEVELOPER_OFF );
    assert( s.values[APP_SET_DIAGNOSTICS_MODE].layer[APP_LAYER_DEFAULT] == 2 );
    assert( app_settings_get( &s, APP_SET_PERF_COUNTERS ) == 0 && s.values[APP_SET_PERF_COUNTERS].reason == APP_REASON_DEVELOPER_OFF );
    assert( app_settings_get( &s, APP_SET_WAIT_DIAGNOSTICS ) == 0 && app_settings_get( &s, APP_SET_REMOTE_SNAPSHOT ) == 0 );
    /* With it, a developer has every tool to hand, and the values tuned on the console before the fork. */
    assert( wine_nx_config_parse( &global, "{ \"developer-mode\": true }" ) );
    app_settings_resolve( &s, &global, NULL, NULL, NULL, NULL );
    assert( app_settings_get( &s, APP_SET_DIAGNOSTICS_MODE ) == 2 );
    assert( app_settings_get( &s, APP_SET_STALL_SNAPSHOTS ) == 0 && app_settings_get( &s, APP_SET_PERF_COUNTERS ) == 1 );
    /* What is not diagnostics does not depend on the mode. */
    app_settings_resolve( &s, NULL, NULL, NULL, NULL, NULL );
    assert( app_settings_get( &s, APP_SET_WORKER_POLICY ) == 4 && app_settings_get( &s, APP_SET_SHADER_RAM_MB ) == 3 );
    assert( app_settings_get( &s, APP_SET_PAD_AS_KEYBOARD ) == 0 && app_settings_get( &s, APP_SET_WINDOWS ) == 0 );

    /* The general settings as the runtime writes them; the program's own word over them. */
    assert( wine_nx_config_parse( &global, "{ \"developer-mode\": true, \"verbose-log\": true, \"windows-through-opengl\": false,"
                                  " \"diagnostics\": \"extended\", \"cpu-worker-policy\": \"4\","
                                  " \"controller-as-keyboard-and-mouse\": true, \"language\": \"ru\" }" ) );
    app_settings_resolve( &s, &global, NULL, &limits, NULL, NULL );
    assert( app_settings_get( &s, APP_SET_VERBOSE ) == 1 && s.values[APP_SET_VERBOSE].source == APP_LAYER_GLOBAL );
    assert( app_settings_get( &s, APP_SET_WINDOWS ) == 1 && s.values[APP_SET_WINDOWS].source == APP_LAYER_GLOBAL );
    assert( app_settings_get( &s, APP_SET_WORKER_POLICY ) == 4 && app_settings_get( &s, APP_SET_LANGUAGE ) == 2 );
    assert( app_settings_get( &s, APP_SET_PAD_AS_KEYBOARD ) == 1 );
    kv.size = (size_t)snprintf( kv.text, sizeof(kv.text), "verbose=0\nwindows=compositor\nworker-policy=2\npad-as-keyboard=0\n" );
    app_settings_resolve( &s, &global, &kv, &limits, NULL, NULL );
    assert( app_settings_get( &s, APP_SET_VERBOSE ) == 0 && s.values[APP_SET_VERBOSE].source == APP_LAYER_PROFILE );
    assert( s.values[APP_SET_VERBOSE].layer[APP_LAYER_GLOBAL] == 1 );
    assert( app_settings_get( &s, APP_SET_WINDOWS ) == 0 && app_settings_get( &s, APP_SET_WORKER_POLICY ) == 2 );
    assert( app_settings_get( &s, APP_SET_PAD_AS_KEYBOARD ) == 0 );

    /* Observation: what changes how a program runs stays off, and says why. */
    assert( wine_nx_config_parse( &global, "{ \"developer-mode\": true, \"diagnostics\": \"observe\", \"verbose-log\": true, \"profiler\": true }" ) );
    app_settings_resolve( &s, &global, NULL, &limits, NULL, NULL );
    assert( app_settings_get( &s, APP_SET_VERBOSE ) == 0 && s.values[APP_SET_VERBOSE].source == APP_LAYER_LIMIT );
    assert( s.values[APP_SET_VERBOSE].reason == APP_REASON_NEEDS_EXTENDED );
    assert( s.values[APP_SET_VERBOSE].layer[APP_LAYER_GLOBAL] == 1 );
    assert( app_settings_get( &s, APP_SET_PROFILER ) == 0 );
    /* A test file from a computer still switches its tool on... */
    experiment_files = "wait-diagnostics.txt=1;performance.txt=true;cpu-worker-policy.txt=2;shader-ram-cache-mb.txt=24;stall-snapshots.txt=0";
    app_settings_resolve( &s, &global, NULL, &limits, experiment, NULL );
    assert( app_settings_get( &s, APP_SET_WAIT_DIAGNOSTICS ) == 1 );
    assert( s.values[APP_SET_WAIT_DIAGNOSTICS].source == APP_LAYER_EXPERIMENT );
    assert( app_settings_get( &s, APP_SET_PERF_COUNTERS ) == 1 && app_settings_get( &s, APP_SET_WORKER_POLICY ) == 2 );
    assert( app_settings_get( &s, APP_SET_STALL_SNAPSHOTS ) == 0 );
    /* A size the list does not offer is kept as it was written. */
    assert( app_settings_get( &s, APP_SET_SHADER_RAM_MB ) == -1 && !strcmp( s.values[APP_SET_SHADER_RAM_MB].text, "24" ) );
    assert( !strcmp( app_setting_value_label( &s, APP_SET_SHADER_RAM_MB, APP_LANG_RU ), "24" ) );
    /* ...but not when diagnostics are switched off as a whole. */
    assert( wine_nx_config_parse( &global, "{ \"developer-mode\": true, \"diagnostics\": \"off\", \"verbose-log\": true, \"session-logs\": true }" ) );
    app_settings_resolve( &s, &global, NULL, &limits, experiment, NULL );
    assert( !app_settings_get( &s, APP_SET_WAIT_DIAGNOSTICS ) && !app_settings_get( &s, APP_SET_PERF_COUNTERS ) );
    assert( !app_settings_get( &s, APP_SET_VERBOSE ) && !app_settings_get( &s, APP_SET_SESSION_LOGS ) );
    assert( s.values[APP_SET_PERF_COUNTERS].reason == APP_REASON_DIAGNOSTICS_OFF );
    /* What is not diagnostics is the test file's still. */
    assert( app_settings_get( &s, APP_SET_WORKER_POLICY ) == 2 );
    experiment_files = NULL;

    /* A combination that cannot stand, and what the card lacks. */
    kv.size = (size_t)snprintf( kv.text, sizeof(kv.text), "d3d=dxvk+vkd3d\ndxvk-source=sarek\ndxvk-version=1.10.3\nlsfg=1\n" );
    limits.lsfg_installed = 0;
    app_settings_resolve( &s, NULL, &kv, &limits, NULL, NULL );
    assert( app_settings_get( &s, APP_SET_DXVK_SOURCE ) == 0 );
    assert( s.values[APP_SET_DXVK_SOURCE].reason == APP_REASON_VKD3D_NEEDS_OFFICIAL );
    assert( s.values[APP_SET_DXVK_SOURCE].layer[APP_LAYER_PROFILE] == 1 );
    assert( !s.values[APP_SET_DXVK_VERSION].text[0] && !strcmp( s.values[APP_SET_DXVK_VERSION].chosen_text, "1.10.3" ) );
    assert( !app_settings_get( &s, APP_SET_LSFG ) && s.values[APP_SET_LSFG].reason == APP_REASON_LSFG_MISSING );
    limits.vulkan = 0;
    app_settings_resolve( &s, NULL, &kv, &limits, NULL, NULL );
    assert( app_settings_get( &s, APP_SET_RENDERER ) == 0 && s.values[APP_SET_RENDERER].reason == APP_REASON_NO_VULKAN );

    /* Its own keys when it has a file of them, unless it says otherwise. */
    limits = (struct app_settings_limits){ 0x14c, 1, 1, 1, 1, 1 };
    app_settings_resolve( &s, NULL, NULL, &limits, NULL, NULL );
    assert( app_settings_get( &s, APP_SET_OWN_CONTROLS ) == 1 );
    kv.size = (size_t)snprintf( kv.text, sizeof(kv.text), "own-controls=0\n" );
    app_settings_resolve( &s, NULL, &kv, &limits, NULL, NULL );
    assert( app_settings_get( &s, APP_SET_OWN_CONTROLS ) == 0 );

    /* The general settings file takes what the model writes, and gives it back. */
    global.count = 0;
    assert( app_settings_set_global( &global, APP_SET_WINDOWS, 1 ) && !wine_nx_config_bool( &global, "windows-through-opengl", 1 ) );
    assert( app_settings_set_global( &global, APP_SET_DEVELOPER, 1 ) );
    assert( app_settings_set_global( &global, APP_SET_DIAGNOSTICS_MODE, 2 ) && app_settings_set_global( &global, APP_SET_VERBOSE, 1 ) );
    assert( !app_settings_set_global( &global, APP_SET_RENDERER, 0 ) );
    app_settings_resolve( &s, &global, NULL, NULL, NULL, NULL );
    assert( app_settings_get( &s, APP_SET_WINDOWS ) == 1 && app_settings_get( &s, APP_SET_VERBOSE ) == 1 );
}

/* Every setting can be shown: a name, a purpose and labels in both languages. */
static void table( void )
{
    int id, language, choice;

    for (id = 0; id < APP_SET_COUNT; id++)
    {
        const struct app_setting_def *def = app_setting_def( id );

        assert( def && def->id == (enum app_setting)id );
        assert( def->profile_key || def->global_key );
        assert( def->type == APP_TYPE_TEXT || (def->choice_count >= 2 && def->default_choice < def->choice_count) );
        for (language = 0; language < APP_LANG_COUNT; language++)
        {
            assert( def->name[language] && def->name[language][0] );
            assert( def->purpose[language] && def->purpose[language][0] );
            assert( !def->impact[0] == !def->impact[1] );
            for (choice = 0; choice < def->choice_count; choice++)
                assert( def->choices[choice].label[language] && def->choices[choice].label[language][0] );
        }
        /* A choice that can be written to the general settings has a word to write. */
        if (def->global_key && def->type == APP_TYPE_CHOICE && !def->global_inverted)
            for (choice = 0; choice < def->choice_count; choice++) assert( def->choices[choice].value );
    }
}

int main( void )
{
    struct launcher_kv kv;
    unsigned int i;

    table();
    for (i = 0; i < 200000; i++)
    {
        random_file( &kv );
        compare( &kv );
    }
    written_reads_back();
    layers();
    puts( "app settings: 200000 random files read as before; writing, layers, limits and the table passed" );
    return 0;
}
