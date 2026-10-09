# Remote Switch bench

Current checkpoint: [handoff package](docs/handoff/2026-10-07/README.md),
especially [control and telemetry](docs/handoff/2026-10-07/REMOTE_CONTROL_AND_TELEMETRY.md).
The installed sys-botbase now has a separate single-flight FS worker and PM-only title queries;
the older no-extra-thread description below is historical. Native GDB attach/stacks/detach
were tested, then its original configuration was restored and the console rebooted.
Latest verified state is DBI FTP Browse SD, not the older HOME/applet states below.

## Experimental CPU placement — 2026-10-07

Native `2026.10.07-1`, 40,563,768 bytes, SHA256
`f4c345fafed86ea04435c11bcacfd029deb7b7b476ba6a576e566fa538877dcf`,
installed with coherent NRO/manifest readback and retained -11 backups
`.before-scheduling-20261007`. Restart-only CPU worker policies and bounded
placement telemetry: [CPU_SCHEDULING_EXPERIMENT_20261007.md](docs/research/CPU_SCHEDULING_EXPERIMENT_20261007.md).
Mode 1 placement and mode 3 ordinary-priority mask 0xf are hardware verified. These are
experiments, not a validated speedup. Legacy remains the absent-file default.
Final SD policy is 0 (verified); temporary DBI FTP stopped, console returned to
HOME. Ninja's early Steamworks exit reproduces under both mode 3 and mode 0;
regression acceptance is pending and is documented in the experiment report.

## Latest installed update — 2026-10-06, passive performance diagnostics

Native revision `2026.10.06-11`, 40,563,768 bytes, SHA256
`cfcfb85852363ba44799d77d01fdc23d3d40eee9f51d7f7af71dc802ac581dd7`.
NRO/manifest readback verified, previous -10 retained with
`.before-performance-continuous-20261006`; -9 backups also remain.
Temporary DBI FTP closed after installation.
Bounded passive timing is enabled by `performance.txt=1`; collection continues
for the entire game session, with fixed caller tables rotating each minute.
Existing TCP 6000 snapshots and authenticated TCP 8181
carry reports; no additional listener or sysmodule. New start/read/stop controls,
sampling ratios, clock availability and limitations:
[PERFORMANCE_DIAGNOSTICS.md](PERFORMANCE_DIAGNOSTICS.md).

The preceding -9 added a general local Steamworks SDK preparation path and
bounded display modes with virtual modesetting enabled. Balatro was confirmed
running with the minimal local SDK, and the user reached Dishonored's menu.
Dishonored remains extremely slow; this is not proof of playability or 30 FPS.
The passive -9 observation is recorded in
[DISHONORED_PERFORMANCE_20261006.md](docs/research/DISHONORED_PERFORMANCE_20261006.md).
Older dated checkpoints below describe earlier builds.

## Current update — 2026-10-06, compatibility runtime

Installed native revision `2026.10.06-8`, 40,543,288 bytes, SHA256
`afdbc42f26d7ee742cca4054b25395f0094bbffec9d29355415c211962c0eee0`.
Only NRO/manifest replaced with backup `.before-profile-api-20261006`;
staged/installed SHA readbacks verified, temporary DBI FTP stopped.
Automatic Steam sign-in survived repeated process restarts/updates.
General changes: WOW64 target context/access support, safe-point snapshots during
ordinary waits, original wait semantics preserved across Resume/APCs, bounded
capture lifetime, FEX TSO=1 default, fatal NT-exit retention, Steamworks dependency
advisory, and metadata filtering fix for incidental hex `2fa` in snapshot IDs.
Ninja reaches title and playable hub with accepted input on -7 (4002 presents over
the 120-second observation, explicit fatal=0); FPS samples range 35.7–60.3, not a
controlled performance comparison. Catlateral -6 no longer logged its prior
GetThreadContext/GC error but stalled with zero frames; -7 retest pending.
Dishonored TSO-only A/B on -4 reached splash, then crashed dereferencing a NULL
SteamRemoteStorage interface. This is not yet playable or proof of 30 FPS.
Detailed provenance/limitations: [COMPATIBILITY_TEST_20261006.md](docs/research/COMPATIBILITY_TEST_20261006.md).

## Earlier update — 2026-10-06, download diagnostics

Installed revision `2026.10.06-1`, 40,539,192 bytes, SHA256
`4626896c5049f784853f3bd1b286b26045493df1c817279c990243421be7f265`.
NRO/manifest readbacks matched; backups use `.before-download-diagnostics-20261006`.
DBI FTP stopped; automatic Steam sign-in with the existing vault confirmed after
replacement. Two prior-build downloads completed: resumed 12 Is Better Than 6
and fresh Balatro. Measurements, overlapping counter semantics and speed follow-up:
[DOWNLOAD_TEST_20261006.md](docs/research/DOWNLOAD_TEST_20261006.md).

After successful startup/settings verification, a further batch intended to open
Banished details produced black output and sys-botbase timeouts. Third installation
and launch were not confirmed. Physical HOME responsiveness was requested from
the user; do not send additional blind input or label the cause a proven crash.

## Previous update — 2026-10-05, automatic Steam session

Installed native revision `2026.10.05-4`, NRO 40,539,192 bytes,
SHA-256 `55f5a6f0025522786d2134b29a04d14218ba3fd4630ba1021fa8a54304b2bf6b`.
Replaced only `/switch/wine/wine-nx-runtime.nro` and `build-manifest.json` through
the user's explicitly authorized temporary DBI FTP Browse SD on port 5000.
Both staged and installed readbacks verified. Backups have suffix
`.before-auto-session-20261005`; the old NRO hash is
`af2d7b6d8d04c23525a52a2206a3e09dfc96ff1999daa17647db2e76b47216f8`.
No vault, game or configuration files were read/exported/changed by deployment.
FTP stopped with B and DBI closed using the guarded HOME/X/A route, confirming
no active title. Local installation journal: `../artifacts/steam-persistent-session/installation.json`.

Before update, Settings showed **Not signed in / No saved session**. A fresh QR
was generated, but no user account approval was completed before replacing NRO.
Successful deployment is not an authenticated session or crypto lifecycle test.
Subsequently the user approved initial Steam sign-in. Device-bound automatic
save succeeded, followed by full Autorun close/relaunch and automatic green OK
without QR/Guard/passphrase. Steam library showed 617 available games including
Families. Evidence: `20261005T184336Z-1857cfad` save confirmation;
`20261005T184405Z-41ef22a1` close route; `20261005T184433Z-ba75aeea`
restored settings; `20261005T184454Z-ba02df71` loaded library. No token export.

User prefers batch navigation with known order/count. Saved routes and starting
states: [QUICK_NAVIGATION.md](tools/remote_switch/QUICK_NAVIGATION.md).
`broker.py navigate` captures start/end only, checks title before each input and
detaches in finally. Fixed DBI route: Down x12, A, Down x2, A. Fixed close route:
primed HOME, X, A. On this run the close route successfully closed DBI. HOME moves
the most recently launched title first; do not assume Autorun remains first after DBI.

## Initial installation — 2026-10-05

User device: OLED, Horizon 21.1.0, Ultra 3.0; exact Atmosphere version unknown.
Autorun launches from its installed HOME forwarder. Preserve that launch path,
39-bit address space and existing loader/Mesosphere patches. Horizon USB is not
working for the user; Hekate SD USB works. Use Wi-Fi for this bench.

DBI FTP at 192.168.70.59:5000 was reached on the physical device. Anonymous login,
CWD + LIST, RETR, STOR, SIZE and rename worked. NLST and LIST with a path argument
returned empty results: use CWD then LIST. Port 6000 timed out before installation.
The SD contents listing had no sys-botbase directory.

Installed official sys-botbase v2.5 into a previously absent directory. Archive
SHA-256: b8a4f6922cfe55f586fe54fd90df2bb646912b6d4c54e881d082e0f191adf47d.
Added exactly:

- /atmosphere/contents/430000000000000B/exefs.nsp
- /atmosphere/contents/430000000000000B/flags/boot2.flag

Executable was uploaded as .part, read back byte for byte, renamed, then enabled
with boot2.flag. Executable SHA-256:
f2a2809656ed7ddfc96944d4f17a216df7d47e6787ff46a4b1f2a350d105c9c2.
No existing SD files or configs were changed. PC backups of system_settings.ini,
override_config.ini, installed Autorun build manifest and installation evidence are
in D:/autorun fork/artifacts/remote-switch-setup, outside this Git repository.
Existing config has enable_debug_api=u8!0x1; no extra capture setting was added.

This installation was subsequently activated and validated as recorded below.

## Minimal tool choice and sources

- [Original sys-botbase](https://github.com/olliz0r/sys-botbase/releases): v2.41
  explicitly added HOS 21.0.0+/Atmosphere 1.10.0+ support; selected v2.5 adds
  metadata failure handling. Input and JPEG capture, not a process debugger.
  [ASCII command protocol](https://github.com/olliz0r/sys-botbase/blob/master/commands.md).
  Capture can still fail on a particular firmware/homebrew configuration.
- [C++ variant](https://github.com/PokemonAutomation/sys-botbase-cpp): USB/Wi-Fi
  and backward-compatible mode. Not installed alongside original module.
- Existing DBI FTP supplies file transport while DBI is foreground. It is not
  assumed to survive exiting DBI. Continuous transfer needs a separately tested
  FTP sysmodule or a reliable UI sequence to open DBI.
- [nxlink/hbmenu](https://github.com/switchbrew/nx-hbmenu): network loader only;
  not selected for replacing the special Autorun forwarder launch path.
- [Atmosphere standalone GDB](https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/changelog.md)
  documents extended-remote on TCP 22225. Verify installed Atmosphere and symbol
  files before enabling. Keep HTC disabled. Begin with a small test process, never
  sockets/system services; a native ARM backtrace does not establish a Windows/FEX cause.
- [Twili](https://github.com/misson20000/twili): documented firmware support table
  is old, so not selected for this HOS 21.1.0 bench without separate validation.

No third-party source code was copied; adapters implement a documented protocol.
Downloaded sysmodule is GPL-3.0 and is kept outside the repo with official release
metadata. No EdiZon, Ultrahand extension, firmware or runtime upgrade is required
for the first input/capture test.

## PC-side module map

tools/remote_switch uses installed Python 3.14, standard library only:

| Module | Role |
| --- | --- |
| broker.py | One action, unique run ID, JSON result, stops on failure |
| transport.py | Literal private IPv4, TCP deadlines and bounded replies |
| controller.py | Version/title, JPEG, bounded click, detach; no memory commands |
| diagnostics.py | Existing read-only Autorun :8181 log/status adapter |
| transfer.py | Content-addressed NRO staging, separate test-target activation and retained prior test |
| credentials.py | Windows user DPAPI credentials, outside Git |
| ftp_logs.py | Direct FTP bounded saved-log collection and redaction |
| debugger.py | GDB process inventory only, no attach/memory writes |
| test_remote.py | Host transport fixtures; no physical-device claims |

Commands from Windows workspace (replace IP if it changes):

```powershell
python autorun/tools/remote_switch/broker.py --host 192.168.70.59 probe
python autorun/tools/remote_switch/broker.py --host 192.168.70.59 screenshot
# Inspect image first, choose harmless input and use exact ID from probe:
python autorun/tools/remote_switch/broker.py --host 192.168.70.59 click DRIGHT --expected-title TITLE_ID
python autorun/tools/remote_switch/broker.py --host 192.168.70.59 detach
# Open DBI FTP first; this stages only, never launches/replaces Autorun:
python autorun/tools/remote_switch/broker.py --host 192.168.70.59 stage-test path/to/test.nro
python -m unittest discover -s autorun/tools/remote_switch -p test_remote.py -v
```

Enable Settings > Steam settings > LAN diagnostics for this run to collect logs.
Set AUTORUN_LAN_KEY in the process environment to the 32-hex fragment displayed
on console, then run the `logs` action. Never put key/URL in CLI arguments, Git,
action journal or shared reports. Logs are bounded tails and can contain private
game output; inspect before sharing. Partial results survive later failures.
Runs and images are Git-ignored; each action.json distinguishes response received
from visually verified execution. A timeout has no automatic input retry.

## Access, initial checks and rollback

Use a trusted LAN only. sys-botbase listens directly without broker authentication
and exposes memory commands to other clients even though this CLI omits them.
FTP DBI is also anonymous. No internet port forwarding. Router client isolation
or a private dedicated LAN is needed if the current network is not trusted.

After the initial reboot, leave HOME and Wi-Fi awake. Agent checks version and
screenshot, then one harmless direction with before/after images and detaches.
Verify that HOME and Autorun screens both capture correctly. Next: identify DBI
and Autorun title IDs visually, test launch/close, stage a small test NRO, and
prove the full close/update/relaunch sequence with rollback before night runs.
Do not replace a live NRO, DLL or manifest or alter games, prefixes, settings,
sessions or boot patches. stage-test never replaces installed files; activate-test
can only replace the separate /switch/autorun-remote-test/probe.nro after verifying
foreground DBI. It preserves the prior probe under its SHA-256 name.

Disable sys-botbase by removing only its boot2.flag and rebooting; remove the
new module directory if permanently rolling back. Original configs require no
restore because they were not changed. Exit DBI to disable its FTP and disable
temporary LAN diagnostics in Autorun. A kernel/GPU/network hang can require a
physical reboot; preserve results and stop, never loop power/recovery attempts.

Host checks: 18 deterministic tests passed on Windows Python 3.14.6. Initial real
local socket fixture stalled under sandbox and was stopped, so fixtures do not
establish live LAN behavior. Physical DBI FTP was tested separately with approved
LAN access. WSL inspection returns Wsl/E_ACCESSDENIED in the sandbox, but approved
execution outside it reaches AvatarLab and the existing pinned build image.

## Hardware progress and persistent access — 2026-10-05

Validated on physical OLED/Horizon 21.1.0:

- sys-botbase responds 2.5. HOME screenshots work. An empty getTitleID response
  means no active application metadata, now represented as null rather than error.
- One DRIGHT visibly changed selection from DBI to Autorun. Autorun was launched
  through the existing HOME forwarder; its library and settings screenshots work.
  Observed Autorun title ID: 0548EABB35576000. Settings identify 2026.10.02-7.
- LAN diagnostics was enabled remotely. Windows local English OCR read the
  temporary key directly into memory, and bounded log tails were downloaded.
  autorun_runtime.log returned HTTP 409; 15 other files were collected (stdout/
  stderr were empty). Collection now records per-file HTTP errors and continues.
  These are saved historical game logs, not evidence of new game tests.
- HOME click did not switch the screen during that Autorun dialog; do not assume
  it worked or that it closes an application. Native Exit Autorun + its confirmation
  returned to HOME; getTitleID then returned empty. Controller remained available.
- DBI was launched remotely. Version 905-ru, observed title ID 03DB117EE17B6000.
  Selected FTP > SD card browsing. Its FTP worked again without user menu input.
- Offline libnx probe v1 compiled with symbols, 196608 bytes, SHA-256
  ff67a42cc9c44f903856ac958bf814ec0513a87cb4a0a78d1a53e8e2ee6f69a6.
  Uploaded to a separate test directory, verified by readback, and activated as
  /switch/autorun-remote-test/probe.nro while DBI was foreground. Not yet launched.

User explicitly approved persistent authenticated FTP :5001 and unauthenticated
standalone GDB :22225 in their trusted LAN, after automatic approval review
required confirmation of those exact services. Installation then succeeded.
Added sys-ftpd module ID 420000000000000E and /config/sys-ftpd/config.ini.
Anonymous login disabled; unique credentials are stored on PC using Windows
per-user DPAPI in artifacts/remote-switch-setup/ftpd-credentials.dpapi outside Git.
The server's SD config necessarily contains its password; do not export it.
FTP is plain LAN transport, not TLS. No internet forwarding was configured.

Official cathery/sys-ftpd 1.0.5 source ce7c85f705c365aea9e901cb6dd3a3f17a25b42b
was rebuilt using the existing image (image ID
8e7dcd7f344ccadb4ac54f3db025a6688488a514b88c9b7087e31b6c526299bc).
The old release documents HOS 19 support, so HOS 21.1.0 compatibility still needs
hardware validation. Only two source edits: inputPoller(void*) callback signature,
and PASS log redaction. License GPL-3.0, complete source/patch kept in
D:/autorun fork/references/sys-ftpd; binary and installation record outside repo.
Fixed heap is 0xA7000; one FS session. An upstream PORT uninitialized-pointer
compiler warning remains; this client uses passive FTP, not PORT.
Binary SHA-256: 0b7eeaf141a985987b26e422fe0564e1c90341b3581f5587e9c4b99cc20ccead.
Binary and config were verified by readback before boot2.flag was installed.

Exactly two settings added in the existing [atmosphere] section:

```ini
enable_standalone_gdbstub=u8!0x1
enable_htc=u8!0x0
```

Original config preserved on PC and as
/atmosphere/config/system_settings.ini.remote-backup-20261005 on SD. Original
live file also remains as system_settings.ini.remote-old after verified rename.
No firmware, payload, loader, runtime, game, prefix or account/session upgrade.

After the next user reboot, initial FTP :5001 and GDB inventory checks failed;
both returned connection timeouts/failure, and previously working sys-botbase
also timed out. The user reported Atmosphere error/reboot when connection checks
were attempted. No successful persistent FTP/GDB session has been established.
User photograph subsequently identified result 0x10801 (2001-0132), Program ID
0100000000000016, Horizon 21.1.0 and Atmosphere 1.11.2-master-b918458b3-dirty.
The [libnx error definition](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/result.h)
maps description 132 to KernelError_OutOfResource, and the
[Switchbrew title list](https://switchbrew.org/wiki/Title_list) identifies this
program as wlan. Precise exhausted resource and causal service remain UNKNOWN.
This is a system Wi-Fi service fatal error, not an established Wine/game failure.
Do not attribute it to either service without that evidence. All further network
probes stopped. A guarded Hekate SD USB rollback script is prepared outside Git:
artifacts/remote-switch-setup/rollback-background-access.ps1. It verifies this
SD's sys-botbase identity and expected config hash before restoring the original
config and removing only sys-ftpd's newly added boot2.flag. Rollback awaits the
user mounting SD via Hekate. It was subsequently performed and verified below. Existing complete
source, binary, records and backups are retained. Capture a saved crash report
before retrying services separately. Do not deliberately repeat a boot loop.

Persistent FTP/GDB activation is therefore FAILED / UNVERIFIED, not complete.
After rollback/recovery, validate one service at a time. An existing LAN diagnostics
response and pre-change screenshots are not evidence that new channels work.
Probe launch/update/relaunch, debugger attach and crash/disconnect recovery remain
pending. No overnight or sustained unattended runner has been configured.

Prepared direct commands — do not run until isolated activation succeeds:

```powershell
$env:AUTORUN_FTP_CREDENTIALS='D:\autorun fork\artifacts\remote-switch-setup\ftpd-credentials.dpapi'
python autorun/tools/remote_switch/broker.py --host 192.168.70.59 ftp-logs
python autorun/tools/remote_switch/broker.py --host 192.168.70.59 stage-test path/to/test.nro --ftp-port 5001
python autorun/tools/remote_switch/broker.py --host 192.168.70.59 gdb-processes
```

The --ftp-credentials global option can supply the encrypted file path instead
of the environment variable. GDB currently only lists processes then disconnects;
no system attach, register/memory write, RAM dumps or cheats. Next debugger work
must identify the independent probe PID first. FTP logs uses 16 KiB per tail,
max 32 allowed log filenames, and strips sensitive/partial lines. Stored game
output can still be private; inspect reports before sharing.

Rollback: remove only the new sys-ftpd boot2.flag, restore the saved original
system_settings.ini to disable GDB, and reboot. Hardware boot failures can be
repaired through the user's working Hekate SD USB path. DPAPI credential storage
is owned by this Windows user. sys-botbase can stay installed independently.

## Completed rollback and safe SD eject

User reported the crash also occurs about five seconds after boot without Wi-Fi
and mounted Switch SD as G: through Hekate USB Mass Storage. This means a network
connection attempt is not established as the trigger. The guarded rollback ran:
original system_settings.ini restored, sys-ftpd boot2.flag removed, sys-botbase
boot2.flag and binary preserved. Restored config SHA-256 matches original exactly:
a5b6c10f3d3e241b735f7768c619d2c410a0dde3ebf5e8873c18fa8988ec088c.
The installed disabled sys-ftpd binary/config are retained for isolated study;
do not re-enable them as part of automatic recovery. Both new channels remain
unvalidated. Source and test artifacts are preserved; Autorun/game data unchanged.

Windows Shell Eject succeeded and G: was verified no longer ready before the
user was told to disconnect USB. Rollback result saved outside Git in
artifacts/remote-switch-setup/rollback-result.json. Selected recent text report
headers did not identify this WLAN fatal, so no raw binary/RAM dumps were read.
The user's photograph is the available failure evidence. User subsequently
confirmed stable HOME with Wi-Fi; sys-botbase 2.5 again replied successfully,
with no application title, in run 20261005T125307Z-42387fac.

## Direct file channel experiment

tools/remote_switch/sysbot_extension overlays the exact official sys-botbase
v2.5 commit 45b6a6419624ed1a1f34aabc9f37336d5f130e61. Build with the pinned
existing devkit container after running prepare.py against that checkout.
The extension keeps port 6000 and adds no thread. One optional FS session and
the SD mount are opened on demand and closed at the end of each file command;
FS failures return an error rather than invoking fatalThrow. Startup services,
heap size, thread count and boot configuration are unchanged. This reduces
added persistent resources; it does not establish the cause of the WLAN fatal.

Commands: direct-probe, direct-logs, direct-stage-test NRO,
direct-activate-test SHA256. Logs are fixed-directory saved tails of at most
2 KiB each, with PC-side redaction. Transfers are limited to an independent
2 MiB NRO under /switch/autorun-remote-test. Staging uses an exclusive digest
filename and sequential 2 KiB chunks; client reads back every byte and verifies
SHA-256. Activation independently hashes the file on Switch, checks NRO magic,
rejects a running application or hbloader applet, preserves the previous target
under its digest, and restores it if the final rename fails. It does not update
Autorun, games, system configuration or arbitrary SD paths. Interrupted stages
remain on SD and are not automatically overwritten or retried.

The original sys-botbase protocol and this extension have no authentication;
use only the already authorized trusted LAN. The original binary must be kept
on PC and SD before installing the overlay. Both compilation and 22 host tests
passed. Installed via the existing DBI FTP with verified read-back, preserving
the official binary on PC and SD as exefs.official-v2.5.nsp. Installed binary
SHA-256: de12437125e80f4d0eaae9a0ecf658f2b3aef27d6c1cd22201d34d4aa5196cc4.
Hardware validation on 2026-10-05: user confirmed about ten minutes of stable
HOME after reboot. arVersion returned 1. Direct collection received 15 saved
logs (2 KiB tails); autorun_runtime.log remained unavailable. Independent v1
and v2 NROs, each 196608 bytes, were transferred and fully read back without DBI.
v1 hash ff67a42cc9c44f903856ac958bf814ec0513a87cb4a0a78d1a53e8e2ee6f69a6;
v2 hash c6bf6837295db6a024855fa93381b0bfae88d74ba6570c19ebecbe5cda596eac.
v2 activated from HOME; v1 target was retained under its hash. Relevant runs:
20261005T131825Z-31ca476a (logs), 20261005T131844Z-d29e3645 (v1 staging),
20261005T132001Z-c98aeddc (v2 staging), 20261005T132045Z-2c6e8e7f (activation).

Launching v2 via the installed hbmenu HOME forwarder 03DB12780BD84000 closed
that application with an error. Selected new textual report
01791206596_03db12780bd84000.log showed User Break reason 0, result 0,
PC hbl+0x3a28, LR hbl+0x367c. These facts do not establish the exact cause.
No system components were changed. DBI was temporarily used for bounded
textual metadata reading, then FTP stopped and DBI exited.

The identical v2 NRO successfully launched via Album R+A, with hbmenu 3.6.1,
nx-hbloader 2.4.5, Applet Mode. Screen confirmed probe v2; A reported marker
write with fclose result 0 (marker contents were not independently read back).
PLUS returned to hbmenu. A test replacement attempt while hbmenu applet was
open was rejected, run 20261005T133516Z-9db9eb6d. The extension still replied
in 20261005T133557Z-e5b20f9e. PLUS did not close hbmenu; B moved from /switch
to the SD root. HOME clicks at both 50 ms and a temporary 500 ms did not return
to HOME; duration was restored to 50 ms and the unsuccessful timing workaround
was removed from code. Final observed screen is stable hbmenu applet at SD root,
run 20261005T133908Z-17b2bf52. Do not treat HOME input as a verified exit action.
Probe applet success is not Autorun runtime
validation: continue launching Autorun only through its dedicated HOME path.

PC album-homebrew action sends only the fixed R+A launch chord, holds R for
approximately 1.35 seconds plus transport latency, then attempts release and
detach in nested finally blocks. It requires manually verified Album selection
on the preceding screenshot; title None alone does not identify HOME or exclude
an applet. No general hold or arbitrary sequence CLI is exposed. Cleanup can
fail on a broken network and is not a module-side watchdog. The host suite now
has 23 passing tests, including cleanup after a simulated click failure.

This is saved-log and separate test-file access, not a working GDB debugger,
live stack tracing, generic SD access, or direct replacement of production Autorun.

## Verified HOME workaround

Subsequent source research found upstream's UNUSED button workaround. Sending
UNUSED followed by HOME on the same virtual device, detaching only afterwards,
returned from both hbmenu applet and Autorun to HOME. Autorun was then closed
through the visible HOME X/A confirmation and title absence was verified.
This supersedes the earlier unsuccessful HOME observations, but does not prove
recovery from an actual game/GPU/kernel hang. Details, primary sources and the
scoped resource-update design: tools/remote_switch/RECOVERY_AND_DEPLOY_RESEARCH.md.
No additional console module/configuration was installed for this workaround.
