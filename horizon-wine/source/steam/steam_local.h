#ifndef AUTORUN_STEAM_LOCAL_H
#define AUTORUN_STEAM_LOCAL_H
/* Shared local SDK preparation for registered Steam installations. No login,
 * network/client process, game-name rule, EXE patch or credential export. */
enum steam_local_result { STEAM_LOCAL_NONE, STEAM_LOCAL_READY,
    STEAM_LOCAL_PAYLOAD_MISSING, STEAM_LOCAL_CONFLICT, STEAM_LOCAL_IO };
int steam_local_prepare(const char *runtime, const char *executable, unsigned executable_machine);
/* Steam2 provider for a registered installation whose x86 program needs the
 * old Steam.dll: recognised from the program file itself, or named by the
 * marker (legacy_local_client=1 to force, =0 to refuse). This does not
 * register a client, authenticate an account or launch a game. */
/* Valve's own Steam.dll, the one file of the provider that is not shipped:
 * the shell fetches it from Valve when a program first needs it. */
#define STEAM_LEGACY_DLL_SIZE 524952
#define STEAM_LEGACY_DLL_SHA256 "d9a06d104da9b119207125b61f590eee1618b1d1f84c7322e6202f0fe87b692a"
int steam_legacy_program(const char *executable);
struct steam_legacy_context { unsigned app_id; char client_dos_path[256]; };
int steam_legacy_prepare(const char *runtime, const char *executable,
                         unsigned executable_machine, struct steam_legacy_context *context);
const char *steam_local_error(int result);
#endif
