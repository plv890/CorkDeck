# General compatibility investigation — 8 October 2026 (Asia/Tbilisi)

## Verified findings

The Forest is used as a stress case, not as a commitment to make this game
playable. The user explicitly narrowed that goal during this investigation.

The new run displayed **TheForest Configuration**, with Play!/Quit. The user
pressed Play and saw the studio splash before the application exited. The
retained log confirms D3D11 through ordinary DXVK 3.1.1 and Vulkan, 287 successful
presents, followed by Unity's `DynamicHeapAllocator out of memory` messages.
This is distinct from the OpenGL compositor which displays the configuration
window. Zero graphics presents alone cannot diagnose a stopped renderer.

Last pre-crash heap report: used 3043 MiB, free 46 MiB. Unity's own error.log
reports 98% memory use, 3189 MiB total and 33 MiB free. Native physical backing
allocations failed with ENOMEM; the final guest exception reads address 0x10.
The evidence supports memory exhaustion preceding this crash. It does not
prove the exact memory owner or that freeing caches will make the game fit.
The older 660-second wait-only run did not capture this configuration window;
its precise wake dependency remains unproven.

Evidence under `D:/autorun fork/artifacts/forest-steam-20261008/`:
`trace2-TheForest.log`, `trace2-autorun_runtime.log`, `crash-error.log`,
`collection.json`, `installation-2.json`, `build-2.json`, and matching
`runtime-requests-2.elf`. Configuration screenshot:
`autorun/tools/remote_switch/runs/20261007T215755Z-62d155b3/screen.jpg`.

## Global changes

- Bounded pending request records connect client Wine TID/request name, opcode,
  select parameters and server handler state. Generation tokens reject stale
  completions, overflow is counted, and terminated-thread records are cleared.
  Existing `wait-diagnostics.txt` controls collection. No guest timeout or
  server reply is replaced; no thread is paused. This is not a full wake-owner
  graph or a guest stack debugger. Its performance overhead is not benchmarked.
- Failed Horizon commits restore Wine protection/commit metadata and restore
  previous host protection before returning failure. ENOMEM is reported as
  STATUS_NO_MEMORY rather than STATUS_ACCESS_DENIED. Allocation logs no longer
  automatically describe every such error as virtual address exhaustion.
- Physical backing allocation failure can reclaim empty pool arenas, clean SD
  cache copies and copied shader RAM entries, then retry once. Cache callbacks
  use try-lock; no live guest/GPU data is evicted, persistent shader storage is
  preserved, and insufficient memory still returns a genuine error.
- `window_frames` includes the compositor counter, separately from successful
  game Vulkan/OpenGL presents. Cursor and window updates are still not proof
  of playable gameplay.

Host validation: source-extracted production protection helpers exercised
ENOMEM/EACCES, metadata rollback and successful retry for small/large ranges;
shader tests exercised LRU reclaim, survival of caller-owned copies and
concurrency; request tests exercised capacity, stale tokens and concurrency.
All passed ASan/UBSan. Native build succeeded with cumulative patched Mesa SDK.
Hardware OOM recovery by these new changes has not been proven; there is no
new claim that The Forest is playable or that diagnostic overhead is zero.

An initial diagnostic build copied the full request table onto the 32-KiB
stall-watch stack and faulted. The table is now copied one record at a time.
The fault belongs to that diagnostic build, not to The Forest. A transfer was
also interrupted before activation; the old active NRO remained intact and
the subsequent install used a new stage and verified readback.

## Steam compatibility boundary

The existing local SDK already provides versioned factories, callbacks and
account/AppID-bound local RemoteStorage. New genuine game-facing API subsets
can extend this layer globally. No Steam SDK payload changed in this task.

Assassin's Creed's retained wrapper log dynamically loads genuine legacy
Steam.dll from the game/parent paths; all attempts return c0000135 before
the 2:0000065558 dialog. It is not another RemoteStorage missing-interface case.
The wrapper is preserved and not bypassed. Local GBE legacy code was inspected;
its WinVerifyTrust success hook is not an acceptable replacement for a genuine
client path and was not integrated.

Valve Proton's client bridge requires an actual Steam client environment.
The local Termux reference depends on glibc/Bionic, X11, a native Valve client
and Hangover-specific integration, not Horizon. Current Autorun execv/execvp
return ENOSYS and its server has no new-process handler. A complete client/IPC
path is therefore still an implementation dependency, not something already
provided by the local SDK. No claim of a fixed Assassin's Creed launch is made.

Primary sources reviewed:
https://docs.unity3d.com/560/Documentation/Manual/CommandLineArguments.html
https://github.com/doitsujin/dxvk/blob/v3.1.1/dxvk.conf
https://github.com/ValveSoftware/Proton/blob/proton_11.0/lsteamclient/steamclient_main.c
https://partner.steamgames.com/doc/features/drm

## Installed checkpoint

Final candidate is native **2026.10.08-3**. Exact final build hashes and install
readback are in `artifacts/forest-steam-20261008/build.json` and
`installation.json`. NRO and manifest are replaced together; original configs,
Steam vault, saves, games, SDK files, clocks and rendering profiles are retained.
GDB stays disabled. Backups use `.before-memory-fixes-20261008`; the earlier
known -8 backup `.before-request-trace-20261008` also remains available.
Read final installation/acceptance journal before treating the candidate as
installed or assuming a current console screen.

Final readback and `acceptance.json` confirm installation, launcher launch/remote
close, and return to DBI FTP Browse SD on 192.168.70.59:5000. The owned incomplete
stage from the interrupted first transfer was removed; backups remain.
