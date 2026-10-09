# Shared Steamworks integration — 2026-10-06

Follow-up open-source survey: [STEAMWORKS_OPEN_SOURCE_RESEARCH.md](STEAMWORKS_OPEN_SOURCE_RESEARCH.md).
It identifies a public authentic-client bridge and independent API implementations,
with concrete source locations and Horizon-specific adaptation requirements.

Autorun's authenticated CM session supplies downloads, not game-facing SDK
interfaces. Dishonored dereferences NULL SteamRemoteStorage after rendering its
splash in the controlled TSO=1 run. Balatro on native -8 passes its earlier
filesystem failure, initializes OpenGL, then cannot connect to Steam and exits
with code 0. Neither game is currently validated playable.

## GameNative reference

Local reference ../references/gamenative is fa9dec8 (2026-06-17).
Its SteamBootstrap.kt initializes Android libsteamclient.so in-process via JNI.
BionicSteamAssetsDependency supplies that client and architecture-specific
Proton lsteamclient PE/Unix components. ActiveProcess registry settings are
part of the complete environment, not a standalone fix.

Current upstream inspected on 2026-10-06 differs: a separate bootstrap host is
started per AppID, with a 60-second READY/FAILED/CLOSED wait and session teardown.
Do not describe the local JNI version as current upstream.

Primary sources:
- https://github.com/utkarshdalal/GameNative/blob/master/app/src/main/java/app/gamenative/SteamBootstrap.kt
- https://github.com/utkarshdalal/GameNative/blob/master/app/src/main/java/app/gamenative/utils/launchdependencies/BionicSteamAssetsDependency.kt
- https://github.com/utkarshdalal/GameNative/blob/master/THIRD_PARTY_NOTICES
- https://github.com/ValveSoftware/Proton/tree/proton_10.0/lsteamclient
- https://partner.steamgames.com/doc/sdk/api
- https://partner.steamgames.com/doc/api/ISteamRemoteStorage

GameNative's application is GPL-3.0; bootstrap is separately marked proprietary
with native source intentionally absent. No code or binary was copied.
Android ARM64 libraries are not Horizon libraries. Our switch-shims/dlfcn.h
returns NULL for dlopen/dlsym; no Android/Bionic native loader or equivalent host
process exists. Proton lsteamclient translates interfaces but requires a client
backend. Copying its DLL or registry keys does not create that backend.

## First implementation gate

Evaluate a Horizon-native backend and versioned PE bridge using Proton's ABI
conversion as a reference. This subsystem is not implemented by the advisory.
Original client hosting is an alternative only after process/native ABI support.
An owned x86/x64 test client should prove these before real-game integration:

1. AppID/account/launch-generation binding; no stale session after game/account
   changes. Download authentication alone cannot mark the game SDK ready.
2. Pipe/user lifecycle, exact interface versions, x86 thiscall/x64 ABI, pointer
   widths, structure packing and buffer ownership.
3. Truthful init failure with a specific diagnostic if backend/interfaces are
   absent. No fabricated initialization or blanket subscription results.
4. Callback registration/removal, correct dispatch thread, async result sizes,
   shutdown cancellation; no loader-lock deadlock or frame-blocking network I/O.
5. RemoteStorage local reads/writes/enumeration/deletion, per-app path isolation,
   quotas, atomic durability and restart recovery. Distinguish local save,
   queued upload and actual cloud acknowledgment; reconcile offline changes.
6. Coherent User/Apps/Friends/Utils context, real ownership/tickets where needed;
   unsupported services remain explicit rather than reporting false success.
7. Preserve original game DLLs and reversible configuration. A probe validates
   exercised interfaces, not universal game compatibility.

Use PC fixtures first for ABI, callback lifetime and save transaction failures.
Device tests follow an actual working bridge/backend and target failing games.
Retest the working Ninja baseline only after major integration changes.

## General loader correction — source only

Balatro -8 also logged alloc_tls_slot's NtQueryInformationThread warning.
Horizon enumerated terminated objects retained by handles; their queried TEB
is zero. NtGetNextThread now skips them while retaining handle queries/cursors.
Extracted real handlers and Wine TLS allocator pass ASan/UBSan: forward/reverse
ordering, terminated cursor continuation, rights/lifetime and dynamic TLS, with
zero TLS query errors. This is a shared loader correction, not a Steamworks fix.
Not deployed: installed native remains 2026.10.06-8.
