# Steam integration: engineering status

Latest installed native revision is `2026.10.06-11`. The -9 launcher now prepares
the minimal local Steamworks SDK for matching adjacent x86/x64 SDK imports using
trusted installation markers, original backups and conflict checks. Balatro
launched with that layer; the user also reached Dishonored's menu, although
performance remains very poor. This deliberately limited implementation does
not provide a full Steam client or online Steamworks services. Native -11 adds
[passive performance diagnostics](PERFORMANCE_DIAGNOSTICS.md), with no account
credentials in measurements. Earlier checkpoints below predate these changes.

Current native runtime `2026.10.06-8`: automatic device-bound session still
restores across NRO updates and Autorun process restarts. This does not provide a
guest Windows Steamworks client. PE prerequisite inspection now flags exact
steam_api.dll / steam_api64.dll imports (normal/delayed) as an advisory requirement.
Dishonored's latest localized crash dereferences a NULL SteamRemoteStorage object;
the original SDK interface initialization/environment remains a broad compatibility
gap. No replacement Steam API DLL or game binary patch applied. See
[COMPATIBILITY_TEST_20261006.md](docs/research/COMPATIBILITY_TEST_20261006.md) for evidence and
the general client/bridge integration path. Download installation success remains
separate from launch compatibility.

2026-10-06: native revision `2026.10.06-1` adds live verification heartbeat and
the missing stage timings to LAN status. Device-bound sign-in survived the NRO
update. Download and performance evidence is in
[DOWNLOAD_TEST_20261006.md](docs/research/DOWNLOAD_TEST_20261006.md); this observability change
does not establish an improvement in download speed.

## Automatic session revision 2026.10.05-4

The account's **Save encrypted session** now offers **Automatic on this console**
and the existing local-passphrase mode. Automatic mode uses vault format v2 with
AES-256-GCM and fresh public salt/nonce. SPL's device-unique KEK mode (option 1,
fixed generation 4 / 4.0.0 device master key) derives an ephemeral encryption key
through two domain-separated CMACs. No key/passphrase is stored beside the SD
vault; boot-specific sealed handles are regenerated, never persisted. This binds
unlock to the console, **not to Autorun**: other privileged homebrew on that same
console can derive it. Key derivation failure has no software/plaintext fallback.
Implementation uses public libnx APIs, not copied third-party implementation:
[libnx SPL](https://switchbrew.github.io/libnx/spl_8h_source.html),
[Atmosphere device-unique KEK semantics](https://github.com/Atmosphere-NX/Atmosphere/blob/master/exosphere/program/source/smc/secmon_smc_aes.cpp).

Vault v1 remains manually unlocked and is never silently converted. Saving v2
is explicit consent; the existing atomic `.tmp`/`.previous` replacement and
Logout deletion apply to both formats. Only account/SteamID/refresh token are
encrypted. Steam password, access token and Guard codes are not persisted.
Startup restores v2 on the existing CM worker; Online requires server logon with
matching identity and a nonzero CM session. A transport interruption permits
three token-logon retries with 2/4/8-second waits; server rejection, invalid
responses and local decryption errors stop. Retry exhaustion requires reconnect
from settings or runtime restart; it is not an indefinite network monitor.
Interrupted installations retain Resume data and are not automatically replayed.
Server token expiry/revocation still requires a new Steam sign-in; Logout only
deletes local credentials, not server revocation.

2026-10-05 host checks: Steam/session/CM and UI fixtures pass under ASan/UBSan,
shell/manual Play regression passes. Synthetic device tests cover wrong device,
unavailable derivation, tamper, no plaintext and v1/v2 separation. Invalid v2
startup remains offline, preserves the vault and Logout removes it. These are
**not hardware crypto proof by themselves**. On the physical OLED, first Steam
login and **Saved for automatic sign-in on this console** succeeded. Full
HOME/X/A close followed by fresh launch restored green signed-in status without
QR/Guard/passphrase and loaded 617 available games (including Steam Families).
This confirms device-key save/load across process restart. Game-return/network
and console-reboot acceptance remains to be completed. NRO installed with
backup and SHA readback. Evidence:
`../artifacts/steam-persistent-session` (local, outside Git).

Remote physical-device bench setup and PC adapter map are tracked separately in
`REMOTE_DEBUGGING.md`. The broker does not change Steam or the execution backend;
sys-botbase input/capture and launch/close were checked on physical OLED. Saved
LAN log tails were collected. Initial foreground-independent FTP/GDB activation
was followed by a reported Atmosphere error/reboot; both channels are unverified
and both additions were rolled back through Hekate SD USB; stable reboot/recovery
confirmation is pending (see REMOTE_DEBUGGING.md). Game compatibility,
prerequisites execution and full update/relaunch
validation remain separate pending checks.

Status: native Steam UI / LAN diagnostics / bounded small-file batches / per-game language choice candidate
(2026.10.02-6), requiring hardware validation of scheduling, language fallback and integrity repair.
The owner confirmed explicit cancellation cache cleanup works on the physical Switch.
QR/mobile confirmation, a labeled username/password form, email/authenticator Steam Guard, CM logon,
heartbeat and local logout are wired to Settings > Steam settings > Sign in to Steam.
A green OK requires a successful authenticated CM response with the expected SteamID.
The user confirmed real QR approval and green authenticated OK on Horizon after f6d028fd.

The user confirmed actual library enumeration, direct Steam CDN download to SD,
registration and launching at least one downloaded game on the physical Switch.
Connection reuse and the first SD pipeline increased speed to a reported 1-2 MB/s,
but overall installation remains slow. This candidate adds a bounded native
libcurl multi pool (default 2, optional 1 or 4 concurrent chunk requests), preserves
one SD writer, uses the authenticated CM cell ID for CDN discovery, and fixes
Horizon diagnostic-file replacement. It also shares Autorun's main header and
UI components, fetches uninstalled-game covers automatically and separates Steam
sign-in, encrypted session saving, unlock and download preferences.
The new transfer speed, live artwork and revised persistence workflow require
physical Switch validation. Family depot entitlement and vault restoration
remain separately subject to hardware validation.
The full original Definition of Done remains incomplete.

## Library / Installed / Downloads candidate

Steam and Downloads are now directly accessible from Autorun's main header.
Account linking, language and session storage stay in Steam settings. After green
OK, Y on the account screen also opens Library, Installed and Downloads tabs.
L/R or the tab headers switch tabs. X searches by title or AppID, + refreshes the
owned and family library. Ten tiles per page support directional and touch selection.
A opens a game card with the estimated selected Windows depot size. Metadata is
requested only for the current selection, with one bounded snapshot, not eagerly
for the whole library. Press A on the card to Install or Add/Choose executable.
On an installed game's card, Y opens an explicit Delete game confirmation. It
warns that local saves/modifications within that folder will also be deleted. Play remains in the
normal Autorun library, with the same existing per-game settings and launch path.
Closing the Steam screen leaves the worker downloading. Exiting Autorun or launching
a game interrupts the worker safely; later sign-in and Install reuses valid staging chunks.
Explicit Y / Cancel in Downloads instead discards all unfinished installation data for
that AppID, including previous attempts. Wait for "Cancelled - installation cache cleared".
Existing installed folders are preserved. A filesystem failure shows error 3112 and its
cleanup errno; Y retries cleanup. The brief final installation commit cannot be cancelled.

The library uses authenticated CM Player.GetOwnedGames, not a public profile or
third-party API key. FamilyGroups.GetFamilyGroupForUser obtains membership for the
authenticated account; GetSharedLibraryApps obtains its included shared apps.
Family entries are marked "Family", require a server-provided owner and are merged
by AppID with owned entries taking precedence. Excluded entries are omitted. A
family-service failure preserves owned results and displays a separate warning.
The provider only queues installation of a listed owned or included family AppID.
Every selected depot still requires Steam's authenticated depot-key and manifest-code
responses under the signed-in account, including when a cached manifest exists. A
family library listing is not proof of depot access. Refused family licenses are
reported; no lender impersonation, license fabrication or DRM bypass is performed.
Public metadata alone grants no
download access. CDN 401/403 may request the normal authenticated CDN query token;
that token stays in memory, is wiped and is never logged or placed in game metadata.

Initial downloads select public Windows depots and the configured language (default
English), preferring 64-bit where
the app provides separate architecture depots. Shared app depot metadata is resolved
when available; access is still checked for the selected owned app. DLC and beta
branches are not included. Steam settings offers 30 Steam language identifiers in a scrolling settings list.
Language-neutral depots remain selected. A missing language-specific Windows depot
reports error 3111 rather than silently installing a different language. Some games
ship all languages in shared files and require selection in the game's own settings.
Changing the preference affects subsequent installations, not an active transfer or
completed installation; language reinstall/update support is still pending.
OS support and an approximate selected-depot size are now shown before Install.
Metadata size can be unknown; it is not the compressed network transfer size.
Exact manifest sizes and SD free space are checked by the installer before files
are fetched. Press + in a game card to retry failed metadata. Visible tiles fetch portrait covers
from the official public Steam CDN automatically, including uninstalled games.
The cover service sends no credentials. It uses one separate fetch/decode worker,
12 texture slots, images capped at 2 MiB/2048 pixels per dimension, a 384-pixel
texture bound and an optional 64 MiB/512-image disk cache. New artwork requests
pause during installation; cached covers can still appear. Missing covers use a
neutral placeholder. Registered games retain Autorun's existing artwork system.

Storage, relative to the existing runtime directory (normally sdmc:/switch/wine):

- `drive_c/Steam/<Steam title>`: new completed installations, metadata and executable.
  Invalid FAT path characters are replaced; reserved names are prefixed, titles
  are bounded without splitting UTF-8 and a conflicting folder gets an AppID suffix.
  Existing numeric AppID directories stay supported and are not renamed automatically.
  On an older installation, Add/Choose executable refreshes a missing display-title
  override from the Steam name, preserving a user's existing custom title/settings.
- `steam/installed/<AppID>.path`: atomic relative-folder locator. Written before
  commit; a matching completed installation marker is still required for Installed.
- `steam/trash/<AppID>`: interrupted removal. The matching marker is removed last;
  after re-login/refresh, a remaining marked trash folder appears as Removal pending
  under Installed. Delete game retries it. It is never offered as a runnable install.
- `steam/staging/<AppID>`: interrupted installation; never marked Installed.
- `steam/staging/<AppID>.plan`: exact depot/manifest set for resume.
- `steam/manifests/<AppID>`: cached raw manifests, no depot keys or tokens.
- `steam/selected/<AppID>.exe`: manually selected relative executable, atomic write.
- `logs/steam-download.log`: append-only numeric transfer diagnostics; copy this file for support.
  At 512 KiB it rotates to `steam-download.log.previous`.
- `logs/steam-diagnostic.log`: authentication/CM diagnostics, no credentials.
- `logs/steam-storage.log`: last uninstall result, AppID and numeric error only.
- `steam/artwork/<AppID>.jpg`: optional public cover cache, no credentials.
- `steam-settings.txt`: download language and concurrency; recoverable replacement, no secrets.
- `steam-session.vault`: optional encrypted refresh-token session, never plaintext.

One provider worker owns CM and all game HTTPS requests. `steam_cdn` uses libcurl
multi to maintain 1/2/4 bounded requests with verified TLS and persistent connection
caches across files. No additional network thread is created per chunk. A separate
SD worker decrypts, decompresses, checks and writes completed chunks out of network
completion order to manifest offsets, with exactly one file writer. No CM or token
RPC runs on that thread. Concurrent 403 responses reuse the same freshly obtained
normal Steam CDN token per host/depot. Signed URLs and tokens stay in memory.
Cancellation removes every CURL handle's ownership of encrypted buffers before
joining/freeing the writer queue; valid chunks are rechecked on resume.
Sync is batched at 4 MiB or a one-second threshold checked after each write, and
at file end or cancellation. Only synchronized validated bytes count as durable
progress. Default concurrency has 3 encrypted slots, about 20 MiB of explicit
chunk buffers; concurrency 4 has 5 slots, about 28 MiB, excluding bounded manifests
and decoder allocations for the large-file pipeline.

Small single-chunk files up to 256 KiB now use `steam_batch`: collect at most 32
unique paths and 4 MiB of uncompressed data, then feed the existing CURL multi pool
into one SD worker across those files. The network can fetch following files while
that worker decrypts, verifies and synchronizes another file. Response buffers are
bounded to concurrency+1 (2/3/5 x 256 KiB+64), about 0.5/0.75/1.25 MiB beyond the
existing 8 MiB raw/scratch buffers; metadata adds roughly 15 KiB. There is no second
full batch of decoded files and no extra SD writer per file. Buffers are reused as
soon as their file finishes. Even concurrency=1 overlaps the next request with SD
work, though requests themselves remain sequential in that mode.

Each missing file is still checked against chunk checksum/SHA-1 and full-file SHA-1
read back from the filesystem, synchronized and closed before it contributes to
verified progress. Existing correct exact-size files are rehashed and skipped.
Directory entries, empty files, larger/multi-chunk files, depot changes, duplicate
paths, and the 32-file/4-MiB bounds drain the current batch before continuing the
manifest. This preserves overlay order and protects the existing large-file path.
CURL ownership is released before stopping/joining the batch writer, so explicit
Cancel still deletes all partial data after all writers stop. Ordinary interruption
retains validated files for resume. Final game registration/commit is unchanged.

This is bounded prefetch and overlapping I/O, not one giant archive or an unsynced
RAM cache. Per-file filesystem creation/verification/flush still costs time, and
this is not a guarantee of 10-15 MB/s on a physical Switch. No measured speedup is
claimed for this candidate until device validation. Download UI, LAN memory
snapshot and safe numeric exports add `batch_files` (newly downloaded files that
passed verification/flush) and `batch_peak_files` (maximum occupied response slots,
including network requests) so diagnostics do not depend solely on the SD log.
Manifests are
read one mapping at a time, chunks are bounded to 4 MiB, and a file's chunk offsets
are sorted and checked for exact nonoverlapping coverage. AES decoding, LZMA/VZa,
Zstandard/VSZa and ZIP chunks have checksum/SHA-1 checks; completed files have a
whole-file SHA-1 check. HTTPS verifies peer and hostname, with redirects/proxies
disabled and a *.steamcontent.com allowlist. Manifest signatures are parsed as a
required section but not cryptographically verified; downloaded content is protected
by HTTPS, ZIP CRC and chunk/file integrity hashes. Local SD metadata is not a trust vault.

Progress reports verified uncompressed data/total, transferred network bytes,
current throughput over windows of at least one second, elapsed time, average
active HTTPS throughput and SD free space. Numeric diagnostics include request and
new-connection counts, HTTPS milliseconds, total elapsed milliseconds and decoding,
write and sync milliseconds. Timings of overlapping phases are not additive. They allow
separating repeated connections/network latency from processing and SD overhead.
ETA is deliberately omitted until a reliable
compressed-size estimate exists. Retries try bounded official hosts. Network failure or
ordinary application shutdown retains staging; retry hashes and reuses good chunks and
repairs corrupt ones. Explicit Cancel joins all CDN/SD producers and then removes
`steam/staging/<AppID>`, the AppID plan (including temporary/previous forms), and all
`steam/manifests/<AppID>` generations, partial archives and temporary files. It does
not remove installed games, settings, session vaults, shared metadata or artwork.
Cleanup also runs on graceful exit immediately after Cancel and needs no Steam
connection. Forced process termination, power loss or SD removal can interrupt it;
retry Cancel/clear partial files after reopening the app (select Install to expose
an older unfinished AppID if necessary). A failed cleanup blocks new Install until
retried successfully; it never reports cache cleared on a filesystem error.
An exact plan change reports error 3110 instead of combining old/new partial files.
Files over UINT32_MAX are refused in this first candidate on both FAT32 and exFAT.
Preflight space validation is conservative and ENOSPC is still handled during writes.

After validation, a flushed installation marker and same-SD staging directory rename
publish the installation. Existing installed folders are preserved and cannot be
overwritten by Install. Interrupted directory rename durability depends on Horizon/SD;
this is not a guarantee against power loss. Compatibility starts as Unknown, even if
download succeeds. Steam launch metadata or a single .exe supplies the executable;
otherwise the user chooses it. No Steam API emulation, DRM bypass or runtime changes.

Uninstall is queued on the provider worker and refuses active installations/downloads.
It verifies the AppID marker, preflights a bounded tree, refuses symbolic links and
unexpected paths, renames only that installation into its AppID trash folder and
removes it without following links. Recursive operations tick CM between entries.
On success the launcher adapter removes only catalog entries below the recorded
original directory. Other games, runtime DLLs, shared prefixes and global settings
are preserved. No secure SD erase or power-loss transaction guarantee is claimed.
Do not modify the installation from another process while it is being removed.

Still pending: automatic protected session unlock/reconnect, standalone Verify/Repair
of completed installs, incremental updates, queue/concurrency controls,
Steam-specific artwork, ETA, DLC/branches and changing a completed install's language. Repair of partial staging
is implemented; it is not yet the completed-install Verify feature. The full original
Definition of Done is therefore not achieved by this candidate.

Host verification: `sh horizon-wine/tests/check-steam-host.sh` and
`sh horizon-wine/tests/check-steam-ui-host.sh`. `check-steam-shell-host.sh` exercises
the main header and a manually registered game using the existing launcher fixture.
Tests cover allowlisted queuing, family merge/exclusion and language selection,
depot denial, cancel/resume with chunk reuse, corrupt staging repair, final atomic
publication and preserving an existing install, in addition to protocol/auth checks.
Fixtures are synthetic and use no account. Storage tests cover named/colliding/
reserved paths, legacy folders, refusal to delete unmarked or linked trees, and
retrying interrupted removal. A blocked-decoder fixture proves the producer can
fill a second slot, a third is unavailable, and ENOSPC cannot publish unsynced bytes.
UI tests navigate all 30 languages and assert the last selection. The HTTPS reuse
fixture runs against a local ephemeral TLS server, never Steam. `tests/steam_lzma_sdk.c` separately
checks the SDK's LZMA1EXT path with and without end markers using native liblzma >=5.4.
The baseline full launcher suite remains failing as documented in BUILDING.md;
Steam-specific checks passing do not change that baseline status.

## Steam-style sign-in screen (2026-10-01)

The native screen uses Steam's dark/blue sign-in layout: explicit ACCOUNT NAME and
PASSWORD fields on the left, Sign in button, QR panel on the right. A fresh QR
session starts when opening the screen while offline without a saved vault. Use the Steam mobile app to
scan and confirm it. Selecting the QR panel regenerates the challenge; server
challenge rotations update the image. Arrow buttons or touch select form fields;
A opens the labeled keyboard. Password and Guard inputs are masked. Closing the
screen clears its password buffer. This is native UI, not Steam's embedded webpage.

QR text and pixels are transient and never written to SD or logs. Only the official
s.team/q challenge origin is accepted. QR token subject is used solely as a CM
routing hint; successful authenticated server logon must confirm the same SteamID
before green OK. The QR API was tested against real Steam CM without an account.
Mobile approval/token receipt/CM logon was subsequently confirmed by the user on Switch.
Offline screenshots use a synthetic challenge and mock SteamID, not a real login.

QR generation uses pinned Project Nayuki MIT C code (revision in
horizon-wine/vendor/qrcodegen/REVISION); no heap allocation and version capped at 12.
Its notice is packaged as licenses/qrcodegen-MIT.txt. The existing execution pipeline
has no changes. Host UI fixtures: sh horizon-wine/tests/check-steam-ui-host.sh.

## Implementation and validation

The provider owns a single SDL worker; UI reads only status, numeric errors and SteamID.
The transport uses existing libcurl/Horizon SSL with peer/hostname verification and
wslay for WebSocket frames. Official directory endpoints are restricted to
*.steamserver.net with valid ports. Discovery retries at most three endpoints.
Protobuf parsing, messages and decompressed Multi packets have explicit memory limits.
RSA PKCS#1 v1.5 uses mbedTLS and Horizon CSRNG, following Steam authentication.
Passwords and Guard codes are cleared from owned buffers; no payload logging exists.
Dependency-internal allocations are not guaranteed to be zeroized.
The diagnostic writer uses an allowlist of state/error/guard-type integers; it never
serializes credentials or raw server messages.

Host ASan/UBSan tests cover protocol fixtures, malformed inputs, Guard retries and
secret cleanup. A live host test received a valid RSA RPC reply from official Steam
CM using a synthetic account name; no real account/password was used. These tests
do not substitute for device validation. The user has confirmed real login and the earlier
HTTPS connectivity probe on the physical Switch (HTTP 200, transport 0).
Baseline launcher/game compatibility findings are recorded separately in
 documentation/baseline-hardware-2026-09-30.md. Execution source remains unchanged.

## Dependency licenses

wslay: MIT/Expat, copyright Tatsuhiro Tsujikawa (2011, 2012). Its full notice is
packaged as licenses/wslay-MIT.txt. mbedTLS is an existing SDK dependency under
Apache-2.0. SteamKit remains protocol reference only (LGPL-2.1); GameNative and
DepotDownloader are behavioral references only; no GPL source was copied.
node-steam-session (MIT, Alex Corn 2022, reference commit
3ac0f34fd964b3f886ba18ef4824ac43c942e030) was inspected without copying source.

The older probe implementation below remains useful for isolated transport testing;
its preview UI is replaced by the account screen in this candidate.
## Connection preview

Open Settings > Library > Steam (preview) > Test Steam connection.
The worker calls Steam's official ISteamDirectory/GetCMListForConnect v1 API
using existing libcurl/Horizon SSL, with peer and hostname verification enabled.
It does not authenticate. UI remains interactive; B cancels. Transfer deadline
is 20 seconds, connect deadline 10 seconds, response memory limit 64 KiB.
Redirects and proxies are disabled for this fixed-origin diagnostic.

The last result is exported automatically to `switch/wine/logs/steam-diagnostic.log`.
Only fixed event/version and numeric result, HTTP and curl codes are written.
No arbitrary remote strings or raw responses enter this logger. This is an
allowlisted diagnostic, not a general-purpose credential redactor. Failure to
write the log is displayed. There are no credentials or session files yet.

Module entry points:
- `source/steam/steam_ui.c`: SDL screen and worker lifetime; joins before exit.
- `source/steam/steam_probe.c`: bounded HTTPS diagnostic and typed errors.
- `source/steam/steam_directory.c`: network-free strict response validation.
- `tests/check-steam-host.sh [--live]`: ASan/UBSan malformed-response tests,
  pre-cancellation test, and optional real unauthenticated Steam request.

Host checks passed; live request returned result=0/http=200/transport=0.
Switch NRO compiled and package verification passed. Certificate-negative tests,
mid-transfer cancellation and the SDL screen still need further validation.
The preview does not establish that authentication or CM transport works.
No new dependency or reference source was copied: existing curl, SDL and Autorun's
JSON reader are reused. SteamKit was consulted for API endpoint/response shape.

## Boundary

Steam authenticates, discovers licensed applications, downloads and verifies
files, manages installation transactions, and registers an executable with the
existing launcher. FEX, Box64, Wine, Mesa, DXVK, VKD3D, input, prefixes and launch
arguments remain unchanged. Manually added games must remain independent of Steam.

## Source audit (2026-09-30)

| Project | Reviewed revision | License evidence | Use |
| --- | --- | --- | --- |
| Autorun | `91107d45eb3204ff80d3637bfa37cefd021a3a1c` | `LICENSE`: LGPL-2.1-or-later; bundled components have separate notices | Existing application |
| GameNative | `fa9dec8a4e293ad63b67623c5327f16d332b444a` | Root `LICENSE`: GPL v3 text | Behavioral reference only |
| DepotDownloader | `989f37b1fbc012798bf41bdf0e66a60adf785ae5` | Root `LICENSE`: GPL v2 text | Behavioral reference only |
| SteamKit | `84c990c3982eedb5abd733116b987c9870a0dccb` | `SteamKit2.csproj`: LGPL-2.1-only | Protocol reference; possible narrowly scoped port |

No reference source has been copied or linked. This table does not replace a
file-level and transitive dependency audit before adopting any implementation.
In particular, GPL code must not be copied into this LGPL feature as though it
were permissively licensed. Preserve attribution and license terms for any
future SteamKit-derived files separately.
SteamKit's package metadata says `LGPL-2.1-only`, while its internal `license.txt`
grant says version 2.1 or later (Ryan Stecker & SteamRE Team, 2018). Preserve both
pieces of upstream evidence; do not silently rewrite notices. Its `3rd party.txt`
lists protobuf-net under Apache-2.0, but current package references additionally
include CsWin32, System.IO.Hashing and ZstdSharp.Port. That notice is not a complete
audit of the current transitive graph.

## Existing integration points

- `horizon-wine/source/launcher.c`: SDL UI, `add_game`, `describe_program`,
  `save_library`, `apply_new_game_renderer`, and existing Play flow. Registration
  must preserve the normal add-game settings behavior through a small adapter.
- `launcher_catalog.[ch]`: INI catalog, currently 256 entries; native paths have
  512-byte buffers. `launcher_list.h` also bounds DOS paths. Steam's full library
  must therefore be paginated separately from installed launcher entries.
- `launcher_pe.h`, `launcher_list.h`: executable identification and path mapping.
- `launcher_settings.h` and launcher settings helpers: existing per-game config.
- `steamgriddb.c`, `launcher_image.c`: existing artwork; SteamGridDB is not Steam
  authentication and must never receive Steam credentials.
- `autorun_update.c`, `horizon_dlls_curl.c`: libcurl networking and download
  examples. There is no general Steam network transport. The self-updater enables
  certificate/hostname verification; preserve strict TLS for the new transport.
- Filesystem operations use stdio/POSIX and libnx rather than a generic storage
  service. Catalog temporary/backup writes illustrate recovery but do not prove
  power-loss durability on FAT32/exFAT.
- `horizon-wine/CMakeLists.txt`: existing curl, mbedTLS and zstd linkage; runtime
  and graphics components remain outside the Steam feature.

## Reference findings

GameNative's `SteamService.kt` uses JavaSteam authentication sessions, callbacks,
Steam Guard, QR polling, and refresh-token logon. License callbacks feed package
PICS requests and application metadata stored through Room DAOs. It selects depots
and delegates downloads to `javasteam-depotdownloader`, passing licenses, branch,
branch password, install directory, cancellation and progress listeners. DLC is
queued separately; updates compare installed depot manifest IDs. Its actual
downloader is a separate dependency, not all contained in SteamService.
`PrefManager.kt` encrypts tokens through `Crypto.kt`, which uses AndroidKeyStore.
Horizon does not gain AndroidKeyStore by copying that code. Debug examples and
QR challenge logging must not be carried over.

DepotDownloader's `Steam3Session.cs` obtains auth-session tokens, logs on using
the refresh token, receives licenses, requests PICS access/product information,
depot keys and manifest request codes. `ContentDownloader.cs` filters OS/arch/
language, resolves public or private branch manifests and shared depots, compares
previous manifests and chunks, and schedules concurrent CDN work. It updates
installed-manifest records after completion. Its in-place file/update algorithm
is not a ready-made transaction guaranteeing preservation of our old installation.
`AccountSettingsStore.cs` stores compressed protobuf tokens in .NET isolated
storage: compression and isolated storage are not credential encryption.

SteamKit implements modern authentication via unified messages in
`Steam/Authentication`: RSA-encrypted credentials, Guard confirmation, QR sessions,
polling and token generation. CDN code and `Types/DepotManifest.cs` handle depot
manifests and chunks. `Steam/CDN/DepotChunk.cs` decrypts AES-256 content, handles
Valve zstd/LZMA or ZIP forms, and checks decompressed length and an Adler checksum
with seed zero. These formats require bounded parsing and independent tests.

SteamKit targets .NET 8/10; GameNative's library requires Java/Android services.
Neither is a native Horizon dependency. Publishing .NET for `linux-arm64` does
not target Horizon (see [Microsoft Native AOT platforms](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/)).

An additional native candidate, [steam-min-cpp](https://github.com/SteamCPP/steam-min-cpp)
at `1dcca938df1967be3d913d0153491ee32fcc2fa1`, has an MIT license and TCP CM,
anonymous login and product-information code. It requires Boost, Crypto++ and
protobuf, and does not provide the modern account-authentication flow needed
here. It has not been adopted; C++ alone does not make it a ready Horizon backend.

## Provider architecture (partially implemented)

The following design notes are retained from the initial feasibility research.
The current implementation is listed above; transport dependencies are now selected.

Current module map, under `horizon-wine/source/steam/`:

| Module | Responsibility |
| --- | --- |
| provider | Owns authenticated CM worker, library snapshots and install queue |
| library | Bounded owned-game parsing, title/AppID search and paging |
| backend / app | Authenticated PICS/depot/CDN services and Windows/public depot metadata |
| content | AES, safe relative paths, bounded LZMA/Zstd/ZIP chunks and hashes |
| manifest | One file mapping at a time, identity checks and chunk iteration |
| install | Space checks, staging/resume, validation, final commit and selected executable |
| library_ui | Library/Installed/Downloads, progress, errors and launcher adapter calls |
| ui | Existing native Steam sign-in screen and library entry point |

Only `launcher.c` supplies the catalog registration/file-picker adapter. Existing
execution code does not import the Steam backend. UI never handles depot keys or tokens.

Use native C/C++ modules with a small C interface, separate from SDL and runtime:
auth/session, CM transport, licensed library/PICS, depot/manifests, downloader,
installation journal, updates/verify, and launcher registration adapter. A bounded
event queue carries progress and typed errors to UI. First establish a native
protocol feasibility spike, guided by SteamKit; do not port .NET or add a server
that receives credentials. The exact transport/serialization dependency remains
unselected until baseline toolchain capabilities are verified.

Toolchain inspection found libcurl **7.69.1**, without `curl/websockets.h`.
Existing HTTPS downloads therefore do not supply Steam CM WebSocket transport.
Native mbedTLS 2.28.10, libsodium and liblzma are present. A separately scoped
transport dependency or implementation must be evaluated without upgrading the
execution stack. libnx SPL exposes crypto/RNG calls, but their presence alone
does not establish an application-isolated credential vault.

### Module map

Keep the implementation under `horizon-wine/source/steam/`; public headers expose
opaque handles and bounded value types. Dependencies flow in one direction:

```text
steam_ui -> steam_provider -> auth / library / install
auth, library, install -> cm_client
install -> depot_catalog -> manifest -> cdn_client -> chunk_codec
install -> install_journal -> filesystem
install -> launcher_registration (the only launcher catalog adapter)
```

`steam_provider` owns worker lifetime and the bounded event queue. UI never sees
tokens, depot keys, cookies or raw protocol messages. `cm_client` owns framing,
jobs, heartbeats and reconnect state, while authentication only owns the modern
auth state machine. `cdn_client` reuses verified HTTPS transport but has no access
to account passwords or refresh-token storage. `chunk_codec` is network-free and
unit-testable with fixtures. `install_journal` is the sole writer of installed
state and uses prepare/verify/commit/recover phases. `launcher_registration`
accepts a verified install directory plus selected EXE and calls existing launcher
catalog/settings APIs; it never launches a program.

Each module gets an independent host test and explicit memory/input limits. Avoid
a catch-all Steam object, global credentials, protocol code in SDL callbacks, or
direct catalog writes from the downloader. Read only this map and the module's
neighbors for routine changes; revisit broader Autorun code only when an interface
boundary actually requires it.

Symbol inspection of the pinned `libcurl.a` shows the Horizon SSL service backend
(`sslContextCreateConnection`, `sslConnectionSetVerifyOption`, etc.), rather than
desktop OpenSSL. Prefer this existing verified-HTTPS path for CM discovery/CDN;
verify its trust, hostname and failure behavior on hardware. Do not assume an
`/etc/ssl` CA bundle or disable verification to work around certificate failures.

Authentication success requires authenticated account information from Steam,
not merely possession or local decoding of a token. Library metadata alone never
authorizes downloading: normal package/depot entitlement and key/request-code
workflows remain mandatory. Start with public Windows depots; fail closed on
unsupported cases. Unknown compatibility remains Unknown after installation.

Start with two configurable CDN requests and one serialized writer, then tune on
hardware. Bound response bodies, manifests, decompression and work queues. Reject
absolute/traversal paths, symlinks, case collisions, overlong paths and unsupported
FAT32 file sizes before writing. Resume only reverified chunks. Stage updates
outside the active version, journal commits, and retain recovery data until the
new version is verified. Account for staging plus rollback space. Do not remove
unmanaged files or saves during updates or uninstall.

## Credential storage design gate

No application-isolated homebrew credential store has been established. The
[libnx SPL API](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/spl.h)
exposes cryptographic operations but is not evidence of an app-specific vault.
SD permissions, a key beside encrypted data, a serial/device ID or a hardcoded key
are not used as token protection.

Sessions remain in RAM by default. "Save encrypted session" (or + on the signed-in
account screen) opts into `steam_session`: AES-256-GCM with a random 128-bit salt,
96-bit nonce and 128-bit tag, PBKDF2-HMAC-SHA256 with 600,000 iterations and a separate
local passphrase of at least 12 characters (at most 127 UTF-8 bytes). The work factor follows the
[OWASP PBKDF2 guidance](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html#pbkdf2);
KDF delay needs Switch measurement. Use a strong, unique passphrase. It is never
saved on SD. Only account name, SteamID and refresh token are encrypted; Steam
password, Guard codes, access token and cookies are not persisted. File format and
length are authenticated as associated data. Writes flush and replace a temporary
ciphertext file atomically; malformed/oversized files, wrong passphrase, tampering
and unsafe file types fail closed. This does not protect against malicious homebrew
reading Autorun's memory while unlocked, nor against weak-passphrase offline guessing.

After restart (including returning from a game), open Steam or Steam settings and
unlock once with the **local vault passphrase**, not a Steam password/Guard code.
Unlock runs on the worker. Exact authenticated Steam CM logon is still required
before green OK and library access; token decoding alone grants nothing. Expired or
revoked tokens require Steam sign-in again. A fresh QR/password login replaces the
local link; save it explicitly afterward. This is persistent encrypted storage with
manual unlock, not transparent hardware-backed storage.

Diagnostics should serialize allowlisted error codes and numeric context only,
not raw exceptions, requests, responses, headers or QR challenges. Test redaction
with synthetic secrets. Logout cancels work, clears memory and removes local vault
and temporary credentials. Local deletion must not be described as server-side
revocation or secure erasure on flash; remote revocation needs a separate supported
Steam operation or the user's Steam account controls.

## Required validation gates

### Download diagnostics and installation recovery (2026-10-02)

For a fresh SD installation use the **full** archive. It already contains the
current NRO; an update ZIP alone omits Wine's NLS data and fonts. The user reported
installing only that update into an empty Wine folder. Their game log fails at
opening `share/wine/nls/locale.nls` (STATUS_OBJECT_NAME_NOT_FOUND), followed by
Wine locale/system-DLL initialization failure before the game starts. All three
validated full Steam packages contain the identical 794,914-byte locale file
(SHA256 `868e5d3a3f115c2649706f566f9741aa3e2c6927c10b3f07d5e4c76b0e403115`).
Restore missing base resources from the same pinned build; do not adjust the
execution backend to compensate for an incomplete installation. Recovery does
not establish game compatibility: the repaired installation needs a device test.

The Steam catalog adapter now persists **only** the title field before applying
Autorun's existing first-add renderer rule. Saving all implicit settings during
title import inadvertently suppressed that rule. Existing explicit renderer and
other per-game settings are preserved; previously imported settings are not
rewritten automatically. An offline test compares Steam registration with the
actual manual first-add rule and verifies that explicit/unknown keys survive.

`switch/wine/logs/steam-download.log` is written by the installer independently
of the UI. It appends `steam_download_v2` records every five seconds and on actual
initial/terminal operation states. Idle startup, Play or logout never writes zero
transfer records. The first actual new operation replaces an old v1 zero snapshot.
Append avoids Horizon's non-overwriting rename; rotation renames into a removed
backup path. The Downloads UI exposes a diagnostic-write errno when saving fails.
This is diagnostic persistence, not a power-loss-proof installation journal.
All fields are numeric allowlists:

- `elapsed_ms`: wall time including metadata, transfer and verification.
- `network_ms`: sum of completed request durations, including failures. With
  concurrent requests it can exceed elapsed time; it is not network utilization.
- `first_byte_ms`: sum of request start-to-first-byte (DNS/connect/TLS/server
  wait); `body_ms`: sum of transfer time after first byte. They overlap between
  requests and cannot be used as a sequential percentage breakdown.
- `decode_ms`, `write_ms`, `sync_ms`: SD-worker decryption/decompression/checks,
  writes and synchronization. `verify_ms`: final file SHA1 reads/checks;
  `resume_ms`: checking existing staging chunks.
- `writer_wait_ms`: time with no active network request while waiting for a
  bounded SD slot, plus final worker drain. Active network/SD overlap is excluded.
- `requests`, `connections`, `network`: completed request/new-connection counts
  and compressed bytes received, including retries. Interrupted in-flight bytes
  are counted but unfinished-request durations are omitted.
- `parallelism`, `peak_requests`: configured and maximum active chunk requests.
  Tiny files/manifests may use only the serial path.

No signed URLs, headers, file paths, account IDs, responses or secrets enter this
log. Overall installation rate is validated bytes divided by total wall time;
it differs from compressed network speed. ETA is approximate and may fluctuate
between metadata, decoding and verification phases.

### Separate Steam settings and Horizon session persistence

Settings > Steam settings has separate Sign in, Save encrypted session, Unlock
saved session, Download language, Parallel CDN requests, Logout and About rows.
Green **OK - signed in** only means authenticated for this run. **Saved on SD**
means an encrypted vault exists. Leaving the account form without a saved session
prompts to save; declining preserves only the current in-memory session. Saving
waits for the worker result and explicitly confirms success/error before returning.
Returning Back alone neither logs out nor deletes a saved vault.
A separate local passphrase still protects the refresh token; a restart requires
manual unlock. Steam passwords are never saved. Fresh sign-in replaces the local
link and therefore needs a new explicit save.

`steam_file` handles Horizon's refusal to rename over an existing file for the
provider-owned settings and encrypted vault: keep a `.previous` backup, publish
the closed/fsynced temporary file, recover the previous file if publication fails.
Readers recover a missing live file after an interrupted replacement. A power-loss
gap during the two renames remains possible; this is recoverable replacement, not
POSIX atomic-overwrite semantics or a hardware-backed credential store. Vault
backups remain encrypted; logout deletes live, temporary and previous vaults.

### Module boundaries for the current change

- `steam_cdn`: bounded libcurl multi transport, no ownership decisions or SD writes.
- `steam_pipeline`: checked chunk decode and single SD writer for large files.
- `steam_batch`: bounded buffers and one SD writer across distinct small files.
- `steam_install` / `steam_diagnostics`: ownership/depot plan, integrity, resume,
  commit and installer-owned numeric logging.
- `steam_artwork`: optional public CDN cover fetch/decode/cache, UI-owned textures.
- `steam_settings`, `steam_session`, `steam_file`: preferences, encrypted tokens
  and recoverable Horizon metadata replacement.
- `steam_ui` / `steam_library_ui`: account settings and native UI presentation.
  Launcher changes only adapt the existing header/navigation and catalog hooks.

No execution backend, renderer setting, DLL, launch argument, FEX/Box64/Wine,
DXVK/VKD3D/Mesa version or driver implementation was changed by this candidate.
Host sanitizer tests exercise simultaneous real localhost HTTPS requests,
connection reuse, strict TLS, bounded responses, out-of-order verified writing,
CDN authorization sharing, cancel/resume, disk-full and log migration/rotation.
UI screenshots use synthetic data/covers; they do not establish live device speed.
Update of completed games and dedicated Verify/Repair remain pending milestones.

1. Build original revision and package the NRO; preserve hashes and build logs.
2. Native auth proof: TLS verification, Guard, cancel, logout, safe diagnostics,
   then real Horizon hardware/account test before building library/downloader UI.
3. Small owned Windows title: authorized depots, interrupted download/resume,
   disk full, incorrect chunk, SD errors and cancellation.
4. Registration parity with manual Add Game; no runtime changes; compatibility
   labels never imply that successful download means successful launch.
5. Transactional update interruption/recovery and manifest Verify/Repair.

The connection preview artifact is in `../artifacts/steam-probe/`.
Do not enter Steam credentials into chat, build scripts or diagnostic reports.

## Hardware report and post-logon fix (2026-10-01)

User confirmed native QR rendering and real Steam mobile approval. The following
step failed with Error 1; this does not establish a stable authenticated session.
Source audit found that successful logon left EResult.OK (1) in the error variable,
masking subsequent transport errors. Success now clears that error. Logon rejection,
malformed response, identity mismatch and missing session each remain separate.
The CM dispatcher also rejected all non-protobuf notifications, although Steam
supports legacy messages after login. Bounded legacy notifications are now ignored
without satisfying any authentication job; legacy logon/Multi are rejected and
logged-off messages still terminate the session. Compressed Multi containing a
protobuf logon reply followed by a legacy notification is covered by a fixture.

Heartbeat follows SteamKit's out-of-game interval (response field 2). Diagnostic
v2 records failed_stage, cm_type and transport_error as integers only. Stage 6 means
the provider had reached authenticated ONLINE before failing; stages 4/5 distinguish
approval waiting and final CM logon. No payloads, account IDs or secrets are logged.
This source fix is not proof of the exact device-side cause. A new hardware check
is needed; no console time change is requested based on the previous generic text.


## Temporary LAN diagnostics (Steam provider build 2026.10.02-2)

Settings > Steam settings > LAN diagnostics (temporary) > Enable for this run
starts a separate read-only IPv4 HTTP server on the Switch address, port 8181.
Open the **complete** displayed URL, including `#` and its random access key, on
another device on the same trusted LAN. The key is generated with libnx CSRNG;
it is separate from Steam credentials, kept in memory and never logged. A browser
refresh keeps access. Disable is available on the console. This setting is off
by default and is not persisted across a full Autorun restart.

The page polls every two seconds, shows a numeric Steam transfer snapshot and
progress bar independently of SD log export, and lists bounded local logs.
Select `steam-download.log`, `autorun_runtime.log`, a game executable's `.log`,
`stdout.txt` or `stderr.txt`. Refresh list after starting a game. Each request
reads only the last 16 KiB; an incomplete/truncated boundary line is discarded.
Read errors appear as filesystem error numbers instead of a successful empty log.
Only regular files with restricted names in the active runtime `logs` directory
are accessible. Vaults, game files, arbitrary paths and symlinks are excluded.
No remote install, launch, settings changes, deletion or filesystem writes exist.
The server serializes clients with bounded requests/timeouts and one 128 KiB
native worker stack. It does not own SDL objects or Steam provider pointers.

Before the existing launcher-to-game socket reset, Autorun stops and joins this
optional worker; after networking returns the module rebinds the same port and
retains the same browser key. The browser may briefly disconnect; reload it if
needed. Game logs are read passively from files already written by Autorun.
At orderly game exit the worker is joined before runtime cleanup/reexecution.
A whole-process crash, closing Autorun or its automatic restart ends this server:
enable it again and open the new URL to inspect saved game logs. Sleep, Wi-Fi loss,
changed DHCP addresses and reading a log while Horizon has it open still require
hardware validation. A restart error is recorded numerically in `lan-service.log`
where possible; logging failure is reported to stderr. This is not a crash handler,
and unavailable/unflushed log data cannot be invented by the browser.

HTTP on the LAN is **unencrypted**. Use a trusted local network, no port forwarding,
and turn it off after debugging. The browser key itself is visible to a network
observer. Steam records are numeric allowlists and never contain account secrets.
Existing runtime/game logs additionally have conservative whole-line redaction
for passwords, Guard/2FA/authentication, tokens, cookies, sessions, tickets and URLs.
This can hide harmless diagnostic lines. Third-party game output is arbitrary:
a keyword filter cannot certify that every possible unlabeled secret has been
removed. Local paths and game names remain visible. Review game logs before
sharing them elsewhere; do not enter credentials into diagnostic test games.

### Narrow lifecycle integration, authorized by the owner

The owner requested that LAN diagnostics restart together with the socket service
and affect the execution backend minimally. `horizon-wine/source/runtime.c` adds
only the diagnostic header and these notifications:

- `main`: pause/resume around the already-existing `network_fast(0)` call;
  mark the UI handoff so the page labels its Steam snapshot as retained.
- `return_to_launcher`: stop/join before teardown starts.
- `leave_cleanly`: stop/join before sockets and loader resources are released.

The reset, cleanup, launch paths, arguments, environments, per-game settings,
Wine/FEX/Box64/GPU/DXVK/VKD3D implementations and versions remain as before.
All notifications are no-ops when the console has not enabled this module.
The native server lives in `source/diagnostics/lan_log.{c,h}` and can be removed with its
UI/CMake entry and these calls. No new third-party dependency or code was copied.

### Checked SD diagnostic export

Settings > Steam settings > Export diagnostics writes `logs/steam-report.log`
with build number, numeric account/library/storage states and the current transfer
snapshot. It flushes, syncs, closes and verifies an exact bounded readback before
showing success. Failure shows a filesystem error and stage. It classifies the
existing transfer log as missing/legacy v1/current v2/unknown/unreadable without
modifying installer-owned history. `available=0` means no transfer in memory;
export after a download/cancellation, before Play or restart. It never serializes
SteamID, credentials, file paths or raw history. Readback is not proof against
power loss or a guarantee of physical SD health.

Offline validation: `sh horizon-wine/tests/check-lan-log-host.sh` exercises real
loopback HTTP, access checks, redaction, invalid paths/symlinks, bounded tails,
service restart with the same key and orderly shutdown under ASan/UBSan. Native
SDL fixtures cover enabling LAN diagnostics and checked export. A real desktop
browser displayed synthetic progress and logs and retained access after reload.
These results are not Nintendo Switch hardware validation.

## Steam client runtime feasibility (2026-10-02)

GameNative is not limited to downloading games: the inspected pinned revision
`fa9dec8a4e293ad63b67623c5327f16d332b444a` documents a "real Steam" mode in
[`THIRD_PARTY_NOTICES`](https://github.com/Greencus/gamenative/blob/fa9dec8a4e293ad63b67623c5327f16d332b444a/THIRD_PARTY_NOTICES).
Its proprietary `libsteambootstrap.so` has withheld source and loads an Android
`libsteamclient.so` from the user's Steam container. It uses internal client exports
and vtable details. These binaries are not a libnx/Horizon dependency and no source
or binary has been incorporated here. Other GameNative paths replace Steam DLLs;
those are outside this project's explicit no-emulation/no-DRM-bypass requirements.
No claim is made about GameHub without inspecting a reliable implementation.

Valve's [Steamworks API documentation](https://partner.steamgames.com/doc/sdk/api)
requires the running Steam client to provide the game interfaces. Our downloader's
CM session is not that client. A tiny AppID file or environment setting cannot supply
its runtime services. A genuine client integration would require separate platform,
licensing and runtime work, beyond the unchanged execution backend boundary. This
candidate does not add a wrapper, replace `steam_api.dll`, pretend the client exists
or classify games as Playable merely because they downloaded. Existing compatibility
settings remain available. The supplied FSD bootstrap log requires VC++ prerequisites;
Catlateral's incomplete module-load trace does not establish a Steam-client failure.

### Small-file batch validation (2026.10.02-4)

Host ASan/UBSan fixtures exercise 40-file installs across the 32-file boundary with
concurrency 1/2/4, actual encrypted/compressed chunk decoding, full SD readback,
shared CDN authorization, correct executable discovery, interrupted reverified
resume and explicit cancellation cleanup. Mixed manifests check directory/empty
entries, a multi-chunk file, duplicate paths and return to the small-file queue.
A blocked-SD-sync fixture proves buffers for subsequent distinct files remain
available and the queue cannot grow beyond its configured slot count. Disk-full,
wrong file SHA-1, abort and symlink cases must never report unsynchronized files
as verified. Native UI and the normal manual-game Play handoff are tested separately.
These are synthetic host results, not a hardware throughput benchmark.

### Per-game language fallback (2026.10.02-5)

The appinfo visitor now reports a bounded bitset of languages from public Windows
base-game depots, excluding DLC, Linux-only and private-branch depots. Matching
trims surrounding whitespace and accepts ASCII case variations. Empty language
configuration means a neutral depot. If the preferred global language is missing,
Install opens a scrolling list of recognized available languages for that game.
The same choice is available with A after language error 3111 in Downloads, even
when the original selection left zero depots. Back does not enqueue installation.

An explicit "Default / base files" option appears only when eligible neutral
depots exist. It excludes language-specific depots and does not claim the game's
built-in language is English or guarantee those base files are sufficient. This
handles games whose default language is bundled with their common files without
silently combining unrelated translations. Unknown Steam language tags are not
invented or offered. App metadata describes availability, not entitlement: every
chosen depot still passes the original authenticated license/key/manifest workflow.

The override is captured when queuing, never alters global Steam settings, is
recorded in the installed game's existing language metadata, and survives a retry
of the current interrupted transfer. Selecting a different language cannot merge
an incompatible saved manifest plan (existing error 3110 and explicit Cancel apply).
The pre-install size is withheld when language selection is required; the chosen
plan's exact size/free-space check still occurs before downloading any game files.
No execution backend, Steam runtime/DRM behavior or third-party dependency changes.
Hardware validation is needed with the owner's previously failing games.

### Integrity repair and diagnostics (2026.10.02-6)

The owner reported persistent error 3103 with HTTP 200 and transport 0 at
mods/xpack/Settings/Profiles/Custom/ServerList.con, recurring after Retry. This
establishes an integrity-stage failure, not its exact cause. The app does not yet
have this user's manifest, so no physical-device root cause is asserted.

Two reproducible edge cases are handled: an empty mapping can omit sha_content or
supply an all-zero placeholder, which previously failed whole-file SHA-1 checking;
and valid manifest-verified chunks need not satisfy the compression wrapper CRC.
Empty placeholders require a zero-size regular SD file with no chunks, and still
reject an incorrect nonzero hash. Nonempty files require SHA-1 and exact chunk
coverage. Chunk processing follows the independently inspected SteamKit behavior
([DepotChunk.Process](https://github.com/SteamRE/SteamKit/blob/master/SteamKit2/SteamKit2/Steam/CDN/DepotChunk.cs)):
wrapper CRC is not authoritative; authenticated manifest Adler32, decoded length,
chunk SHA-1 and whole-file SHA-1/readback remain mandatory. No dependency/code copied.

Chunk response-length/decrypt/decompress/checksum/SHA failures get at most two
additional attempts per current file or bounded small-file window. Network buffers
are released and SD workers joined before retry; already valid staging is rechecked
and reused, progress is not counted twice, and CDN preference rotates when possible.
Permanent corruption stops with preserved staging. File readback/hash, manifest,
layout, memory and filesystem failures are not blindly retried. Explicit Cancel
still removes the AppID cache after workers stop, and installed games are preserved.

Downloads displays the integrity detail and format numerically and in plain text,
independent of SD logs. The same numeric fields appear in checked diagnostic export,
transfer history and optional LAN memory snapshot; no payload, keys, signed URL,
account identity or credentials are recorded. Format 1=ZIP, 2=VZip/LZMA, 3=VZstd.
Detail 1=response length, 2=decrypt, 3=decompress, 4=manifest Adler32,
5=chunk SHA-1, 6=file SHA-1/readback, 7=manifest, 8=layout, 9=size, 10=memory.
Existing execution backend remains unchanged; downloaded files are never marked
installed while any required verification fails.

### Game prerequisites — first device-validation stage (2026.10.02-7)

Steam > Installed > game > **X: Dependencies** opens an independent module,
`steam_prerequisites.{c,h}` and `steam_prerequisites_ui.{c,h}`. It inspects the
selected executable's normal/delay PE imports and adjacent DLLs (at most 32 PE
files and 256 directory entries), and reads bounded installscript hints. Steam
app metadata supplies `common/extended/installscript` or `extended/installscript`.
Scripts are advisory: their commands, registry operations and URLs are never
executed. Missing/partial discovery is **Unknown**, not proof no component is
needed. Transitive imports, other launch executables, nested plugins and every
Common Redistributables depot requirement are not yet exhaustively resolved.

The first installer catalog contains Microsoft Visual C++ v14 x86/x64 version
14.44.35211.0. It uses fixed direct Microsoft HTTPS URLs, certificate/hostname
verification, no redirects/proxy/netrc/cookies or Steam credentials, pinned byte
length and SHA-256, streaming 32 KiB buffers and disk readback before publishing.
The release catalog was checked on Windows with **Valid Microsoft Corporation
Authenticode signatures**; runtime validation uses the pinned hash, not a claim
that Horizon implements Authenticode. Microsoft installers are **not bundled**
in our distribution. Download/launch requires the user's explicit Install action
and acknowledgement that Microsoft license terms apply. Failed/cancelled downloads
remove `.part`, and are never marked installed. This small installer download
restarts after interruption; Steam game chunk resume is unaffected.

Cache/probes live under `switch/wine/drive_c/windows/AutorunPrerequisites`, away
from the scanned game catalog. They are shared by games using the existing Wine
environment, and deliberately survive uninstalling one Steam game. No credentials
are written here. Finish a Steam transfer/removal before launching a prerequisite.
Choose the architecture of the Windows executable (the screen shows it when known),
not ARM64. The module queues one transient ordinary executable through a launcher
adapter, then uses unchanged `describe_program`/`start_program`; no game entry is
created. Installer arguments are stored **only beside our owned installer**, with
`/install /passive /norestart /log`, never in a game's or global argument file.
Wine graphics for this transient program are selected in its own provider-owned
`.wine-nx.txt`; game renderer settings remain unchanged. The x64 Microsoft package
also has a 32-bit bootstrap EXE; its payload architecture is what the x64 button
means. The x64 read-only probe is a genuine 64-bit EXE.

After it finishes, reopen Autorun if necessary, then choose **Check Wine environment
x86/x64** in Dependencies. Packaged read-only Windows probes check the relevant
VC14 registry registration (both registry views) and matching-architecture
`vcruntime140.dll`/`msvcp140.dll` PE files, and write a result correlated with a
fresh native request. A cached download, stale result, installer exit or interrupted
check cannot produce a success status. The status says **Last check: registered +
DLL files**, not Playable. Recheck after registry reset, runtime reinstall or other
external changes; this is not a full API/load test or proof every imported VC14
symbol/version is available. Installation attempts invalidate the earlier result.

Installer logs: `switch/wine/drive_c/windows/AutorunPrerequisites/VC14-x86.log`
or `VC14-x64.log` (Microsoft may also write companion MSI logs in this directory).
Probe records: `probe-x86.result` / `probe-x64.result`; native messages use the
`[PREREQUISITES]` prefix in `autorun_runtime.log`. No arbitrary installscript text,
URLs, registry dumps or Steam secrets are copied to diagnostics.

**Hardware validation required before further automation.** The original Wine
process creation path uses child-process spawning (`dlls/ntdll/unix/process.c`),
while this Horizon runtime normally launches one foreground Windows application.
Microsoft bootstrap/MSI installers may need functionality unavailable on the
console. Compilation and successful PC download do not establish they install
on Switch. An incomplete installation remains missing/Unknown; we do not fake
registry success, replace the execution backend, extract DLLs to override games,
or change Wine/FEX/Box64/DXVK/VKD3D/Mesa. Existing installer/probe executables need
testing together on the real device before adding automatic install/check/play
sequencing. Legacy VC++, .NET, DirectX helper installation, OpenAL, PhysX and
custom/Unreal prerequisites are reported as hints where recognized, but are not
automatically installed in this stage. Actual Steam game compatibility is separate.

Protocol/installer documentation:
[Valve Common Redistributables](https://partner.steamgames.com/doc/features/common_redist),
[Valve InstallScripts](https://partner.steamgames.com/doc/sdk/installscripts),
[Microsoft supported VC downloads](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist),
[Microsoft installer/registry options](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files).
