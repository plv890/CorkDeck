# Compatibility work checkpoint — 2026-10-06, Asia/Tbilisi

## Latest checkpoint — native -8 and shared Steamworks investigation

Installed `2026.10.06-8`, SHA256
`afdbc42f26d7ee742cca4054b25395f0094bbffec9d29355415c211962c0eee0`,
40,543,288 bytes; installation journal ../artifacts/compatibility-profile-api/installation.json.
Read-only Windows primary-token identity and default ProfilesDirectory now let
Balatro pass its earlier LÖVE filesystem initialization error and initialize
OpenGL. Full closed log: tools/remote_switch/runs/20261006T110946Z-9ce03f22/Balatro.log.
It then reports inability to connect to Steam and exits with code 0. This is
progress past one general API defect, not playable-game acceptance.

GameNative local/upstream comparison and the common game-facing backend/ABI/
callback/storage implementation gates are in STEAMWORKS_COMPATIBILITY.md.
Source-only follow-up: NtGetNextThread excludes terminated retained objects,
avoiding bogus TEB queries during DLL TLS allocation. Source-extracted handler
and real TLS allocator ASan/UBSan fixtures pass. Not built/deployed yet.
Ninja was not retested after this minor update, per user priority. DBI FTP was
stopped after closed-log collection (20261006T111319Z-771af741).

## Earlier checkpoint — 14:24 local

Installed **2026.10.06-7**, 40,543,288 bytes, SHA256 `73086da595a67b8a3b20c5645643fc44d7c986ed6cc62bff1b74ab4ac39dc1bb`, guarded journal/backups `../artifacts/compatibility-wait-semantics/installation.json`. FTP stopped after verified update. Native build, Steam host suite, LAN security/redaction suite, seven context/FEX/fatal source fixtures passed. New wait regression was first reproduced failing locally, then passed after fix: Resume must preserve original timeout/event status, modified contexts and APC payload. Client kernel-APC retries retain updated snapshots. No per-game binary edits.

- -6 Catlateral confirmed retest `20261006T095907Z-observe-8193f25c`: zero presents over 240 seconds; full closed log `20261006T100401Z-bb095e33/CatlateralDamage.log` has no prior GC/GetThreadContext error but stalls with all 52 threads at 0% and frozen syscall counters. Root correctness improved but game is not fixed. That source additionally had an ordinary-wait resume semantic bug proven by a failing local test; -7 addresses it. -7 Catlateral retest `20261006T102403Z-observe-91024a2e` pending at this checkpoint.
- -7 Ninja confirmed from carousel fourth card, `20261006T101938Z-observe-62fcfc1f`: 4002 successful presents at 119 seconds, fatal=0. Screenshot title `20261006T102107Z-31e4f95f`; Xbox A input `20261006T102126Z-513cc51d` transitions to hub screenshot `20261006T102204Z-5a15b6ed` (player visible, HUD 35.7 FPS). Transition screenshot HUD 60.3 FPS is not a gameplay average. Previous hub was ~60 FPS; current samples vary and an isolated TSO/performance comparison remains pending. Startup/input regression check passed, full game compatibility unproven.
- Diagnostic metadata bug reproduced with exact run ID `000001387082fa7b`: broad sensitive-word filter mistook embedded `2fa` for a credential and redacted snapshot freshness. -7 exempts only the precisely formatted 16-digit hex run field from that one keyword; password/credential/2fa payload elsewhere remains filtered. Positive exact-ID and malformed/secret negative cases pass; Ninja observation freshness works again.

Earlier checkpoints below are historical. Their "installed" revisions and "pending" states do not override the current checkpoint.

## Latest evidence — 03:40 local

### Resume checkpoint — 2026-10-06, 13:55 Asia/Tbilisi

After the user's request to continue, console was on Nintendo HOME with no active title. This is separated from the 03:42 controlled Catlateral observation; subsequent runs in the time gap are not attributed to that observer. Saved-log collection `20261006T095029Z-fe5853f4` contains the decisive -5 Catlateral error: `server get target=0000049c machine=014c tid=8 system=0 started=1 suspended=1 valid=1 snapshots=0 ... status=c00000bb`. The thread is initialized and parked but the server received ZERO context snapshots. Ordinary `server_wait` -> `server_select` supplied NULL context; Horizon parked such a request and marked it valid with count zero. This is a general suspension-during-wait bug, not missing Unity DLLs. Live observation (stage 5, no presents, fatal=0) did not retain the GC dialog; the saved log explicitly shows it. Fatal=0 alone never establishes no failure.

General source fix: ordinary `server_select` on Horizon ARM64 captures a genuine safe-point native context and serializes its actual WOW64 CPU area before entering the server; explicit suspend contexts bypass capture; native-only/uninitialized guest threads do not receive synthesized x86 state. Reply changes restore guest CPU registers, and changed native state uses context continuation. Source-extracted ASan/UBSan fixture `check_horizon_wait_context.py` verifies the wrapper and helper control flow (host converters/capture are mocks); existing server fixture verifies target-owned register roundtrip/nested suspension. These host fixtures cannot prove dynarec timing on hardware. Native -6 built successfully; journal `../artifacts/compatibility-wait-context/installation.json` controls actual deployment status, hardware acceptance pending. SHA256 `3a3092ef1e5df821c3c0b638db48007f2b4ab6b7d0358c5bc4fef66d920d6920`, 40,543,288 bytes. -5 startup and automatic Steam login were physically verified by `20261005T234101Z-99667b1a`.

Latest closed Dishonored snapshot (different run `0000009441aea431`) retains C0000005 as fatal_kind=7 with 577 presents, confirming the new NT-exit diagnostic survives quiet logs and process exit. Its operator/controlled provenance during the time gap is unknown; do not present its FPS or timings as the earlier isolated TSO A/B. Newly present Balatro saved log shows LÖVE filesystem initialization failure; installation success is distinct from launch compatibility. This points to an additional shared filesystem/API investigation, pending reproduction.

Dishonored controlled comparison on native -4 changed ONLY FEX_TSOENABLED 0 -> 1. Observation `20261005T231640Z-observe-f1caf014`, log `tools/remote_switch/runs/20261005T232816Z-b6712213/Dishonored.log`. TSO=1 confirmed in startup. The previous RenderingThread fault disappeared in this run: first successful presents at ~121 seconds, visible title splash (screenshot `20261005T231947Z-6b717ab8`, HUD 19 FPS), 461 presents before a DIFFERENT C0000005 at ~205 seconds. This is progress, not playable gameplay or a measured 30 FPS result. Initial diagnostics recovered after ~86 seconds; three timeouts must not be counted as a crash. No graphics configuration changed. Keep TSO=1; original adjacent profile is backed up as `.before-tso-test-20261006`.

The later fault is now localized: log `[BOX64EXIT] eip=00a0907e ... ecx=00000000`; EXE disassembly at 0xa09078 loads global `[0x145b15c]`, then 0xa0907e dereferences `[ecx]`. Global initialization at 0x9ac249 calls import `[0xf947f0]` and stores EAX to that global. PE steam_api.dll IAT starts at RVA 0xb947ac; offset `(0xf947f0 - 0x400000 - 0xb947ac)/4 = 17`, whose import is **SteamRemoteStorage**. Therefore the immediate crash is an unchecked NULL Steamworks RemoteStorage interface, not the PhysX component that happened to load shortly before it. PhysX attribution was only an earlier timing hypothesis. Local artifacts `null-object-references.txt` and `pe-imports.txt` support this; licensed EXE was not executed on PC. At this exit heap_free~1236 MB and section_failures=0; insufficient GPU/CPU performance has not been established as its cause.

General environment gap: authenticated Autorun CM/download sessions do not supply the Windows Steam client IPC/interface lifecycle expected by the game's original steam_api.dll. Valve prerequisites: https://partner.steamgames.com/doc/sdk/api ; API: https://partner.steamgames.com/doc/api/ISteamRemoteStorage . A shared integration needs process/user/AppID context, API initialization, interface-version matching, callbacks, local/cloud save semantics, and truthful ownership checks. First evaluate an authenticated original client through Wine after child-process support is established. If using a native bridge, implement/version/test interfaces and save durability coherently; downloading more VC++ or changing resolution will not make a NULL RemoteStorage interface valid. No EXE edits, replacement steam_api DLLs or fabricated successful API init were applied.

New general dependency inspection detects exact steam_api.dll / steam_api64.dll imports, including delayed/adjacent PE imports, and shows an advisory Steamworks environment requirement. It does not block every such game or claim that import presence proves a mandatory failure. New ASan/UBSan PE tests cover x86 mixed case, x64 delay imports and rejection of a similarly named backup file; existing Steam host suite passed.

Native **2026.10.06-5** built successfully, 40,543,288 bytes, SHA256 `8e0a7a6e9d16e2f75bcfa86ebf3a2756e33dc4737463dcb08531220928f75aa8`. Journal `../artifacts/compatibility-safe-default/installation.json` is authoritative for install state; hardware acceptance still pending at this checkpoint. Adds bounded context capture/lifetime/error metadata, FEX TSO=1 default (explicit saved override still honored), retained fatal NT process exits (kind 7), and Steamworks advisory above. All six current context/FEX/fatal fixtures passed; shared CPU context fixture is now fixed and PASSED. PC observer/navigation: 9 tests passed. Older paragraphs below describe earlier checkpoints and must not override this update.

User priority: broad game startup compatibility first, downloader speed second. General runtime/API/environment improvements; no bespoke binary patches. Dishonored separately deserves an attempt if resource evidence makes playability plausible. User explicitly permits local PC reproduction only for cases that can meaningfully accelerate diagnosis.

## Installed and pending code

Installed 2026.10.06-4, NRO SHA256 `ffb8a6f0cd7b7f4592f631f8947e3f212b95dff1cd6474232574a7655a027079`, 40,539,192 bytes. Journal/backups under `../artifacts/compatibility-shared-runtime/installation.json`. Changes: remote WOW64 get/set contexts use server snapshots with access checks; native suspension serializes guest CPU area; missing native/system CPU area guarded; quiet sampled context errors now reach live memory diagnostics. Device-bound Steam auto-login survives updates. This does NOT establish that Unity/Mono is fixed.

Unbuilt newer source in `dlls/ntdll/unix/horizon.c`: two-second bounded context capture wait, retained target object reference across wait, extra failure fields (tid/system/started/suspended/valid/machines). Local ASan/UBSan tests passed. Do not claim this source is installed; header still -4 pending a new revision/build/deployment. `check_horizon_suspend.py` tests ready/unready snapshots, i386 snapshot roundtrip, nested suspend/resume, capture timeout, handle lifetime during wait; `check_horizon_trace.py` tests quiet failure retention/sampling without SD reopen; `check_wow64_thread_context.py` tests access rights, self aliases, remote target state/errors, CPU area selection/absence. Fixed host continuation fixture ARM64 type selection and ASan non-PIE flags. `check_shared_cpu_context.py` currently fails because its host fixture does not expose ARM64 unwind helpers; not a native runtime failure, not reported passed.

Observer now has bounded read-only recovery (default 120 seconds) and `--no-screenshots`; host recovery tests passed. These prevent premature termination after three temporary initialization timeouts and avoid screenshot traffic during GPU setup. No input is sent by observer.

## Hardware facts

- 2026.10.06-2 Ninja reached a playable hub at about 60 FPS, fatal=0; input accepted. Observation `20261005T222210Z-observe-9ef2ee16` was initially mislabeled Catlateral. Corrected record to Ninja based on screenshot; never count it as Unity evidence.
- Catlateral confirmed card, -2: `20261005T222658Z-observe-5e23b62a`, fatal GC/GetThreadContext at ~10 seconds, zero successful presents.
- Catlateral confirmed card, -4: `20261005T224552Z-observe-5c081fc0`, same GC error at 10 seconds. Now exact live code: `[WOW64 CONTEXT] get target=0x49c flags=00010003 status=c00000bb` (STATUS_NOT_SUPPORTED). Actual unsupported server reason is not yet distinguished; pending richer metadata will separate unstarted target, absent/wrong architecture snapshot and capture timeout. Do not substitute invented registers.
- Dishonored RHCP, -4, unchanged DXVK profile: `20261005T225155Z-observe-060f6279`. Early observer stopped at 24 seconds on three HTTP timeouts; this is remote-unavailable/Unknown, not crash proof. Remote channels later recovered. Screenshot `20261005T230152Z-a39a90f7` shows an in-game Rendering thread exception (not Atmosphere). Same addresses as old run: 0x5486a5, 0x5488af, 0x41680a. Fresh live read `20261005T230213Z-9dcaa512` shows fatal_kind=2, zero successful graphics presents, heap_used~714 MB and heap_free~2376 MB, section_failures=0. Old run also had >2 GiB free at failure. Thus startup failure is not evidenced as raw RAM exhaustion; 30 FPS gameplay remains unmeasured.

## Dishonored local analysis and active A/B

Private artifacts `../artifacts/dishonored-analysis`:
- Licensed executable copied ONLY for offline disassembly, never executed on PC, SHA256 `66443f3d68a6c658b0ed943260c3eb2f4cc9e3400d5809a94d6ec41eb725e17e`, 18,041,856 bytes.
- `crash-stack-disassembly.txt`: 0x5486a0 loads a render command from queue output; 0x5486a3 loads its vtable; fault 0x5486a5 reads `[edx+4]` for virtual dispatch. This motivates a publication/memory-ordering/data-corruption hypothesis, not proof of its cause.
- Saved `0-Config-DishonoredEngine.ini` includes [SystemSettings] 1920x1080, effects/shadows/high textures; editor subsection 1280x720 is not gameplay settings. Native physical resolution not yet confirmed. Do not claim previous tests were minimal.
- UE3 `Launch.log` was absent at all three bounded candidate roots. Config exists at `/switch/wine/drive_c/users/steamuser/Documents/My Games/Dishonored/DishonoredGame/Config/DishonoredEngine.ini`. No engine config changed yet.
- Installed public DLL manifest: 1607 files, Wine 11.18, arm64x, source b698a1ec6613acca9b87736e37751384eabbb6a8. Saved `compatibility-shared-runtime/dll-manifest.before.json`; DLL payload was not updated.
- Actual old runtime log: `[FEX] TSO=0 multiblock=1 maxinst=1000`, selected libwow64fex.dll. Both FEX TSO and Box64 strong-memory defaults are off in source. Configured CPU defaults FEX; this test is FEX.

Active isolated comparison changes ONLY `FEX_TSOENABLED=1` in `/switch/wine/drive_c/Steam/Dishonored RHCP/Binaries/Win32/Dishonored.wine-nx.txt`. Exact original backup `.before-tso-test-20261006`; local `profile.before.txt`, `profile.tso.txt`, `tso-test.json`. Readbacks/SHA verified. Same runtime -4, DXVK+VKD3D profile and all graphics settings unchanged. Next: confirm selected Dishonored card, launch with new DPAPI key and observer `--no-screenshots --recovery-seconds 120`, check `[FEX] TSO=1` from current startup. If rendering works, investigate which memory modes matter and evaluate a shared safe default on multiple games; do not attribute improvement to resolution because resolution is unchanged. If unchanged, restore TSO baseline before a distinct low-resolution or WineD3D comparison. Keep backups and restore ineffective settings.

## Remote state/routes

FTP was STOPPED and port 5000 refused after latest profile edit. Autorun was relaunched and temporary LAN key screen opened; screenshot `tools/remote_switch/runs/dishonored-tso-key-private.jpg` needs private inspection/key DPAPI saving. No game launched yet for this TSO comparison.

Home carousel is sorted by last-play timestamp: ordering changes after every attempt, including failure. The game can be off-screen left; it was not deleted. Confirm starting/final selected card before A. Steam settings signed-in Unlock row is disabled and skipped: from Sign in, Down x6 reaches LAN diagnostics (not x7). From its URL modal: A, B x3 returns Home (Steam settings -> right Settings -> left section list -> Home), then wait 2 seconds before arrows. Do not combine a guessed game launch with this return.

DBI fully initialized main menu first row: Down x12 A Down x2 A = FTP Browse SD; B stops. Initial network-less boot menu has fewer rows; wait for complete menu before fixed route. Close active title: HOME primed, X A, then wait >=2 seconds. Closing DBI leaves DBI FIRST on Nintendo HOME, Autorun SECOND: Right A launches Autorun. Left from DBI was incorrect. Native title guards apply before each press. Always detach/release; capture route start/end only unless mismatch.

User has already explicitly approved temporary DBI FTP and fully automatic Steam sign-in. No repeated permission question needed for same authorized update/read task. No background FTP, no firmware/kernel/sysmodule changes. Private screenshots may contain Steam IDs, keys or Wi-Fi passwords; do not publish them. No subagents authorized.
