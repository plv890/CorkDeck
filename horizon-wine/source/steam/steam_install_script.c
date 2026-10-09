/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#include "steam_install_script.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SCRIPT_DEPTH 8
#define SCRIPT_WORD  520
#define SCRIPT_PAIRS 16

enum { TOKEN_END, TOKEN_OPEN, TOKEN_CLOSE, TOKEN_TEXT, TOKEN_BAD };

struct reader
{
    const char *p, *end;
};

/* One word of Valve's text format: a brace, or a string in quotes with
 * backslash escapes, or a bare word. A string too long for 'out' comes back
 * empty with *cut set, so a value that does not fit is never used shortened. */
static int next_token( struct reader *r, char *out, size_t size, int *cut )
{
    size_t n = 0;

    *cut = 0;
    out[0] = 0;
    for (;;)
    {
        while (r->p < r->end && (*r->p == ' ' || *r->p == '\t' || *r->p == '\r' || *r->p == '\n')) r->p++;
        if (r->p + 1 < r->end && r->p[0] == '/' && r->p[1] == '/')
        {
            while (r->p < r->end && *r->p != '\n') r->p++;
            continue;
        }
        break;
    }
    if (r->p >= r->end || !*r->p) return TOKEN_END;
    if (*r->p == '{') { r->p++; return TOKEN_OPEN; }
    if (*r->p == '}') { r->p++; return TOKEN_CLOSE; }
    if (*r->p == '"')
    {
        for (r->p++; r->p < r->end && *r->p != '"'; r->p++)
        {
            char c = *r->p;

            if (!c) return TOKEN_BAD;
            if (c == '\\' && r->p + 1 < r->end)
            {
                c = *++r->p;
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
            }
            if (n + 1 < size) out[n++] = c;
            else *cut = 1;
        }
        if (r->p >= r->end) return TOKEN_BAD;
        r->p++;
    }
    else
    {
        for (; r->p < r->end && *r->p && !strchr( " \t\r\n{}\"", *r->p ); r->p++)
        {
            if (n + 1 < size) out[n++] = *r->p;
            else *cut = 1;
        }
    }
    out[*cut ? 0 : n] = 0;
    return TOKEN_TEXT;
}

static const char *place( const struct steam_script_places *places, const char *name, size_t length )
{
    static const struct { const char *name; size_t offset; } known[] =
    {
        { "INSTALLDIR", offsetof( struct steam_script_places, install_dir ) },
        { "USER_MYDOCS", offsetof( struct steam_script_places, documents ) },
        { "COMMON_MYDOCS", offsetof( struct steam_script_places, public_documents ) },
        { "APPDATA", offsetof( struct steam_script_places, app_data ) },
        { "LOCALAPPDATA", offsetof( struct steam_script_places, local_app_data ) },
        { "WINDIR", offsetof( struct steam_script_places, windows ) },
        { "ROOTDRIVE", offsetof( struct steam_script_places, root_drive ) },
    };
    size_t i;

    for (i = 0; i < sizeof(known) / sizeof(known[0]); i++)
        if (strlen( known[i].name ) == length && !strncasecmp( known[i].name, name, length ))
            return *(const char *const *)((const char *)places + known[i].offset);
    return NULL;
}

/* %NAME% replaced by the place it stands for. 0 when a place is not known
 * or the result does not fit. */
static int expand( const char *in, const struct steam_script_places *places, char *out, size_t size )
{
    size_t n = 0;

    while (*in)
    {
        const char *close = *in == '%' ? strchr( in + 1, '%' ) : NULL;

        if (close && close > in + 1)
        {
            const char *value = place( places, in + 1, close - in - 1 );
            size_t length;

            if (!value) return 0;
            length = strlen( value );
            if (n + length >= size) return 0;
            memcpy( out + n, value, length );
            n += length;
            in = close + 1;
            continue;
        }
        if ((unsigned char)*in < 0x20 || n + 1 >= size) return 0;
        out[n++] = *in++;
    }
    out[n] = 0;
    return 1;
}

static int component_is( const char *path, const char *name )
{
    size_t length = strlen( name );

    return !strncasecmp( path, name, length ) && (!path[length] || path[length] == '\\');
}

/* The key as the NT API names it. Only what an installer of a game has any
 * business writing is taken: below Software, and not the parts of it that
 * Windows and Wine keep their own settings in. A 32-bit program's view of
 * Software (Wow6432Node) is Software itself in this prefix. */
static int registry_key( const char *in, const struct steam_script_places *places, char *out, size_t size )
{
    static const char *const refused[] = { "Microsoft", "Wine", "Classes", "Policies" };
    const char *rest;
    size_t i;
    int n;

    while (*in == '\\') in++;
    if (component_is( in, "HKEY_LOCAL_MACHINE" ) || component_is( in, "HKLM" ))
        n = snprintf( out, size, "\\Registry\\Machine\\Software" );
    else if ((component_is( in, "HKEY_CURRENT_USER" ) || component_is( in, "HKCU" )) && places->user_sid)
        n = snprintf( out, size, "\\Registry\\User\\%s\\Software", places->user_sid );
    else return 0;
    if (n <= 0 || (size_t)n >= size) return 0;

    rest = strchr( in, '\\' );
    while (rest && *rest == '\\') rest++;
    if (!rest || !component_is( rest, "Software" )) return 0;
    rest += strlen( "Software" );
    while (*rest == '\\') rest++;
    if (component_is( rest, "Wow6432Node" ))
    {
        rest += strlen( "Wow6432Node" );
        while (*rest == '\\') rest++;
    }
    if (!*rest) return 0;
    for (i = 0; i < sizeof(refused) / sizeof(refused[0]); i++)
        if (component_is( rest, refused[i] )) return 0;

    while (*rest)
    {
        size_t length = strcspn( rest, "\\" );

        if ((size_t)n + length + 2 > size) return 0;
        for (i = 0; i < length; i++)
            if ((unsigned char)rest[i] < 0x20) return 0;
        out[n++] = '\\';
        memcpy( out + n, rest, length );
        n += length;
        rest += length;
        while (*rest == '\\') rest++;
    }
    out[n] = 0;
    return 1;
}

static struct steam_script_op *add_op( struct steam_script_plan *plan, int kind )
{
    struct steam_script_op *op;

    if (plan->count >= STEAM_SCRIPT_MAX_OPS)
    {
        plan->skipped++;
        return NULL;
    }
    op = &plan->ops[plan->count];
    memset( op, 0, sizeof(*op) );
    op->kind = kind;
    return op;
}

static void add_key( struct steam_script_plan *plan, const struct steam_script_places *places, const char *key )
{
    struct steam_script_op *op = add_op( plan, STEAM_SCRIPT_KEY );

    if (!op) return;
    if (registry_key( key, places, op->key, sizeof(op->key) )) plan->count++;
    else plan->skipped++;
}

static void add_value( struct steam_script_plan *plan, const struct steam_script_places *places, const char *key,
                       int dword, const char *name, const char *value, int cut )
{
    struct steam_script_op *op = add_op( plan, dword ? STEAM_SCRIPT_DWORD : STEAM_SCRIPT_STRING );
    int ok;

    if (!op) return;
    ok = !cut && strlen( name ) < sizeof(op->name) && registry_key( key, places, op->key, sizeof(op->key) );
    if (ok) strcpy( op->name, name );
    if (ok && dword)
    {
        char *end;
        int hex = value[0] == '0' && (value[1] == 'x' || value[1] == 'X');
        unsigned long long number = strtoull( hex ? value + 2 : value, &end, hex ? 16 : 10 );

        ok = end != value + (hex ? 2 : 0) && !*end && number <= 0xffffffffu;
        op->number = (uint32_t)number;
    }
    else if (ok) ok = expand( value, places, op->text, sizeof(op->text) );
    if (ok) plan->count++;
    else plan->skipped++;
}

struct folder_pair
{
    char label[64], number[16];
    char source[SCRIPT_WORD], destination[SCRIPT_WORD];
    int bad;
};

/* "SrcFolder 1" and "DstFolder 1" of one entry belong together. */
static void note_folder( struct folder_pair *pairs, const char *label, const char *name, const char *value, int cut )
{
    int source = !strncasecmp( name, "SrcFolder", 9 );
    const char *number;
    size_t i;

    if (!source && strncasecmp( name, "DstFolder", 9 )) return;
    number = name + 9;
    while (*number == ' ') number++;
    for (i = 0; i < SCRIPT_PAIRS; i++)
    {
        struct folder_pair *pair = &pairs[i];

        if (pair->label[0] && (strcasecmp( pair->label, label ) || strcmp( pair->number, number ))) continue;
        if (!pair->label[0])
        {
            if (!label[0] || strlen( label ) >= sizeof(pair->label) || strlen( number ) >= sizeof(pair->number)) return;
            strcpy( pair->label, label );
            strcpy( pair->number, number );
        }
        if (cut) pair->bad = 1;
        else strcpy( source ? pair->source : pair->destination, value );
        return;
    }
}

int steam_install_script_plan( const char *text, size_t size, const struct steam_script_places *places,
                               struct steam_script_plan *plan )
{
    struct folder_pair *pairs;
    const char *language = places->language && places->language[0] ? places->language : "english";
    int pass, ok = 0;
    size_t i;

    memset( plan, 0, sizeof(*plan) );
    if (!text || !size || size > STEAM_SCRIPT_MAX_BYTES) return 0;
    if (!(pairs = calloc( SCRIPT_PAIRS, sizeof(*pairs) ))) return 0;

    /* A value can be given once for every language, each in a block of its
     * own. The first pass finds whether the game's language has such a block;
     * when it has none, the English one is used, as the game's files are. */
    for (pass = 0; pass < 2; pass++)
    {
        struct reader r = { text, text + size };
        char path[SCRIPT_DEPTH][SCRIPT_WORD], key[SCRIPT_WORD], value[SCRIPT_WORD];
        unsigned int depth = 0;
        int cut, value_cut, token, language_seen = 0;

        ok = 0;
        for (;;)
        {
            int registry, folders, typed;

            token = next_token( &r, key, sizeof(key), &cut );
            if (token == TOKEN_END) { ok = !depth; break; }
            if (token == TOKEN_CLOSE)
            {
                if (!depth) break;
                depth--;
                continue;
            }
            if (token != TOKEN_TEXT) break;
            token = next_token( &r, value, sizeof(value), &value_cut );
            if (token != TOKEN_OPEN && token != TOKEN_TEXT) break;

            registry = depth >= 2 && depth <= SCRIPT_DEPTH && !strcasecmp( path[0], "InstallScript" ) &&
                       !strcasecmp( path[1], "Registry" );
            folders = depth == 3 && !strcasecmp( path[0], "InstallScript" ) && !strcasecmp( path[1], "Copy Folders" );
            typed = registry && depth >= 4 && (!strcasecmp( path[3], "string" ) || !strcasecmp( path[3], "dword" ));

            if (token == TOKEN_OPEN)
            {
                if (registry && depth == 2 && pass) add_key( plan, places, cut ? "" : key );
                if (typed && depth == 4 && !pass && !strcasecmp( key, language )) language_seen = 1;
                /* Deeper than anything read here: counted, to find the way back out. */
                if (depth < SCRIPT_DEPTH) strcpy( path[depth], key );
                depth++;
                continue;
            }
            if (!pass) continue;
            if (typed && !path[2][0]) plan->skipped++;
            else if (typed && (depth == 4 || (depth == 5 && !strcasecmp( path[4], language ))))
                add_value( plan, places, path[2], !strcasecmp( path[3], "dword" ), key, value, cut || value_cut );
            else if (folders) note_folder( pairs, path[2], key, value, cut || value_cut );
        }
        if (!ok) break;
        if (!pass && !language_seen) language = "english";
    }

    for (i = 0; ok && i < SCRIPT_PAIRS && pairs[i].label[0]; i++)
    {
        struct steam_script_op *op;

        if (pairs[i].bad || !pairs[i].source[0] || !pairs[i].destination[0]) { plan->skipped++; continue; }
        if (!(op = add_op( plan, STEAM_SCRIPT_COPY ))) continue;
        if (expand( pairs[i].source, places, op->key, sizeof(op->key) ) &&
            expand( pairs[i].destination, places, op->text, sizeof(op->text) )) plan->count++;
        else plan->skipped++;
    }
    free( pairs );
    if (!ok) memset( plan, 0, sizeof(*plan) );
    return ok;
}

uint64_t steam_install_script_mark( const char *text, size_t size, const struct steam_script_places *places )
{
    /* The last word changes when what a script is turned into changes, so
     * that scripts applied by an earlier build are applied again. */
    const char *parts[] = { places->install_dir, places->language, "plan 1" };
    uint64_t hash = 0xcbf29ce484222325ull;
    size_t i, part;

    for (i = 0; i < size; i++) hash = (hash ^ (unsigned char)text[i]) * 0x100000001b3ull;
    for (part = 0; part < sizeof(parts) / sizeof(parts[0]); part++)
    {
        const char *p = parts[part] ? parts[part] : "";

        hash = (hash ^ 0xff) * 0x100000001b3ull;
        for (; *p; p++) hash = (hash ^ (unsigned char)*p) * 0x100000001b3ull;
    }
    return hash;
}
