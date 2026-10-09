# CPU scheduling and parallelism — 2026-10-07

<!-- migration-checkpoint-20261007 -->

Актуальное состояние и итоговые результаты: [пакет передачи следующему чату](docs/handoff/2026-10-07/README.md).
Этот документ сохраняет последовательность этапа; его прежние current/HOME/policy/GDB-pending формулировки относятся к моменту записи и не переопределяют последний checkpoint.


Follow-up implementation and target results now exist in
[CPU_SCHEDULING_EXPERIMENT_20261007.md](CPU_SCHEDULING_EXPERIMENT_20261007.md).
The pending-policy statements below describe this document's initial research
checkpoint, before the user's subsequent implementation authorization.

## Conclusion

Better placement of independent threads is feasible using existing libnx APIs.
It is partially implemented and active already. There is not yet evidence that
placement is the principal Dishonored bottleneck. A saturated rendering thread
and long native pipeline calls require separate investigation. Splitting all
Windows API execution away from game execution is not a simple affinity change.
No runtime settings, binaries, sysmodules or kernel configuration were changed
for this research.

## What this project already does

- `dlls/ntdll/unix/horizon.c:horizon_pin_current_thread` initially pins Wine
  threads round-robin, unless the program supplies an affinity mask.
- `horizon-wine/source/thread_profile.c:wine_nx_thread_balance` samples thread
  CPU ticks and balances movable Wine threads over permitted application cores
  0–2 roughly every two seconds via the runtime flusher. Explicit application
  affinity is preserved. Server connection CPU cost counts against its client;
  the connection follows the client's affinity on subsequent requests.
- `nx_thread_graphics_worker` recognizes dxvk-cs, wined3d_cs and vkd3d_queue.
  When process capabilities permit, those workers are placed on core 3 with
  priority 63. The compositor also opts into this path. Other thread mask calls
  are wrapped to exclude core 3. Shader/compiler/cache threads are not covered
  by this graphics-worker allowlist.
- The saved Dishonored log confirms `[CORES] 72 dxvk-cs: core 3, priority 63`
  and 26 `[BALANCE]` actions. Thus four-core graphics support and balancing
  are not missing features to implement again.

Saved gameplay log `artifacts/dishonored-gameplay/Dishonored.log`: 143 thread
reports, median 1.92 and maximum 2.38 core equivalents among registered threads.
RenderingThread is at least 80% of one core in 86 of 135 reports containing it.
This is not total system utilization: native/unregistered threads and other
processes are not fully represented. Counts describe the retained session,
not a controlled same-scene benchmark.

## Why "game on one core, Windows on another" is limited

The guest thread's translated x86 code, synchronous Wine API calls and native
library calls usually share one executing host thread and its TLS/stack/context.
Setting affinity moves that whole thread; it does not run its dependent stages
simultaneously. Moving a synchronous call to another worker requires dispatch,
result waiting, pointer/lifetime ownership, exception and APC handling, and
correct ordering. Frequent short calls such as QPC would gain another handoff.
This would likely add cost unless there is measured independent work to overlap.

Existing independent game pool/render/audio/IO threads, DXVK compiler/cache/
submit workers and Wine server connections can be placed independently.
Shader jobs for different keys can execute concurrently. One dependent shader
compile or rendering-thread instruction stream does not become parallel merely
because another core is idle. Code restructuring inside the appropriate
compiler/runtime/driver may create parallel jobs, but needs dependency evidence.

## Important limitations found in current diagnostics/policy

1. `[THREADS] @core` comes from `svcGetThreadCoreMask`'s preferred core, not a
   measured history of actual residency. For singleton affinity it identifies
   the permitted core; for a multi-core mask it is not actual occupancy.
2. Reports list only the twelve busiest registered threads above a threshold.
   There is no complete runnable/blocked timeline, queue latency, per-core idle
   time, priority report or lock-owner attribution.
3. Balancing is a greedy placement based on recent thread CPU consumption,
   not real runnable demand. It excludes helpers on core 3 and most native
   background workers, and cannot account for system/overlay competition.
   Its before/after load is a prediction, not measured speedup after a move.
4. Pinning to singleton cores restricts the kernel's ability to migrate work
   during short bursts; recent averages can lag scene transitions. Existing
   thresholds reduce unnecessary moves but do not prove optimal scheduling.
5. Core 3 also services system/background work. Priority 63 intentionally
   gives the graphics helper little precedence. Putting a dependency-critical
   dxvk-cs there may add scheduling delay. This is a hypothesis, not a proven
   explanation: the largest fresh pipeline call was on shader worker 0x4c,
   not dxvk-cs. Moving dxvk-cs alone may leave NAK/cache costs unchanged.

## Ready-made mechanisms and tools

- [libnx SVC wrappers](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/kernel/svc.h):
  core/priority masks, thread affinity/priority and thread CPU tick queries.
  These are sufficient for an in-process placement experiment. No additional
  sysmodule is needed. Query actual process capabilities and check results.
- [libnx pthread implementation](https://github.com/switchbrew/libnx/blob/master/nx/source/runtime/newlib.c):
  upstream pthreads receive permitted affinity; our wrapper/pinning restricts
  this. Reusing the OS scheduler with broader masks is a valid A/B candidate.
- [Atmosphere scheduler source](https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libmesosphere/source/kern_k_scheduler.cpp):
  priority/affinity queues and migration already exist. This research does not
  establish the console's exact Atmosphere revision. A kernel replacement is
  unnecessary to test our runtime policy.
- [Status Monitor modes](https://github.com/masagrator/Status-Monitor-Overlay/blob/master/docs/modes.md):
  per-core CPU load from idle ticks and GPU/RAM observations. Useful external
  corroboration; it does not attribute Wine/FEX call cost or schedule jobs.
- [sys-clk](https://github.com/retronx-team/sys-clk): clock management and
  supporting clock telemetry, not automatic parallelization. Keep clocks fixed
  during scheduling comparisons. No installation was performed.

Primary upstream source was consulted; no code was copied. Exact versions and
licenses must be pinned if any source is later imported. Existing process
capabilities and working forwarder remain the starting point.

## Smallest meaningful implementation/experiment

First extend existing fixed-size thread reporting (not per-call text logging):
effective affinity mask, preferred core, priority, thread CPU-time delta,
helper/fixed status, placement-policy ID and moves. Report every 5–10 seconds.
Add native worker registration where missing. Keep IDs meaningful across a run
without assuming thread numbers stay the same across launches.

At pipeline/cache/submit queues aggregate enqueue-to-start latency, wall and
thread CPU duration, queue depth, readiness wait and cache/compiler stage
durations. Wall-minus-CPU indicates off-CPU time; it cannot distinguish blocked
time from descheduling without additional wait instrumentation. Use per-core
idle data only through supported API access; libnx limits IdleTickCount to the
querying thread's core, so one query cannot safely be treated as all-core data.

Compare three reversible policies, separately from shader-worker-count tuning:

| Policy | Placement | Question |
|---|---|---|
| A | Current balancing + dxvk-cs core 3/priority 63 | Baseline |
| B | dxvk-cs on application cores at saved application priority; others unchanged | Does core-3 scheduling delay the critical command stream? |
| C | Broad permitted masks for non-fixed application workers; kernel schedules them | Does singleton pinning leave burst work waiting beside idle cores? |

Preserve explicit game affinity, capability checks, original priority/affinity
for rollback and required FEX TSO. B should be a targeted policy rather than
turning every independent feature off. Existing per-game four-cores setting
can provide a coarse preliminary comparison, but also changes compositor/
other helper placement, so its result is confounded.

Use the same save/route, graphics, clocks and build; separate first and warmed
passes, wait 5–10 minutes for startup. Record p50/p95/p99 frame times, long-frame
count, useful progress, worker CPU and queue waits. Prefer A/B/A to detect cache
warmup and changing workloads. Measure collector overhead independently.
No new screenshots during known routes; one final visual acceptance as needed.

Proceed to a reusable runtime policy only if measured scheduling/queue delays
and frame pacing improve. If the renderer remains saturated while compiler
queues are empty, prioritize guest/transition/polling cost. If a unique NAK
compile dominates with low off-CPU time, changing affinity cannot remove the
compile itself. This study establishes feasibility and a concrete experiment;
the new policies/telemetry and their hardware performance remain pending.
