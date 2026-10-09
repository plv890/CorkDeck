/*
 * The settings model: every setting the application has, in one table, and the
 * one place where the value a program actually runs with is worked out.
 *
 * A setting may be given in several layers. From the weakest to the strongest:
 *
 *   default     what the build does when nobody said anything
 *   global      the user's own choice for every program (config/settings.json)
 *   profile     the program's own choice (NAME.wine-nx.txt beside it)
 *   limit       what the device or a combination of choices rules out
 *   experiment  a loose file in the runtime folder, for a test from a computer
 *
 * app_settings_resolve gives, for each setting, what each layer said, the value
 * that applies, the layer it came from and, when a stronger layer overrode a
 * choice, why. The table also carries what a person needs to decide: what the
 * setting is for and what it can cost in speed, memory, stability or
 * compatibility, in English and Russian.
 *
 * This module reads no files of its own accord and draws nothing: the caller
 * hands it the layers it has read. The formats on the card are the ones the
 * runtime has always used (config_json.h, launcher_settings.h).
 */
#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <stddef.h>

struct wine_nx_config;
struct launcher_kv;

enum app_language { APP_LANG_EN, APP_LANG_RU, APP_LANG_COUNT };

enum app_setting
{
    /* Graphics */
    APP_SET_RENDERER,
    APP_SET_DXVK_SOURCE,
    APP_SET_DXVK_VERSION,
    APP_SET_VKD3D_VERSION,
    APP_SET_DXVK_HUD,
    APP_SET_FRAME_LIMIT,
    APP_SET_VSYNC,
    APP_SET_UPSCALING,
    APP_SET_SHARPNESS,
    APP_SET_LSFG,
    APP_SET_LSFG_PERFORMANCE,
    APP_SET_LSFG_FLOW,
    /* Translation and the processor */
    APP_SET_TRANSLATOR,
    APP_SET_FOUR_CORES,
    APP_SET_SYNC,
    APP_SET_CORE_BALANCING,
    APP_SET_WORKER_POLICY,
    APP_SET_SHADER_RAM_MB,
    APP_SET_CLOCK_PROFILE,
    /* Input */
    APP_SET_OWN_CONTROLS,
    APP_SET_PAD_AS_KEYBOARD,
    APP_SET_KEYBOARD_ON_FOCUS,
    /* Diagnostics */
    APP_SET_DIAGNOSTICS_MODE,
    APP_SET_SESSION_LOGS,
    APP_SET_PERF_COUNTERS,
    APP_SET_STALL_SNAPSHOTS,
    APP_SET_REMOTE_SNAPSHOT,
    APP_SET_VERBOSE,
    APP_SET_PROFILER,
    APP_SET_WAIT_DIAGNOSTICS,
    /* Experimental */
    APP_SET_WINDOWS,
    APP_SET_GL_PINNED_CACHED,
    APP_SET_GL_CLEAN_BEFORE_SUBMIT,
    APP_SET_DISPLAY_DEVICES,
    /* The shell */
    APP_SET_REOPEN_SHELL,
    APP_SET_DXVK_FOR_NEW,
    APP_SET_LANGUAGE,
    APP_SET_ANIMATIONS,
    APP_SET_SOUNDS,
    APP_SET_COUNT
};

enum app_setting_group
{
    APP_GROUP_GRAPHICS, APP_GROUP_TRANSLATION, APP_GROUP_INPUT, APP_GROUP_DIAGNOSTICS,
    APP_GROUP_EXPERIMENTAL, APP_GROUP_SHELL, APP_GROUP_COUNT
};

enum app_setting_type
{
    APP_TYPE_SWITCH,   /* two choices: 0 off, 1 on */
    APP_TYPE_CHOICE,   /* one of the table's choices */
    APP_TYPE_TEXT      /* a short word: a version */
};

enum app_setting_layer
{
    APP_LAYER_DEFAULT, APP_LAYER_GLOBAL, APP_LAYER_PROFILE, APP_LAYER_LIMIT, APP_LAYER_EXPERIMENT,
    APP_LAYER_COUNT
};

/* Why the value that applies is not the one a weaker layer chose. */
enum app_setting_reason
{
    APP_REASON_NONE,
    APP_REASON_VKD3D_NEEDS_OFFICIAL,   /* DXVK + VKD3D cannot use Sarek or a DXVK before 2.1 */
    APP_REASON_NO_VULKAN,              /* this build has no Vulkan: Wine's renderer */
    APP_REASON_NOT_X86,                /* a native program has no translator to choose */
    APP_REASON_LSFG_MISSING,           /* Lossless.dll is not on the card */
    APP_REASON_FORWARDER_OLD,          /* the forwarder cannot give four cores */
    APP_REASON_FEX_MISSING,            /* the FEX module for this architecture is not installed */
    APP_REASON_DIAGNOSTICS_OFF,        /* diagnostics are switched off as a whole */
    APP_REASON_NEEDS_EXTENDED,         /* affects the program: only in extended diagnostics */
    APP_REASON_EXPERIMENT_FILE,        /* a test file in the runtime folder says so */
    APP_REASON_COUNT
};

#define APP_SETTING_CHOICES 8
#define APP_SETTING_TEXT    32

struct app_setting_choice
{
    const char *value;                    /* as written in a file; NULL: the key is left out */
    const char *alias;                    /* another spelling that reads as this choice */
    const char *label[APP_LANG_COUNT];
};

struct app_setting_def
{
    enum app_setting id;
    enum app_setting_group group;
    enum app_setting_type type;
    const char *profile_key;              /* in NAME.wine-nx.txt; NULL: not a per-program setting */
    const char *profile_alias;            /* an older name of the same key */
    const char *global_key;               /* in config/settings.json; NULL: not a global setting */
    unsigned char global_inverted;        /* the JSON key says the opposite of choice 1 */
    const char *experiment_file;          /* a loose file that overrides; NULL: none */
    short default_choice;
    unsigned char choice_count;
    struct app_setting_choice choices[APP_SETTING_CHOICES];
    const char *name[APP_LANG_COUNT];
    const char *purpose[APP_LANG_COUNT];  /* what it is for */
    const char *impact[APP_LANG_COUNT];   /* what it may cost; NULL when nothing is known to */
};

struct app_setting_value
{
    short layer[APP_LAYER_COUNT];         /* the choice each layer gave, or -1 */
    short effective;                      /* the choice that applies */
    unsigned char source;                 /* enum app_setting_layer */
    unsigned char reason;                 /* enum app_setting_reason */
    char text[APP_SETTING_TEXT];          /* APP_TYPE_TEXT: the word that applies; "" for the default */
    char chosen_text[APP_SETTING_TEXT];   /* APP_TYPE_TEXT: what the profile asked for */
};

/* What the device and the card allow, which no file says. */
struct app_settings_limits
{
    unsigned short machine;               /* the program's IMAGE_FILE_MACHINE_*, or 0 when not known */
    unsigned char vulkan;
    unsigned char lsfg_installed;
    unsigned char four_cores_available;
    unsigned char fex_installed;          /* for this machine */
    unsigned char has_own_keys;           /* NAME.keys.txt is there: what "its own controls" defaults to */
};

/* Reads one experiment file: returns 1 with its first line, 0 when it is not there. */
typedef int (*app_settings_experiment)( void *context, const char *file, char *line, size_t size );

struct app_settings
{
    struct app_setting_value values[APP_SET_COUNT];
};

const struct app_setting_def *app_setting_def( enum app_setting id );
const char *app_setting_group_name( enum app_setting_group group, enum app_language language );
const char *app_setting_layer_name( enum app_setting_layer layer, enum app_language language );
const char *app_setting_reason_text( enum app_setting_reason reason, enum app_language language );

/* Any of global, profile and experiment may be NULL: that layer says nothing. */
void app_settings_resolve( struct app_settings *out, const struct wine_nx_config *global,
                           const struct launcher_kv *profile, const struct app_settings_limits *limits,
                           app_settings_experiment experiment, void *context );

/* The effective choice, and for APP_TYPE_TEXT the effective word. */
static inline int app_settings_get( const struct app_settings *settings, enum app_setting id )
{
    return settings->values[id].effective;
}

/* The label of the choice that applies, e.g. "DXVK + VKD3D"; the word itself for a text setting. */
const char *app_setting_value_label( const struct app_settings *settings, enum app_setting id,
                                     enum app_language language );

/* Writes a choice into a layer's own file model. choice -1 removes the key, so
 * the layer says nothing again. Returns 0 when the setting has no place in that
 * layer or the file model is full. */
int app_settings_set_profile( struct launcher_kv *profile, enum app_setting id, int choice, const char *text );
int app_settings_set_global( struct wine_nx_config *global, enum app_setting id, int choice );

#endif
