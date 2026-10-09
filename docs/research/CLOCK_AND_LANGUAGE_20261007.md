# Clock profile and console language — 2026-10-07

<!-- migration-checkpoint-20261007 -->

Актуальное состояние и итоговые результаты: [пакет передачи следующему чату](docs/handoff/2026-10-07/README.md).
Этот документ сохраняет последовательность этапа; его прежние current/HOME/policy/GDB-pending формулировки относятся к моменту записи и не переопределяют последний checkpoint.


## Confirmed clock faults

The active manager is Horizon OC 2.5.1. The Autorun title is
`0548EABB35576000`, and the active power profile was PD Charger.
The global PD profile requested CPU 2499, GPU 1228 and RAM 3000 MHz,
while actual readings were 1020/307/1600 MHz.

`/config/horizon-oc/config.ini` contained a separate Autorun PD profile
requesting 1785/921/1600. HOC resolves application values before global
values. Removed only these three application overrides, preserving all
global profiles, voltages, timings and other application profiles.

The Hekate Autorun entry loaded both `hoc.kip` and `autorun-loader.kip`.
Both declare the same Loader program ID. The second binary exactly matched
the project's stock Loader (SHA256
`996922063f4a45b963f7bf61fff342fd9e1a6f0fc323868ab1938afec773d6dc`).
The installed HOC already matched the project's compatible HOC code after
normalizing the CUST settings block (revision 6, version 251). Thus the
correct repair is one HOC Loader, preserving its CUST block, together with
the existing custom Mesosphere.

Updated the setup recognizer to include `autorun-loader.kip` when removing
existing Loader lines, and added a regression fixture containing all three
Loader filenames. Targeted setup tests passed. The updated native Quick
setup then completed on the console using HOC / Preserve existing.

Backups and readback records are in
`../artifacts/clock-language-20261007/`. The original autoboot number is 1;
3 was selected temporarily to test the corrected Autorun entry. Restore
the owner's autoboot choice after collecting the hardware result.

Official source used:
[Horizon OC 2.5.1](https://github.com/Horizon-OC/Horizon-OC/tree/2.5.1),
particularly clock_manager.cpp, config.cpp, board_freq.cpp and the profile
UI. The frequency chooser gets its table from HOC, which queries clkrst / pcv;
it is not a list of clock limits implemented in Autorun.

## Implemented language path

Native console language comes from
[libnx setGetSystemLanguage](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/set.h).
A bounded mapping covers all 18 current console language codes, including
Chinese script aliases, with English fallback on query failure or unknown
codes. Steam download settings use this default when no valid explicit
preference exists.

Wine's Windows NLS lookup now receives the console locale directly before
locale initialization: Newlib's C locale cannot provide it. This selects
the Windows system/user locale and UI language through existing Wine NLS
data, without relying on installed Unix locales.

The local Steamworks DLL now reads validated `language` and `ui_language`
settings instead of returning English unconditionally. The native preparer
preserves a valid recorded installation language and otherwise defaults to
the console language. Steam UI language follows the console. Existing game
language choices and game assets are not rewritten or redownloaded.

Own x86/x64 ABI probes passed, including Russian game language with German
UI language. Native preparer ASan/UBSan passed, including preservation of
the Russian installation preference and separate UI fallback.

Runtime 2026.10.07-4 and both SDK payloads installed with staged/readback
verification and backups. Runtime SHA256:
`561a33cb527c533c0902f40cb0369b7d01d0b3e6a2b0c7b24f2c3434f29e64a9`.
It also includes the previously tested optional pipeline thread CPU-time
diagnostics. Console locale and actual post-restart clocks still require
the hardware result below; host tests do not prove these.

## Hardware result after restart

The user confirmed boot into the corrected profile and then controlled
Dishonored themselves. No controller input or screenshots were requested
during that game test. Observer:
`tools/remote_switch/runs/20261007T131922Z-observe-de2ba4f5`.

HOC overlay in the launcher confirmed actual RAM 3000 MHz. CPU was 1428 MHz
at idle. During Dishonored, successful clkrst reads confirmed CPU
2499000000 Hz and EMC 3000000000 Hz. The application's stock clock ceiling
is therefore removed; the CPU governor still varies frequency with load.
GPU readings varied between 230.4 and 76.8 MHz. These are observed clocks,
not a claim of maximum GPU load or a controlled FPS speedup.

At game seconds 124–146, presents increased 145→694, about 25 presents/s.
They then reached 778 and stopped increasing in the following samples;
registered thread CPU load fell to 0.07 cores. A user-visible pause or hang
must be distinguished before attributing this to synchronization. The same
critical-section address `02491ED4` also timed out in the previous mode-3
log while presents subsequently continued, so its warning alone does not
establish the cause of the current stall.

Pipeline thread CPU-time queries work on hardware. One report counted
1762 calls on tid 0x4c: 1.624 s wall time and 0.416 s current-thread CPU
time; tid 0x48 counted 382 calls: 0.298 s wall and 0.041 s CPU. This is a
new way to separate executing compiler code from scheduling/wait time.
It does not count CPU consumed by other threads and does not identify a
specific lock; do not call all of that difference idle CPU or estimate FPS
gain from it.

The user confirmed that the first launch visibly hung in the main menu,
and that its FPS had noticeably improved beforehand. This is a qualitative
observation, not a measured benchmark. The user restarted the game while
retaining all control. The second log-only observation is
`tools/remote_switch/runs/20261007T132432Z-observe-c73abce9`; at game second
333 it had reached 2512 presents with fatal_kind=0 and was still progressing.

The second run eventually stopped at 4636 presents, and the user confirmed
another visible hang and ended game testing. Full saved log and decoded
stacks are analysed in `DISHONORED_OC_HANG_20261007.md`.
Wine NLS successfully reported Russian LCID 0x0419 on hardware.

Final runtime revision 2026.10.07-5 adds an explicit System language choice
to Steam settings and preserves inheritance when changing download
concurrency. Targeted settings ASan/UBSan and offline language-picker
fixture passed. NRO/manifest installed and readback verified, SHA256
`935a0129b95d616c2e4d44efa1d9cf78171b0e4d00d5f50d2fc655eb2fa8fc82`.
No game was relaunched to test revision 5, as requested.
The owner's original autoboot=1 was restored, retaining the repaired
single-HOC Autorun boot entry.
