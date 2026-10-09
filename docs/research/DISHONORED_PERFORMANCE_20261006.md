# Dishonored: passive performance observation, 2026-10-06

Installed native revision: 2026.10.06-9. Game run: `000001a5a4cc596c`.
Evidence: `tools/remote_switch/runs/20261006T164719Z-observe-0cde0001/observation.json` and sample snapshot logs. Observation completed in 302.4 seconds. No input, settings changes, restart or deployment during measurement. The user operated the game.

## Findings

- Late samples show the main guest thread (`4w`) around 95–98% and `RenderingThread` (`88w`) around 93–97%. Aggregate low CPU utilization does not mean these critical threads have spare capacity.
- Successful-present counter rates fall to approximately 0.2–0.5 per second in late intervals. These are counter deltas, not GPU rendering duration; changing menus and startup activity confound comparisons.
- Shader worker utilization falls from approximately 47% earlier to 3–4% later, while poor presentation continues. Shader warmup alone does not explain the persistent slowdown.
- SD reads nearly plateau at 535–538 MB late in the run. Cache writes continue. Concurrent cumulative IO times must not be interpreted as elapsed wall time.
- No fatal event is recorded. Final runtime heap reports approximately 2268 MB used and 821 MB free; this does not rule out other memory constraints.
- Sampled syscall rankings are dominated by `0x31`, `NtQueryPerformanceCounter`: approximately 12–13 million estimated calls per progress interval in late samples. Instrumentation samples every 64 calls globally, so deterministic sampling may bias per-call counts. Dominance is a lead, not proof of cost or root cause.

The user-displayed monitor screenshot (`20261006T165358Z-9fb1fc6a/screen.jpg`) shows GPU 0% at 153.6 MHz and CPU frequency around 1785 MHz, with DXVK D3D9 HUD approximately 0.3 FPS. This is a single sample; short GPU bursts may be missed. CPU labels overlap, so the visible percentages are not treated as an aggregate. The RAM percentage is not evidence of free allocation capacity.

## Source inspection

`RtlQueryPerformanceCounter` calls `NtQueryPerformanceCounter`; the native Switch implementation reads the system tick and converts to 100 ns units, with declared frequency 10,000,000 Hz. No wrong frequency or epoch has been established. The cost of crossing the translated/native boundary and the origin of repeated polling remain unknown.

A search of application sources (`horizon-wine/source`, `dlls/ntdll`) found no explicit `pcvSetClock`, `clkrst`, `apmSet`, CPU boost call or literal 1785 MHz clock assignment. This is not an audit of all dependencies or installed overclock services. The selected overclock profile and actual effective limits have not been read back.

If the intended CPU target is 2400 MHz, increasing from 1785 MHz gives only a 1.34 frequency ratio. Even ideal linear CPU-bound scaling would not by itself explain a transition from 1–3 FPS to 30 FPS. Actual scaling may be lower because of synchronization and other costs.

## Next general diagnostics

1. Capture bounded, low-overhead per-thread caller samples for performance-counter polling; separate game frame limiting, active synchronization and graphics query polling.
2. Measure Vulkan submission, fence/present waits and pipeline creation duration to distinguish underfeeding the GPU from waiting for it.
3. Read back effective CPU/GPU/EMC clocks and the active title's overclock profile without changing them.
4. Optimize measured common hot paths or waiting behavior with local synthetic reproductions and controlled hardware comparisons. Keep the required FEX TSO setting enabled; do not change timer semantics blindly.

Current evidence supports investigating CPU-side work or active waiting starving the GPU. It does not establish that 30 FPS is achievable, that the GPU driver is at fault, or that a translator rewrite is necessary.
