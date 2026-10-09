# Dishonored: extended session log analysis

Collected after the user's gameplay test on native 2026.10.06-11. Autorun was
already closed when collection began; no new gameplay run was started. Full
redacted saved log: `D:/autorun fork/artifacts/dishonored-gameplay/Dishonored.log`
(494592 source bytes, no skipped prefix). Numeric per-window extraction:
`D:/autorun fork/artifacts/dishonored-gameplay/performance-windows.json`.
Temporary DBI FTP was stopped after reading; Switch remains on HOME.

## Findings

The retained log covers approximately 25 minutes. Counter-derived presentation
rates are approximately 17.43/s in progress seconds 600–1000, 11.81/s at
1100–1300, and 4.20/s at 1300–1535. These are presentation-counter deltas, not
controlled FPS benchmarks. Exact menu/gameplay transition and user input were
not recorded. The user independently confirms reaching gameplay and around
20 FPS in the menu.

Pipeline creation remains expensive late in the session:

| Recorded interval | Worker | Recorded calls | Total wall duration | Longest call |
|---|---|---:|---:|---:|
| Collector age 1429.9 s, window 63.0 s | `4c` / Wine 76 / `dxvk-shader-l` | 80 | 59.53 s | 3.16 s |
| Collector age 1493.3 s, window 63.4 s | `4c` / Wine 76 / `dxvk-shader-l` | 388 | 55.22 s | 1.68 s |
| Same interval | `48` / Wine 72 / `dxvk-cs` | 18 | 3.70 s | **3.53 s** |

The command-stream worker also enters a multi-second pipeline call. This is a
concrete critical-path candidate, rather than solely background compiler work.
Durations include conversion, driver work, waiting and descheduling; they do not
prove all this time is shader compilation. Do not sum concurrent workers.
Calls crossing collector-generation boundaries can be omitted.

Late `RenderingThread` CPU usage reaches approximately 86–92% of one core,
while total reported usage is around 1.4–1.7 cores. Sampled syscall rankings
remain dominated by `NtQueryPerformanceCounter`, at roughly 8–11 million
estimated calls per progress interval in several late samples. Existing
deterministic sampling biases exact counts; it does not measure the full guest
translation/WOW64/native transition cost.

Final runtime heap reports 442 MB free and section failures zero. This does not
prove all memory limits are absent, but the log does not establish immediate
heap exhaustion. No explicit unhandled native fault marker was found in this
saved run. Forced process closure can omit buffered final lines.

## Prioritized general improvements

1. **Separate cache lookup, deserialization, native compilation, and pipeline
   linking/waiting.** Record hit/miss and a bounded identity for repeated shader
   or pipeline keys. Determine whether expensive calls are unique new work,
   recompilation of identical work, slow disk-cache reads, or synchronization.
   Then improve bounded cache reuse and move avoidable work off `dxvk-cs`.
2. **Measure the rendering thread's timer/polling transition cost.** Native QPC
   alone is only part of the path. A fast common timer path or better readiness
   waiting could reduce CPU work, preserving monotonicity, units, timeout/APC
   behavior and required FEX TSO semantics. Do not alter clocks or sleeping
   behavior based only on call volume.
3. **Compare graphics pipeline library behavior for NVK/Maxwell.** The log
   advertises GPL and already uses three compiler threads. More threads is not
   an established fix. A controlled driver-capability profile comparison should
   preserve cache artifacts and correctness, without title-specific EXE patches.
4. **Check cache IO only after attributing time.** SD write/cache activity is
   substantial, but cumulative parallel IO counters do not establish it as the
   main stall. Batch metadata/writes or change queueing only when measured.

Local reference sources already contain both NVK memory cache and Mesa disk
cache paths (`nvk_device.c`, `nvk_physical_device.c`, `vk_pipeline_cache.c`). A
blanket claim that caching is disabled is unsupported. Native call timings do
not yet report which cache layer hits, and the reference checkout alone does
not prove every build option in the linked SDK archive.

This analysis identifies actionable common graphics/synchronization paths.
It does not establish a guaranteed 30 FPS target or quantify logging overhead.
No runtime, game settings, overclock configuration or shader cache was changed
during this collection.
