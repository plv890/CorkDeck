# Dishonored performance investigation — 2026-10-07 (Asia/Tbilisi)

This is an investigation checkpoint, not a performance fix or a 30 FPS claim.
The user requested investigation of performance losses using the existing remote
bench and telemetry. NEXT_CHAT_REMOTE_DEBUG.md was used as project context;
its historical authorization statements were not treated as new user requests.
Existing uncommitted changes were preserved. No AGENTS.md was found in the workspace.

## Established baseline

The existing -11 gameplay evidence reports a late shader worker spending
59.53 s in pipeline calls in a 63.0 s window, and a 3.53 s pipeline call on
dxvk-cs. RenderingThread reaches 86–92% of one core. These are overlapping native
wall durations, not compiler CPU time or full guest timer-transition cost.
See DISHONORED_GAMEPLAY_PERFORMANCE_20261006.md and the saved gameplay log.

## New source findings

The local Mesa reference HEAD matches the handoff pin:
`d4a00ea0ab3f59afb967cc5d779e4263d237bd77`.

1. `src/vulkan/runtime/vk_pipeline_cache.c:336`:
   `vk_pipeline_cache_lookup_object` sets `cache_hit=true` for a memory object
   lookup. A successful disk lookup/deserialization returns an object without
   setting that flag. `vk_pipeline.c` explicitly documents this distinction.
   **A false lookup `cache_hit` flag does not prove recompilation.** The caller
   in this pinned revision also checks whether the returned object is NULL;
   do not equate this internal flag with the final Vulkan feedback bit.
   Instrument memory hit, disk hit, disk miss and deserialize failure separately.
2. `src/nouveau/vulkan/nvk_device.c:497`: the internal device memory cache uses
   `.weak_ref=true`. `vk_pipeline_cache_object_unref` removes its entry when the
   last owning reference is released. This permits repeated disk deserialization
   after object destruction; it does not prove that Dishonored does this, or that
   its application cache has the same lifetime. Changing this to an unbounded
   strong cache is not justified by current memory evidence.
3. `src/vulkan/runtime/vk_pipeline.c:1835` calls `vk_compile_shaders` after stage
   reuse/cache lookup. `src/nouveau/vulkan/nvk_shader.c:1170` separates NIR
   lowering, NAK compilation, shader upload and push-data setup. Those are the
   concrete attribution points beneath the current aggregate Vulkan timer.
4. Mesa already provides disk hit/miss counters in `disk_cache_get` controlled
   by `MESA_SHADER_CACHE_SHOW_STATS`. They are printed at cache destruction,
   so forced application closure can lose output. They do not measure disk
   latency or identify repeated keys.
5. `build-unified.sh` enables shader cache for the target build and disables it
   for the separate host compiler build. These flags must not be confused.
   Reference build scripts alone do not verify the installed static SDK options.

No third-party code was copied. Sources are the pinned local Mesa checkout and
[official DXVK 3.1.1 README](https://github.com/doitsujin/dxvk/blob/v3.1.1/README.md).
The README provides compiler/cs/frametimes HUD counters and explains that GPL
compiles at shader load time. Unreal Engine may still load shaders immediately
before drawing. Waiting in a menu cannot establish gameplay warmup.
The installed GPLAsync DLL provenance/settings still need exact verification;
the log version string alone identifies v3.1.1, not the precise fork commit.

The runtime already supplies `DXVK_ASYNC=1` when GPLAsync is selected and its
payload is installed (`runtime.c:2278`). Turning on the same environment variable
again is not a new optimization. Exact fork behavior must be verified first.
The native QPC path already calls `horizon_interrupt_time()` at 10 MHz units
(`dlls/ntdll/unix/sync.c:102`); the unresolved cost is predominantly the full
guest/bridge path and polling attribution, not a demonstrated slow native clock.

## Remote experiment

Read-only probe `20261006T212338Z-102c0137` received sys-botbase 2.5 and title None.
One screenshot confirmed HOME, DBI selected and Autorun next. Autorun was launched
with Right/A; title `0548EABB35576000` and Dishonored selected were verified.
Play action `20261006T212437Z-638f2c9e` reached "Starting game...".
The first observer failed its initial title query with a timeout; a bounded
probe also timed out. No blind recovery input was sent. The user confirmed that
the game was launching/working and physical HOME responded. Probe
`20261006T212544Z-f8810565` subsequently received the Autorun title again.
Thus this was a temporary unavailable remote channel, not evidence of a crash.

A new 120 s log-only observer was started at
`tools/remote_switch/runs/20261006T212556Z-observe-5a006603`.
It completed with advancing snapshots: game second 190, 793 successful presents,
fatal=0. First observed presentation was around second 122. The user's subsequent
instruction reaffirmed known button/count routes and a 5–10 minute startup wait.
A second log-only observation continues in
`tools/remote_switch/runs/20261006T212826Z-observe-d2298de7`, allowing ten minutes
from launch before final visual acceptance. Game/build/settings/clocks/cache
were not changed for this experiment. This is a repeated-launch observation,
not a controlled first-pass/warm-pass gameplay A/B.

## Next discriminating experiment

Use fixed-size aggregated stage counters: cache-lock wait, memory lookup, disk
read, deserialize, NIR lowering, NAK compile and shader upload. Add bounded
hashed shader keys with outcome counts, not shader contents or per-call SD writes.
Keep the existing collector's generation/in-flight limitations explicit.
Use matching ELF/library build provenance; verify actual SDK build options first.

Compare the same gameplay segment twice with identical graphics and clocks;
retain caches. Collect frame-time distributions and dxvk-cs waits alongside
stage counters. A separate collector-on/off run measures diagnostic overhead.
If duplicate keys repeatedly deserialize, evaluate bounded retention/reuse.
If unique NAK work dominates, evaluate upstream compiler/library scheduling.
If warmed compiler activity drops but RenderingThread remains saturated, measure
full QPC guest/WOW64/native transition and caller polling before adding a fast path.
Keep FEX TSO=1 and preserve timer/APC semantics.

Runtime optimization, stage instrumentation, controlled gameplay A/B and
hardware overhead measurement remain unimplemented/unvalidated at this checkpoint.

## Completed fresh hardware result

Both log-only observations completed. Latest sample: game second 639,
5,077 successful presents, fatal=0, collector active. Remote reads resumed after
the startup interruption and snapshots advanced throughout the measured fragments.
No input was sent after Play by this investigation; any user input or automatic
scene transitions are not annotated, so intervals are not an identical-scene A/B.

Between game seconds 222–304, counter deltas range about 21.7–24.8 presents/s.
Between 426–568 they range about 0.55–0.95 presents/s. These rates use runtime
integer-second counters and are not frame-time percentiles or a controlled FPS
benchmark. A retained report at second 467 contains worker 0x4c with 15 pipeline
calls, 28.72 s cumulative wall duration and a **4.67463 s maximum call**.
This independently reproduces the multi-second pipeline-path lead from the
previous saved gameplay log. The measurement does not isolate NAK CPU time,
locks, descheduling, cache IO or linking, nor prove the cause of every slow frame.

One final screenshot, `20261006T213535Z-c2b81e80/screen.jpg`, confirms an actual
Dishonored gameplay scene after over ten minutes from launch. Its frame-time
graph displays max 3243.4 ms; this is a different window/metric from the pipeline
timer. The external overlay at that instant shows CPU activity and near-zero GPU
load, consistent with a CPU-side stall hypothesis but not a utilization benchmark.
The game remains running; no cache, settings, clocks or runtime files were changed.

Immediate implementation priority: bounded stage/key instrumentation in the
existing Mesa cache/compiler path, with matching SDK rebuild provenance and
collector overhead validation. A setting-only speedup has not been established.
