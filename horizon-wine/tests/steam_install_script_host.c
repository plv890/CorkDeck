/* What a Steam install script is turned into: registry entries and folders. */
#include "../source/steam/steam_install_script.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct steam_script_places places =
{
    .install_dir = "C:\\Steam\\Fallout 3 - Game of the Year Edition",
    .documents = "C:\\users\\steamuser\\Documents",
    .public_documents = "C:\\users\\Public\\Documents",
    .app_data = "C:\\users\\steamuser\\AppData\\Roaming",
    .local_app_data = "C:\\users\\steamuser\\AppData\\Local",
    .windows = "C:\\windows",
    .root_drive = "C:",
    .user_sid = "S-1-5-21-0-0-0-1000",
    .language = "russian",
};

/* Fallout 3's own script, as Steam ships it. */
static const char fallout[] =
    "\"InstallScript\"\n{\n\t\"Registry\"\n\t{\n"
    "\t\t\"HKEY_LOCAL_MACHINE\\\\SOFTWARE\\\\Bethesda Softworks\\\\Fallout 3\\\\1.00.0000\"\n\t\t{\n\t\t}\n"
    "\t\t\"HKEY_LOCAL_MACHINE\\\\SOFTWARE\\\\Bethesda Softworks\\\\Fallout3\"\n\t\t{\n"
    "\t\t\t\"string\"\n\t\t\t{\n\t\t\t\t\"Installed Path\"\t\t\"%INSTALLDIR%\\\\\"\n\t\t\t}\n\t\t}\n\t}\n"
    "\t\"Run Process\"\n\t{\n\t\t\"DotNetFX\"\n\t\t{\n"
    "\t\t\t\"HasRunKey\"\t\t\"HKEY_lOCAL_MACHINE\\\\Software\\\\Valve\\\\Steam\\\\Apps\\\\22300\"\n"
    "\t\t\t\"Process 1\"\t\t\"%INSTALLDIR%\\\\DotNetFX\\\\dotnetfx3.exe\"\n"
    "\t\t\t\"Requirement_OS\"\n\t\t\t{\n\t\t\t\t\"Is64BitWindows\"\t\t\"0\"\n\t\t\t}\n\t\t}\n\t}\n"
    "\t\"Copy Folders\"\n\t{\n\t\t\"Fallout 3\"\n\t\t{\n"
    "\t\t\t\"SrcFolder 1\"\t\t\"%INSTALLDIR%\\\\Fallout3\"\n"
    "\t\t\t\"DstFolder 1\"\t\t\"%USER_MYDOCS%\\\\My Games\\\\Fallout3\"\n\t\t}\n\t}\n}\n"
    "\"kvsignatures\"\n{\n\t\"InstallScript\"\t\t\"6a8e51\"\n}\n";

/* Values for every language, numbers, a 32-bit view, and what must be left out. */
static const char mixed[] =
    "// a comment\n"
    "\"installscript\" {\n"
    " \"registry\" {\n"
    "  \"HKEY_LOCAL_MACHINE\\\\Software\\\\Wow6432Node\\\\Maker\\\\Game\" {\n"
    "   \"string\" {\n"
    "    \"english\" { \"Language\" \"English\" }\n"
    "    \"german\" { \"Language\" \"German\" }\n"
    "    \"Path\" \"%INSTALLDIR%\"\n"
    "    \"Saves\" \"%LOCALAPPDATA%\\\\Game\"\n"
    "    \"Key\" \"%CDKEY%\"\n"
    "   }\n"
    "   \"dword\" { \"Patch\" \"4\" \"Flags\" \"0x10\" \"Bad\" \"four\" }\n"
    "  }\n"
    "  \"HKEY_CURRENT_USER\\\\Software\\\\Maker\" { \"string\" { \"\" \"default\" } }\n"
    "  \"HKEY_LOCAL_MACHINE\\\\Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run\" { \"string\" { \"Game\" \"x\" } }\n"
    "  \"HKEY_LOCAL_MACHINE\\\\SYSTEM\\\\CurrentControlSet\" { \"dword\" { \"a\" \"1\" } }\n"
    "  \"HKEY_CLASSES_ROOT\\\\.game\" { \"string\" { \"\" \"x\" } }\n"
    " }\n"
    " \"Copy Folders\" {\n"
    "  \"one\" { \"DstFolder 2\" \"%APPDATA%\\\\B\" \"SrcFolder 1\" \"%INSTALLDIR%\\\\a\" \"DstFolder 1\" \"%USER_MYDOCS%\\\\A\" \"SrcFolder 2\" \"%INSTALLDIR%\\\\b\" }\n"
    "  \"half\" { \"SrcFolder 1\" \"%INSTALLDIR%\\\\c\" }\n"
    " }\n"
    "}\n";

static const struct steam_script_op *find( const struct steam_script_plan *plan, int kind, const char *key, const char *name )
{
    unsigned int i;

    for (i = 0; i < plan->count; i++)
        if (plan->ops[i].kind == kind && !strcmp( plan->ops[i].key, key ) && !strcmp( plan->ops[i].name, name ))
            return &plan->ops[i];
    return NULL;
}

int main(void)
{
    struct steam_script_plan *plan = malloc( sizeof(*plan) );
    struct steam_script_places other = places;
    const struct steam_script_op *op;
    char *large;

    assert( plan );
    assert( steam_install_script_plan( fallout, strlen( fallout ), &places, plan ) );
    /* Two keys, the value of one of them, and the folder. */
    assert( plan->count == 4 && !plan->skipped );
    assert( find( plan, STEAM_SCRIPT_KEY, "\\Registry\\Machine\\Software\\Bethesda Softworks\\Fallout3", "" ) );
    assert( find( plan, STEAM_SCRIPT_KEY, "\\Registry\\Machine\\Software\\Bethesda Softworks\\Fallout 3\\1.00.0000", "" ) );
    op = find( plan, STEAM_SCRIPT_STRING, "\\Registry\\Machine\\Software\\Bethesda Softworks\\Fallout3", "Installed Path" );
    assert( op && !strcmp( op->text, "C:\\Steam\\Fallout 3 - Game of the Year Edition\\" ) );
    op = find( plan, STEAM_SCRIPT_COPY, "C:\\Steam\\Fallout 3 - Game of the Year Edition\\Fallout3", "" );
    assert( op && !strcmp( op->text, "C:\\users\\steamuser\\Documents\\My Games\\Fallout3" ) );

    /* No Russian block: the English one. */
    assert( steam_install_script_plan( mixed, strlen( mixed ), &places, plan ) );
    op = find( plan, STEAM_SCRIPT_STRING, "\\Registry\\Machine\\Software\\Maker\\Game", "Language" );
    assert( op && !strcmp( op->text, "English" ) );
    op = find( plan, STEAM_SCRIPT_STRING, "\\Registry\\Machine\\Software\\Maker\\Game", "Path" );
    assert( op && !strcmp( op->text, places.install_dir ) );
    op = find( plan, STEAM_SCRIPT_STRING, "\\Registry\\Machine\\Software\\Maker\\Game", "Saves" );
    assert( op && !strcmp( op->text, "C:\\users\\steamuser\\AppData\\Local\\Game" ) );
    assert( !find( plan, STEAM_SCRIPT_STRING, "\\Registry\\Machine\\Software\\Maker\\Game", "Key" ) );
    op = find( plan, STEAM_SCRIPT_DWORD, "\\Registry\\Machine\\Software\\Maker\\Game", "Patch" );
    assert( op && op->number == 4 );
    op = find( plan, STEAM_SCRIPT_DWORD, "\\Registry\\Machine\\Software\\Maker\\Game", "Flags" );
    assert( op && op->number == 16 );
    assert( !find( plan, STEAM_SCRIPT_DWORD, "\\Registry\\Machine\\Software\\Maker\\Game", "Bad" ) );
    op = find( plan, STEAM_SCRIPT_STRING, "\\Registry\\User\\S-1-5-21-0-0-0-1000\\Software\\Maker", "" );
    assert( op && !strcmp( op->text, "default" ) );
    op = find( plan, STEAM_SCRIPT_COPY, "C:\\Steam\\Fallout 3 - Game of the Year Edition\\a", "" );
    assert( op && !strcmp( op->text, "C:\\users\\steamuser\\Documents\\A" ) );
    op = find( plan, STEAM_SCRIPT_COPY, "C:\\Steam\\Fallout 3 - Game of the Year Edition\\b", "" );
    assert( op && !strcmp( op->text, "C:\\users\\steamuser\\AppData\\Roaming\\B" ) );
    /* The game's key and the user's, four strings, two numbers, two folders. Left out: the CD key, the bad number, three keys
     * outside what a game may write with a value each, the folder with no
     * destination. */
    assert( plan->count == 10 );
    assert( plan->skipped == 9 );
    for (unsigned int i = 0; i < plan->count; i++)
        assert( !strstr( plan->ops[i].key, "Microsoft" ) && !strstr( plan->ops[i].key, "SYSTEM" ) &&
                !strstr( plan->ops[i].key, "Wow6432Node" ) && !strstr( plan->ops[i].key, ".game" ) );

    /* The game's own language when the script has it. */
    other.language = "german";
    assert( steam_install_script_plan( mixed, strlen( mixed ), &other, plan ) );
    op = find( plan, STEAM_SCRIPT_STRING, "\\Registry\\Machine\\Software\\Maker\\Game", "Language" );
    assert( op && !strcmp( op->text, "German" ) );
    assert( steam_install_script_mark( mixed, strlen( mixed ), &other ) !=
            steam_install_script_mark( mixed, strlen( mixed ), &places ) );
    other = places;
    other.install_dir = "C:\\Steam\\Moved";
    assert( steam_install_script_mark( mixed, strlen( mixed ), &other ) !=
            steam_install_script_mark( mixed, strlen( mixed ), &places ) );
    assert( steam_install_script_mark( mixed, strlen( mixed ), &places ) ==
            steam_install_script_mark( mixed, strlen( mixed ), &places ) );

    /* Not scripts: cut short, a brace too many, a block with no name, too large. */
    assert( !steam_install_script_plan( fallout, strlen( fallout ) - 30, &places, plan ) && !plan->count );
    assert( !steam_install_script_plan( "\"a\" { } }", 9, &places, plan ) );
    assert( !steam_install_script_plan( "{ \"a\" \"b\" }", 11, &places, plan ) );
    assert( !steam_install_script_plan( "\"a\" \"unfinished", 15, &places, plan ) );
    assert( !steam_install_script_plan( "", 0, &places, plan ) );
    assert( (large = calloc( 1, STEAM_SCRIPT_MAX_BYTES + 2 )) );
    memset( large, ' ', STEAM_SCRIPT_MAX_BYTES + 1 );
    assert( !steam_install_script_plan( large, STEAM_SCRIPT_MAX_BYTES + 1, &places, plan ) );
    /* A value longer than fits is left out, not shortened. */
    memset( large, 'x', 2000 );
    {
        static const char head[] = "\"InstallScript\" { \"Registry\" { \"HKLM\\\\Software\\\\M\" { \"string\" { \"v\" \"";

        memcpy( large, head, strlen( head ) );
    }
    strcpy( large + 1000, "\" \"w\" \"ok\" } } } }" );
    assert( steam_install_script_plan( large, strlen( large ), &places, plan ) );
    assert( !find( plan, STEAM_SCRIPT_STRING, "\\Registry\\Machine\\Software\\M", "v" ) );
    op = find( plan, STEAM_SCRIPT_STRING, "\\Registry\\Machine\\Software\\M", "w" );
    assert( op && !strcmp( op->text, "ok" ) && plan->skipped == 1 );
    /* Nesting deeper than anything read does not lose the way back. */
    assert( steam_install_script_plan( "\"InstallScript\" { \"a\" { \"b\" { \"c\" { \"d\" { \"e\" { \"f\" { \"g\" { \"h\" { \"i\" { \"j\" \"k\" } } } } } } } } } \"Registry\" { \"HKLM\\\\Software\\\\N\" { } } }",
                                       strlen( "\"InstallScript\" { \"a\" { \"b\" { \"c\" { \"d\" { \"e\" { \"f\" { \"g\" { \"h\" { \"i\" { \"j\" \"k\" } } } } } } } } } \"Registry\" { \"HKLM\\\\Software\\\\N\" { } } }" ),
                                       &places, plan ) );
    assert( plan->count == 1 && find( plan, STEAM_SCRIPT_KEY, "\\Registry\\Machine\\Software\\N", "" ) );

    free( large );
    free( plan );
    puts( "Install script: registry entries, languages, folders, refused places and malformed text passed" );
    return 0;
}
