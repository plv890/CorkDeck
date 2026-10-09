# Verified navigation on this bench

Latest state and full routes:
[handoff control guide](../../docs/handoff/2026-10-07/REMOTE_CONTROL_AND_TELEMETRY.md).
Last verified screen is DBI FTP Browse SD. The last clean Autorun carousel selected
The Forest, so an old remembered `A = Dishonored` assumption is invalid.

User reaffirmed 2026-10-07: reuse known button/count routes with title guards;
request/analyze screenshots only for unknown state, mismatch or visual acceptance.
Allow slow games 5–10 minutes to produce an image before judging startup.
Prefer log-only observations during that period; a temporary network timeout
alone is not a game failure. Do not repeat an already delivered Play action.

User preference (2026-10-05): remember key order/count and run stable routes in
one batch. Inspect starting and final screens; inspect intermediate screens only
when state differs or a route fails. Do not infer a new game's menu sequence.

DBI 905-ru, main menu **first row selected**:
`Down x12 -> A -> Down x2 -> A` opens FTP **Browse SD** on port 5000.
From that FTP screen, `B` stops FTP and returns to the main menu. This is distinct
from NAND, saves and game-install FTP modes. Only open FTP within user authorization.
After `B`, the FTP main-menu row remains selected: `A -> Down x2 -> A` reopens
the same Browse SD mode. At initial boot wait for the network rows to appear.

Active title (including Autorun/DBI/game): `HOME -> X -> A` closes it. HOME uses
the verified UNUSED priming workaround. The active title is first on HOME; title
guards still apply. A clean HOME with no title is the required final state.

Autorun Home: `Minus -> Right -> Down x3 -> A` opens Steam settings when Library
is the selected settings section and its first row is selected. Settings remember
selection, so confirm this starting state before reusing that route.

```powershell
python tools/remote_switch/broker.py --host 192.168.70.59 --timeout 15 navigate dbi-ftp-sd --expected-title 03DB117EE17B6000 --start-confirmed
python tools/remote_switch/broker.py --host 192.168.70.59 --timeout 15 navigate close-title --expected-title 0548EABB35576000 --start-confirmed
```

The broker captures only before/after images for a route, checks title before each
input and detaches in finally. These are fixed bounded sequences, not visual menu
detection; `--start-confirmed` is an explicit assertion about the starting screen.
FTP/QR screenshots are private evidence and must not be published.

Autorun modal transitions need 0.75s settling after A/B/Minus; list movement uses
0.25s. Faster B sequences can be swallowed during animations and do not reliably
reach the next screen. HOME arrow/A works with ordinary `click`; on this bench
repeated ready-attach/detach arrows sometimes leave the old selection.

Autorun Home, selected game card, no modal: `Up -> Right x2 -> A` opens the
Steam library through the header. Library `L` cycles backwards:
Downloads -> Installed -> All games. These routes were confirmed on 2026-10-06.

Wait at least 2 seconds after closing a title before the next HOME selection.
Recent software order changes when DBI is launched: inspect the final HOME state
once before choosing Autorun. A fixed Left from DBI is not a verified launch route.
Closing DBI on this bench leaves DBI first and Autorun second: `Right -> A`.
Closing Autorun leaves Autorun first: `A`. Confirm the starting selection once.

Steam settings with successful sign-in, Sign in row selected: `Down x6` reaches
LAN diagnostics (disabled Unlock is skipped), `A -> A` enables it and opens URL.
From that URL modal, `A -> B x3`, wait 2 seconds, returns Home. The home carousel
sorts by last-play time, including failed launches; confirm the selected game
before Play, and do not reuse its index from an earlier session.
The attempted PLUS+MINUS game-return chord only opened Ninja Options on this
bench; it was removed from broker presets until runtime input behavior is proven.
