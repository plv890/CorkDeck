#ifndef AUTORUN_STEAM_PREREQUISITES_H
#define AUTORUN_STEAM_PREREQUISITES_H
#include <stdint.h>
/* Deliberately separate from game installation and the execution runtime. */
enum steam_prereq_arch { STEAM_PREREQ_X86, STEAM_PREREQ_X64 };
enum steam_prereq_error { STEAM_PREREQ_FILESYSTEM=3401, STEAM_PREREQ_NETWORK,
    STEAM_PREREQ_INTEGRITY, STEAM_PREREQ_SPACE, STEAM_PREREQ_CANCELLED,
    STEAM_PREREQ_PATH, STEAM_PREREQ_PROBE_MISSING, STEAM_PREREQ_BUSY };
enum steam_prereq_need { STEAM_PREREQ_VC14=1, STEAM_PREREQ_LEGACY_VC=2,
    STEAM_PREREQ_DX_HELPERS=4, STEAM_PREREQ_DOTNET=8, STEAM_PREREQ_CUSTOM=16,
    STEAM_PREREQ_STEAMWORKS=32 };
struct steam_prereq_catalog { const char *name,*file,*url,*sha256; uint64_t size; unsigned int machine; };
const struct steam_prereq_catalog *steam_prereq_catalog(unsigned int arch);
struct steam_prereq_requirements { unsigned int needs,architecture,files,limited,script; };
/* Bounded reads of PE imports and adjacent DLLs. No script commands executed.
 * A zero mask is Unknown, not proof that a game has no dependencies. */
unsigned int steam_prereq_inspect(const char *game,const char *executable,const char *script,
                                 struct steam_prereq_requirements *);
struct steam_prereq_progress { uint64_t downloaded,total; unsigned int error,http,transport; };
struct steam_prereq_callbacks { void *context; int (*cancel)(void *);
    void (*progress)(void *,const struct steam_prereq_progress *); };
/* Fixed Microsoft HTTPS origin, pinned length + SHA256, streaming to private
 * cache. No credentials, redirects, arbitrary URLs or bundled redistributables. */
unsigned int steam_prereq_fetch(const char *root,unsigned int arch,const struct steam_prereq_callbacks *);
int steam_prereq_cached(const char *root,unsigned int arch);
/* Prepare only provider-owned args and request files. Caller launches returned
 * EXE using the existing launcher. Download completion never marks Installed. */
unsigned int steam_prereq_prepare(const char *root,unsigned int arch,int probe,char out[512]);
/* Current probe result, checked against a fresh request nonce. 0 unknown,
 * 1 detected, 2 missing. The actual game remains compatibility Unknown. */
int steam_prereq_result(const char *root,unsigned int arch);
const char *steam_prereq_error_text(unsigned int);
#endif
