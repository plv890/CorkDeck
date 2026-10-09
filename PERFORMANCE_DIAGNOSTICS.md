# Passive performance diagnostics

Current native revision and measured results are in the
[handoff package](docs/handoff/2026-10-07/README.md) and
[results/roadmap](docs/handoff/2026-10-07/RESULTS_AND_ROADMAP.md).
The collector described below is inherited by 2026.10.07-8; optional pipeline CPU timing,
native worker and passive wait observations were added separately. Controlled frame-time
distributions and collector-overhead A/B remain pending.

Native revision 2026.10.06-11 adds a bounded collector to the existing runtime.
It requires no new Switch sysmodule or listener. The older `profile.txt` sampler
still exists separately and briefly pauses threads; this collector does not.

## Enable and read

`/switch/wine/performance.txt` containing `1` starts collection for the entire
game session. `0` or a missing file disables automatic collection. Per-game
launch starts a fresh generation; old calls cannot contaminate a new window.
Without that file the hot-path checks return immediately. Sampling stops at the
end of the game or a remote stop. Statistics remain available until the next window or process exit.
Caller tables rotate approximately every minute after their final summary is
written, allowing later gameplay to replace startup callers within fixed memory.
`elapsed_ms` is session age, `window_ms` is the current caller-table age.

Existing authenticated LAN diagnostics on TCP 8181 provides:

- `GET /perf`: up to eight most expensive recorded caller buckets.
- `POST /perf/start`: reset statistics and collect until stopped or game exit.
- `POST /perf/stop`: stop collection and retain results.

All require the current `X-Autorun-Key`; mutating GET requests are refused.
Keys expire on application exit. The local helper never prints or stores them:

```powershell
python tools/remote_switch/performance.py --host 192.168.70.59 --lan-key-file <DPAPI-file> --output <report.txt> start
```

With SD snapshots enabled, the existing independent TCP 6000 reader can read
`[PERF]`, `[PERF-CALL]` and `[PERF-CLOCK]` lines without HTTP or a new key. Reports
use the existing flusher, approximately every ten seconds. No per-call SD writes
occur. The last report is retained after collection expires.

## What is measured

- `qpc`: one in 1024 calls per thread to native `NtQueryPerformanceCounter`.
- `nt-wait`, `nt-delay`: one in 64 calls per thread to native multi-object wait
  and delay; this does not cover every possible synchronization operation.
- Vulkan submit, present, fence/semaphore wait, image acquire, graphics/compute
  pipeline creation: all calls through the instrumented Wine Vulkan thunks,
  both 32-bit and 64-bit paths.
- Existing `[THREADS]` reports retain per-thread CPU consumption and core.
- CPU/GPU/EMC rates are queried read-only through `clkrst` while collecting.
  A nonzero result means unavailable; `hz=0` with an error is not a real clock.
  These are service-reported rates. Hardware overrides or governors may differ;
  compare with the external monitor rather than treating this as proof of the
  physical clock or the active overclock profile.

Each bucket records Wine thread ID (zero for workers without Wine TLS), native caller address, saved WOW64 EIP if present,
number of recorded calls, summed wall nanoseconds, maximum wall nanoseconds,
count at least one millisecond, and negative return codes. A saved EIP can name
the WOW64 transition rather than the original game's call site; it is not an
asynchronous guest PC sample. Native caller addresses need the matching ELF and
image mapping to symbolize. The window generation distinguishes resets.

## Interpretation and limits

Durations include blocking and descheduling, not just CPU instructions. Native
QPC measurements exclude translation and earlier Wine/WOW64 boundary overhead.
Vulkan thunk measurements include argument conversion and its host call; they
are CPU-side wall durations, not GPU timestamps or GPU utilization. Internal
driver calls that bypass these thunks are not independently timed.

Sampling is deterministic; exact counts and extrapolated costs can be biased.
Do not compare raw QPC sums with unsampled Vulkan sums as time shares. Nested or
concurrent durations cannot be added to get wall time. Use repeated controlled
comparisons and matching thread/clock/present evidence.

Storage is fixed at 96 caller buckets. Recording uses a try-lock; conflicts and
new keys after capacity is reached are dropped and counted. Reports show only
the top eight buckets by sampled wall sum, so absent rows do not prove zero
activity. The disabled collector performs only a deadline check. Enabled
overhead is not yet benchmarked on hardware; all-call Vulkan sampling can affect
very call-heavy workloads. Timer reads themselves add measurement cost.
Calls still running when a caller-table generation rotates are discarded if
they finish in a later generation; this is a limitation for very long waits.
In-flight calls do not appear until completion. This is not a full tracing system.

Synthetic ASan/UBSan tests cover sampling, injected waiting, concurrent writes,
capacity, short output buffers, generation isolation, optional timed expiry,
continuous mode, and native workers without Wine TLS. HTTP
tests cover key enforcement, wrong methods, start/read/stop, existing redaction
and old routes. These tests do not establish game FPS or full compatibility.

No timer semantics, FEX TSO, game graphics settings or overclock rates are changed.

## Initial hardware correction

The first -10 probe emitted valid timer and CPU/GPU/EMC reports, but encountered
a native access violation before presentation. Its closed Dishonored log records
`far=0x1488`, null `x0`, and a parked thread. The matching ELF's QPC wrapper at
RVA `0xf6300` reads `TEB->TlsSlots` directly after `NtCurrentTeb`, without checking
NULL. Native workers may legally have no Wine TEB. This was a collector defect,
not a hardware capability conclusion. Revision -11 guards NULL, captures thread
identity before the native call, and locally tests both Wine and non-Wine workers.
Explicit native `[EXC] unhandled ...; parking thread` now also latches fatal kind 4.

## Revision -11 hardware checkpoint

Observation `tools/remote_switch/runs/20261006T173320Z-observe-d0b9940c` completed
660 seconds without sending input or taking screenshots during polling. Fresh
run `000001b2932c2d82` reached successful presentation around game second 121.
Final game state at second 656: stage 6, 4544 successful presentations, fatal=0.
Collector remained active at elapsed 652368 ms, beyond the former five-minute
limit. 49 distinct partial/top caller reports were retained; output is available
in `D:/autorun fork/artifacts/performance-continuous/captured-performance.json`.

The final independent screenshot `20261006T174452Z-df232cd2/screen.jpg` confirms
Dishonored's menu. Its HUD shows 17.4 FPS at that moment, with external monitor
GPU 55% at 614.4 MHz and CPU near 1588 MHz. This is not a benchmark or evidence
that logging improved FPS. Service rate reports during the observation instead
showed 1785/76.8/1600 MHz; the discrepancy is why the source is explicitly labeled
and hardware clocks must not be inferred from `clkrst` alone.

The new timings identify pipeline creation as a concrete investigation lead:
one retained interval contains a maximum native Vulkan pipeline-call duration
of approximately 4.21 seconds; a later interval shows approximately 0.247 seconds.
Compilation/warmup, caching, descheduling and other driver costs need separating.
These overlapping CPU-side measurements do not prove the cause of every slow
frame or establish gameplay performance. The game was left running with continuous
collection enabled. Temporary DBI FTP is closed; old NRO/manifest backups remain.
