# CPU worker experiments — native 2026.10.07-1

<!-- migration-checkpoint-20261007 -->

Актуальное состояние и итоговые результаты: [пакет передачи следующему чату](docs/handoff/2026-10-07/README.md).
Этот документ сохраняет последовательность этапа; его прежние current/HOME/policy/GDB-pending формулировки относятся к моменту записи и не переопределяют последний checkpoint.


User authorized implementing/testing alternate CPU placement and application
access to the fourth core. This extends CPU_SCHEDULING_RESEARCH_20261007.md.

## Implemented

Restart-only `/switch/wine/cpu-worker-policy.txt`, exactly one digit 0–3:

| Value | Behavior |
|---|---|
| 0 or absent/invalid | Existing app cores 0–2 balancing; graphics CS/compositor helper core 3, priority 63 |
| 1 | Named DXVK shader/cache workers on core 3 at priority 63; CS/compositor on application cores |
| 2 | Named DXVK shader/cache workers allowed cores 0–3, preferred 3, priority 63; CS on application cores |
| 3 | Application/native pthread affinity allowed 0–3, ordinary priorities retained; automatic Wine threads get broad permitted mask; custom balancing disabled |

Policies require existing four-core option and process core/priority capabilities.
Unavailable/disabled four-core support falls back to policy 0's three-core path.
Explicit program affinity is preserved. Modes 1/2 identify `dxvk-shader-l`,
`dxvk-shader-h`, `dxvk-cache`, not arbitrary game threads or an entire Windows
environment. Native compilation runs on the invoking worker. One core cannot
compile all dependent shader jobs simultaneously; mode 1 may serialize workers
and make compilation slower. Mode 2 tests whether letting the kernel migrate
those workers helps. These are experiments, not recommended defaults.

The installed forwarder already permits core IDs 0–3 and priorities 28–63 via
NPDM kernel flags (`source/forwarder.c:npdm_kernel_flags`). Mode 3 removes our
runtime/Wine mask restriction rather than changing firmware. Allowed core access
is not exclusive ownership: system threads still run, and their scheduling can
affect performance. Exact scheduling advantage requires hardware measurement.

`[SCHED]` records requested/effective policy and masks. Bounded `[SCHED-THREAD]`
rows record up to twelve active registered threads' preferred core, effective
mask, priority, CPU delta and query result codes during existing thread reports.
Preferred core is not an actual residency sample for broad affinity masks.
Additional queue/lock/compiler-stage telemetry is not implemented here.

No new third-party code, sysmodule or firmware installation. Existing libnx
affinity/priority calls are reused. Defaults remain legacy without the test file.
Rollback policy: write 0/remove file and fully restart Autorun. Runtime rollback:
restore matching NRO/manifest backups and remove the newly created policy file.

## Local and installation evidence

ASan/UBSan host thread-profile test passed including policy worker classification
and existing balancing fixtures. Native build completed with existing warnings;
matching ELF/NRO retained under `artifacts/cpu-scheduling-20261007`.

Installed NRO: 40,563,768 bytes,
SHA256 `f4c345fafed86ea04435c11bcacfd029deb7b7b476ba6a576e566fa538877dcf`.
NRO and manifest stage/final readbacks verified; policy 1 read back as `1\n`.
Previous -11 retained as `.before-scheduling-20261007` for both runtime files.
No previous cpu-worker-policy file existed. Installation journal and one-shot
installer are in the artifact folder. Do not rerun this installer: its old hash
and unused backup assertions intentionally apply only once.

Temporary DBI FTP was stopped and DBI closed before candidate launch.
Dishonored launched through the confirmed existing carousel route, without
repeated screenshots. Initial observer title query timed out during startup's
temporary remote-channel loss, as in the earlier baseline. This is not a crash
verdict. Hardware results are added below after bounded observation.

## GTA V reference

The user's reference was identified as `YNWAMAX IPS Mod - GTA5_September_Core3`
in public discussion. [Discussion](https://gbatemp.net/threads/gta-v-port.684062/page-4)
describes an application exeFS/main IPS patch. The actual patch, authoritative
source and license were not available in this investigation, so the exact
changed code and claimed FPS gains are unverified. No GTA files were downloaded
or applied. It is not established that it changes the Horizon kernel. Our
independent mode 3 tests the analogous application mask restriction with owned
runtime source and existing process capabilities.

### Supplied addon inspection (supersedes "actual patch unavailable" above)

The user then supplied local paths for the addon IPS and NPDM and authorized
reading matching SD copies. Local files were parsed without executing GTA.
`artifacts/cpu-scheduling-20261007/addon-analysis.json` retains bounded records.

- IPS: 129 bytes, SHA256
  `43678134c2046a17c6325fc80eab0402cd47036ab92e85ee87dde775399e59c9`.
  Eight replacement records; ARM64 decoding uses existing devkitA64 objdump.
- NPDM: 1,604 bytes, SHA256
  `bb4e5775535c280e546891be4e145e55d354cf5ef6b35cc9f0aea3a522bf2079`.
  ACI0 and ACID kernel flags both `0x030073b7`: core range 0–3,
  priority range 28–59. Autorun's `0x030073f7` permits the same cores and 28–63.
- Replacement instructions include comparing a loaded value to 3, loading
  mask 8, ANDing a mask with 7, and selecting mask 8 conditionally. This is
  consistent with the `Worker3Only` folder name: selected worker gets core 3,
  other work remains on cores 0–2. Branches and an entry-related pointer
  comparison make it application-specific. Without original main/symbols,
  exact caller identity and complete control flow are not proven.

These files give concrete evidence of application permissions plus worker
affinity changes, not evidence of a kernel-wide unlock or a fourth exclusive
application core. No GTA executable/source or patch bytes were imported into
Autorun. Mode 1 uses our own named-worker classification; mode 3 separately
tests broader application affinity. The supplied GTA NPDM is not installed
for Autorun because it also contains game-specific identity/service settings.

Both supplied files were read from their exact SD paths through DBI FTP and
matched local SHA256 byte-for-byte. `sd-addon-and-policy.json` records the read.
No addon path was modified. Only `/switch/wine/cpu-worker-policy.txt` changed
from 1 to 3 for the next test, with its mode-1 content retained on SD.

## Mode 1 hardware checkpoint

Observer `20261006T214737Z-observe-dcf53c56` completed 300.4 s without input or
screenshots; last game second 373, 5,446 successful presents, fatal=0.
Counter rate across its retained fragment is about 18.03 presents/s. Largest
retained aggregate pipeline call in observer top rows: 249.15 ms. This is not
a same-scene comparison with the baseline's later gameplay stalls.

Full redacted saved log: `tools/remote_switch/runs/20261006T215348Z-bf17ea97/Dishonored.log`.
It confirms effective policy 1, shader workers 76/80/84 with mask 8 / priority 63,
and CS 72 with application mask 2 / priority 59. Thus placement is hardware
validated, but FPS benefit and cache-worker placement when that worker becomes
active remain separately unproven. Native worker CPU/queue attribution and
collector overhead are pending. Mode 2 has not yet been hardware tested.

## Mode 3 hardware checkpoint

Observer `20261006T215713Z-observe-70660502` completed 300 s, after startup's
temporary channel outage. Last game second 468: 7,003 successful presents,
fatal=0. First successful presentation around second 164, later than mode 1's
approximately 114 s. Final screenshot `20261006T220213Z-06a3acc5/screen.jpg`
shows Dishonored's rendered menu/background, not a same-scene gameplay benchmark.

Full saved redacted log `20261006T220248Z-979041f5/Dishonored.log` confirms
`[SCHED] requested=3 effective=3 app_mask=f` and Wine threads with mask f /
priority 59, server threads mask f / priority 57, both kernel query results zero.
This establishes ordinary-priority application permission for all four cores.
It does not establish exclusive core ownership or that every thread actually
executes on core 3. `helper_mask` in the header describes the legacy helper
mechanism and is not active in mode 3; per-thread queried values are authoritative.

The supplied addon provides a reusable scheduling principle, not a portable
performance library. All four cores and selective worker placement now work
in owned Autorun code. Any FPS gain remains unproven until same-save A/B/A with
stable clocks and cache state; mode 3's longer observed startup discourages
claiming an unconditional improvement. Native pipeline-stage attribution,
queue latency, diagnostics overhead and mode-2 hardware acceptance remain pending.

## Regression control and final state

10 Second Ninja X was tested with mode 3, then with mode 0. Both runs exited
before first presentation with `NtTerminateProcess(self)` code `c0000005` after
Steam UserStats/RemoteStorage/Screenshots initialization messages. Mode 3 is not
required to reproduce it; the exact SDK/backend cause remains unproven. This is
not a passed Ninja acceptance. The older successful -7 test predates local SDK
updates and is not evidence that the present SDK combination works.

Redacted logs: `20261006T220717Z-23986c3c/10SNX.log` (mode 3) and
`20261006T220917Z-b266df9b/10SNX.log` (mode 0), under remote_switch/runs.
The passive observer saw active=0 after return, so its lack of a fresh fatal
flag did not prove a successful launch. Saved exit evidence is authoritative.

Final SD policy is **0**, readback verified (`final-policy.json`). Experimental
modes remain available, but are not automatically recommended. Temporary FTP
was stopped and DBI closed to HOME. Native candidate remains installed with
coherent -11 rollback backups. No GTA addon files or game EXEs were modified.
