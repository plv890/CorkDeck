# Backend performance and compatibility research — 2026-10-07

<!-- migration-checkpoint-20261007 -->

Актуальное состояние и итоговые результаты: [пакет передачи следующему чату](docs/handoff/2026-10-07/README.md).
Этот документ сохраняет последовательность этапа; его прежние current/HOME/policy/GDB-pending формулировки относятся к моменту записи и не переопределяют последний checkpoint.


Continuation after CPU scheduling experiments. No console setting, runtime
binary or production SDK payload was changed during this research pass. The
console remains on HOME with CPU policy 0 from the preceding experiment.

## New verified compatibility lead

The current SDK generator's FAMILIES does not include screenshots; the runtime
lookup therefore has no ISteamScreenshots object. An independent synthetic PE
probe was built with the pinned SDK ABI headers and existing LLVM-MinGW toolchain
for x86 and x64. It dynamically loads the current own SDK DLL, initializes a
dummy local account/AppID binding, and queries versions 001–003 through both
SteamInternal_CreateInterface and ISteamClient::GetISteamScreenshots.

Both architectures returned NULL for all six paths, with probe exit 0 (availability
check completed). Outputs and DLL hashes are retained in
`artifacts/backend-research-20261007/screenshots-results.json`. No game ran on PC.

The saved Ninja logs under CPU policies 3 and 0 both end with c0000005 after
Steam_Screenshots_Init, before first presentation. The exit context includes
ECX=0 and a saved instruction pointer in the game's initialization code. This
is consistent with an unavailable interface being used, but is not yet proof
of the exact crash instruction or requested interface revision. Preserve that
distinction. More cores cannot supply an absent ABI object.

[Valve ISteamScreenshots documentation](https://partner.steamgames.com/doc/api/ISteamScreenshots)
defines screenshot hooking and screenshot-library operations. A minimal fix
should provide exact versioned vtables, coherent hook state and unavailable
results for unsupported capture/library operations; it should not pretend to
run an overlay or upload screenshots. The pinned reference currently exposes
only the current Screenshots003 header, so older ABI versions require a verified
historical header/layout before implementing aliases. The launcher must infer
the screenshots version from preserved original SDK just as it does other
legacy families. Generator/runtime/probes/native payload hashes and previous
payload allowlist must change together. No title-specific EXE patch is needed.

## New verified scheduling confound

Mode 1 log `20261006T215348Z-bf17ea97/Dishonored.log` says three compiler threads.
Mode 3 log `20261006T220248Z-979041f5/Dishonored.log` says four. Thus the earlier
comparison simultaneously changed affinity policy and compiler thread count.
This helps explain why those checkpoints cannot establish a scheduling speedup.

DXVK 3.1.1's [official configuration](https://github.com/doitsujin/dxvk/blob/v3.1.1/dxvk.conf)
defines `dxvk.numCompilerThreads`: zero uses available cores, positive values
fix the count. The next CPU placement comparison should use a fixed count of
three in all policies, then independently compare counts two/three/four. Preserve
the config and cache; verify effective config and actual thread count in logs.

## Concrete GPL/compiler experiment from upstream source

The same official configuration documents three states for
`dxvk.enableGraphicsPipelineLibrary`: Auto uses GPL plus background optimized
pipelines; True uses GPL without background optimized pipelines; False disables
GPL. These are not equivalent to simply switching shader compilation on/off.

[DXVK 3.1.1 graphics source](https://github.com/doitsujin/dxvk/blob/v3.1.1/src/dxvk/dxvk_graphics.cpp)
confirms that True returns early from `compilePipeline`, preventing that worker's
optimized-pipeline path. It also retains base pipelines in `releasePipeline`.
`createInstance` can still fall back to optimized compilation if base linking
fails: True is not a guarantee of zero compilation or zero critical-path stalls.

[DXVK device source](https://github.com/doitsujin/dxvk/blob/v3.1.1/src/dxvk/dxvk_device.cpp)
confirms that Auto pipeline-lifetime tracking applies to 32-bit applications,
with a RADV exception. NVK is not that exception. This reinforces the existing
reuse/weak-reference investigation: released base libraries plus weak driver
cache entries can lead to repeated work. Actual key repeats and disk outcomes
are still unmeasured. A blanket `trackPipelineLifetime=False` is a memory-risk
experiment, not the first recommendation for this 32-bit workload.

First compare Auto versus True on the same scene, fixed compiler count/build/
CPU placement/clocks, with memory and frame-time monitoring. Fewer seconds spent
in the low-priority compiler would distinguish background optimization cost;
unchanged rendering-thread saturation would point to another limit. True may
reduce shader optimization quality and increase retained library memory, so
CPU stall reduction can trade against GPU throughput or memory use. Current
Dishonored uses a GPLAsync selection; verify its exact DLL provenance and option
semantics before assuming it implements every upstream behavior unchanged.
This setting experiment has not yet been executed on hardware in this pass.

## FEX scope and constraints

Local packaged DLL manifest identifies libwow64fex.dll SHA256
`0facb4af48adebb15def4fba956fa483dd91b0c602f1042db98df1433a32723a`, source pin
`ad60d97183a761c4` and modified Horizon origin. This is local package provenance,
not a fresh SD readback or a full resolved upstream commit. The runtime's
nx-amd64-fex-2640 label remains insufficient to identify every component.
The expected FEX checkout is absent from the local/build-mirror toolchains.

[FEX memory-ordering analysis](https://fex-emu.com/Scourge-of-emulation/)
explains acquire/release, alignment handling and gains requiring newer ARM
features. Do not force unsupported ARM extensions on Tegra or turn off TSO
to chase throughput: TSO=1 already fixed a target crash in this project.

The project exposes multiblock/MaxInst and JIT L1/L2 choices. Upstream's
[configuration source](https://github.com/FEX-Emu/FEX/blob/main/FEXCore/Source/Interface/Config/Config.json.in)
identifies multiblock compilation as a potential JIT-stutter contributor.
This is a valid later controlled workload experiment, but current main options
must be matched to the actual pinned port. Do not conflate JIT L1/L2 lookup
caches with NVK/DXVK shader caches. Current native pipeline-call stalls cannot
be attributed to FEX JIT merely because the process uses translation.

## Checks performed and limits

- Existing `check_horizon_wait_context.py`, `check_horizon_suspend.py`, and
  `check_wow64_thread_context.py` passed on the current source with synthetic
  fixtures. This supports preserving current wait/APC/context behavior while
  measuring overhead; it does not prove every game/target path.
- `check_fex_unaligned.py` could not start its fixture: required FEX checkout's
  CPUBackend.h is missing. The ARM64-only full FEX core checks cannot run as
  native ARM execution on the current x86_64 WSL host.
- `check_fex_smc.py` initially exited SIGSEGV with the existing Clang sanitizer
  build; rerunning with existing GCC + ASan/UBSan passed all fixture cases.
  The compiler/sanitizer-dependent failure remains unexplained; do not label
  the Clang run passed or infer a target SMC defect from it.
- x86/x64 Screenshots availability probes completed and reproduced NULL.
  Availability is verified; a corrected SDK and Ninja hardware acceptance are
  still pending. No production source was changed for this research pass.

## Prioritized next work

1. Resolve exact legacy Screenshots ABI/caller, add a bounded local interface
   and synthetic vtable/hook tests, then retest Ninja on the target.
2. Fixed-count CPU policy A/B/A to remove the compiler-count confound.
3. Verified-fork GPL Auto/True experiment with stage/cache key attribution,
   retained-memory checks and frame-time distributions.
4. Full QPC guest/WOW64/native boundary and caller measurement if the rendering
   thread remains saturated after compiler activity settles. Native clock reads
   already use the ARM system timer; no timer units/epoch change is justified.
5. FEX block/cache experiments after exact source/payload reconciliation and
   applicable fixtures are available. Keep TSO semantics and supported ISA.

Prefer aggregated fixed-size counters over verbose per-call logs. Existing
pipeline wall time includes driver work, lock waits and descheduling; add
thread CPU time, queue latency and cache outcomes to separate them. Keep known
button routes, log-only observations and 5–10 minute startup allowance.
