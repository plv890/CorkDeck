# Startup and live saved-log diagnostics

Latest checkpoint and limitations:
[control/telemetry handoff](../../docs/handoff/2026-10-07/REMOTE_CONTROL_AND_TELEMETRY.md).
Native 2026.10.07-8 keeps passive performance/wait diagnostics;
`stall-snapshots.txt=0` independently disables forced thread snapshots.
The sys-botbase file channel now uses one FS worker to keep control responsive.
The last Autorun process is closed, so its previous HTTP key has expired.

Native `2026.10.06-11` adds bounded passive timing throughout the game session through the same service:
authenticated `GET /perf`, `POST /perf/start`, `POST /perf/stop`, and `[PERF]`
lines in closed snapshots. See [PERFORMANCE_DIAGNOSTICS.md](../../PERFORMANCE_DIAGNOSTICS.md)
for activation, exact sampling scope and limitations. No new Switch module is required.

Revision 2026.10.05-3 adds authenticated `GET /live` to the existing in-app LAN
diagnostics on port 8181. It copies the current 16 KiB ring under a try-lock and
then releases the lock before filtering and sending. It does not open or flush
an SD log. The memory ring is maintained even when optional SD snapshots are
disabled. Enable the existing temporary service in Settings → Library → Steam
settings → LAN diagnostics. Its random key survives the networking reset before
Play, expires on application exit, and is never added to reports or Git.

```powershell
python tools/remote_switch/broker.py --host 192.168.70.59 --lan-key-file <DPAPI-file> live-logs
python tools/remote_switch/observe.py --host 192.168.70.59 --game "Catlateral Damage" --source http --lan-key-file <DPAPI-file> --seconds 600
```

Alternatively supply `AUTORUN_LAN_KEY` in the local environment. The DPAPI file
contains only the current debug key, encrypted for the owning Windows user.
The existing sys-botbase closed-snapshot path remains available without enabling
HTTP and includes the same numeric fault summary.

Explicit recognized failures latch `fatal_kind` until a new program starts or
the runtime exits: 1 GC fatal, 2 rendering-thread exception, 3 DLL initialization
aborted, 4 unhandled exception, 5 native `[FAIL]`, 6 other fatal dialog. This is a
diagnostic flag, not proof the process exited; ordinary err/fixme/stall messages
do not set it. The observer polls every two seconds by default, requires an
advancing console sample before accepting a retained fault, saves its evidence
and stops waiting without sending input. Screenshots are at most every 30 seconds
except the final/error capture. Black alone keeps the verdict Unknown.

Hardware proof: Catlateral's GC error was detected by HTTP at game elapsed 11s,
fatal_age_s=0, and remained latched at 107s/age 96s. No DBI or app exit was needed
to read it. The observation folder includes initial menu time; its overall 114.5s
duration must not be confused with the 11s game startup interval.

The native runtime optionally mirrors its most recent 16 KiB of existing log
lines to two alternating closed files in `/switch/wine/logs`. This avoids the
SD sharing restriction that prevents sys-botbase from opening the live writer.
Enable with `/switch/wine/remote-snapshot.txt` containing `1`; remove it or write
`0` to disable on the next runtime launch. No sysmodule reboot is needed.
Snapshots reuse the existing flusher thread, at most once every two seconds
when log content changes. Copies happen under the existing mutex; SD writes
happen outside it. Default is off. Performance must be checked on hardware.

```powershell
python tools/remote_switch/broker.py --host 192.168.70.59 direct-logs --live
python tools/remote_switch/broker.py --host 192.168.70.59 click A --layout xinput --ready --expected-title 0548EABB35576000
python tools/remote_switch/broker.py --host 192.168.70.59 ftp-logs --ftp-port 5000 --file BeatHazard.log --whole
```

The original installed file extension can read the closed snapshot tails.
Collection records run identity, sequence and console tick from the footer.
One slot may be unavailable while being written; compare both, group by run
identity, then choose the greatest sequence within that run. Old slots may
remain after exit or when diagnostics are disabled: receiving bytes alone
does not prove the application is alive. Observe changing sequence/ticks and
counter progression together with process/title and screenshots. The installed
extension returns only 2 KiB tails: high-volume stdout may displace the startup
heartbeat between reads. Revision 2026.10.05-2 puts a compact `[REMOTE-STATE]`
summary immediately before the footer; the observer prefers it over old log
heartbeats. A missing heartbeat means unavailable evidence, not a stage reset.
Collect the complete saved game log through DBI after closing.

The native startup stages are preparation, translator selection, DLL loading,
guest/DLL initialization, graphics initialization, observed presentation.
Startup text appears on the console's owning thread before the graphics backend
takes the screen. This is not an overlay over a stalled graphics backend.
The log reports total and stage elapsed seconds every ten seconds.
`graphics_presents` counts GL swap/Vulkan present calls, including unsuccessful
attempts. Revision 2026.10.05-2 separately records
`graphics_successful_presents`: EGL returns success or Vulkan returns SUCCESS
or SUBOPTIMAL for a nonempty presentation. Stage 6 now requires this success
counter. Revision 2026.10.05-1 used attempted presentations for stage 6.
API success still does not prove a useful game scene. `window_frames` counts
Wine framebuffer output and may include a cursor. Neither counter proves a
visible usable game scene. A black screen or long initialization is not an
automatic failure; long-running titles should receive at least ten minutes.

Game controls use positional XInput: Xbox A→Nintendo B, B→A, X→Y, Y→X.
HOME and launcher dialogs still use Nintendo labels. `--ready` attaches a
device before the press, waits one second, holds for 200 ms, then releases and
detaches in nested finally blocks. Normal short clicks remain the default.

Explicit complete saved-log reads accept up to four `.log` names in the fixed
logs folder, at most 512 KiB each, with redaction before local persistence.
Default FTP collection remains a 16 KiB tail. Do not confuse older saved game
logs with the current run, or load failures for optional fallback paths with
required dependency failures.

For a bounded read-only observation:

```powershell
python tools/remote_switch/observe.py --host 192.168.70.59 --game "Long Live Santa" --seconds 600
```

Verify the selected game before starting.
The observer saves changing closed log tails and periodic screenshots, sends no
input and never converts a black screen into a failure verdict. Its game label
is operator context, not automatic game detection.

A mouse cursor proves that part of the input/window path is working, not that
the engine has initialized. Diagnose required-DLL loading, DLL initialization,
graphics and Steam API failures separately. Valve documents that Steam API
initialization can fail when the client is not running, the App ID is unknown
or the account has no license. The API DLL alone does not provide all client
interfaces: https://partner.steamgames.com/doc/sdk/api#SteamAPI_Init . Check the
current game's evidence before attributing its failure to Steam; obtain missing
redistributables from the game's legitimate installation or official vendor.
