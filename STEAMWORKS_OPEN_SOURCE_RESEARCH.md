# Open Steamworks implementation options — researched 2026-10-06

## Decision

### Accepted scope after user clarification

User does not need network gameplay and puts cloud saves at most second.
Choose a source-built local Steamworks PE layer based on Goldberg/GBE as the
first implementation candidate. It is a third-party LGPL implementation, not
an official Valve client or endorsed component. Prefer existing PE x86/x64
execution over introducing a native Android/Linux client host. This is the
expected smaller integration, not a measured performance result.

First build configuration: no overlay, LAN/multiplayer backend, cloud traffic
or additional Steam process; retain local RemoteStorage, callback dispatch,
interface versions and necessary User/Apps/Utils/Stats behavior. Network APIs
must report their actual unavailable state coherently. Bind launch to the
existing account/owned installation context, with no guest vault tokens.
Preserve original DLLs and local saves, with reversible deployment. Compare
GBE API versus client mode using a test probe before choosing the DLL injection
point. Do not claim client mode can preserve every original SDK unchanged.

Use LGPL-3.0-or-later attribution/source/rebuild requirements for incorporated
GBE code and track separate dependencies. Cloud and authentic native client
hosting are deferred; they are not prerequisites for this local first stage.

Official ARM64 client availability was verified directly against Valve's public
beta manifest, not inferred from community screenshots. Snapshot saved as
../artifacts/steamworks-research/valve-linuxarm64.manifest (12,582 bytes),
version 1791249696, SHA256
b5922fbeb3d869d8c2992e939b10e146ac7c6eb24a8d5d356802dcd998fbf97e.
It contains bins_linuxarm64, bins_linuxarm64_linuxarm64 and
bins_androidarm64_linuxarm64, each with package size and SHA-256.
https://client-update.steamstatic.com/steam_client_publicbeta_linuxarm64
Only the manifest was downloaded; no Valve package was installed or executed.
This confirms published ARM64 packages, not a release date, Horizon support or
that Android/Linux variants have interchangeable ABIs.

The genuine-client path therefore does not inherently require x86 translation
of the client core. Its next feasibility gate is a dependency/import inventory
for the chosen native package and the smallest headless client host, before
considering a full UI/CEF environment. Horizon needs the relevant binary loader,
libc/TLS/threading, filesystem, socket/IPC and lifecycle support. This gate is
worth investigating alongside the local compatibility route; neither the
existing CM session nor an open proxy alone replaces that native host.

GameNative's closed bootstrap does not make an independent implementation
impossible. There are open client bridges and independent API implementations.
For Horizon the practical first route is a shared game-facing PE compatibility
layer with local storage/callbacks, backed by Autorun's account/AppID context.
An authentic Valve client bridge is a distinct, longer route because its host
runtime is missing. Neither route is implemented or hardware-validated here.

## Source comparison

| Project | Actual available implementation | Use in Autorun | Remaining blocker |
|---|---|---|---|
| moio9/steam-arm64-termux | MIT project-authored socket protocol/server, Proton patches, RemoteStorage and callback dispatch | Best concrete reference for genuine-client marshalling | Requires authenticated glibc Steam plus dlopen, Unix sockets/process runtime; not standalone login/bootstrap |
| ValveSoftware/Proton lsteamclient | Versioned Windows-to-Unix Steam ABI conversion | Interface layouts, x86/x64 conversions, callback packing | Still needs native steamclient backend; component and SDK licensing must be tracked separately |
| Detanup01/gbe_fork / alex47exe/gse_fork | LGPL-3.0-or-later independent Steam API/client emulation, local saves, callbacks, multiple old interfaces | Most plausible reusable core for a local PE compatibility backend | Stock emulation is not real Steam authentication/cloud/multiplayer; ownership and account semantics need adaptation |
| Droid-Deck/DroidDeck | GPL-3.0 Android environment around real Valve ARM64 Steam/Proton | Runtime dependency inventory and session management | Linux/Android host, compositor and process environment, not a Horizon SDK backend |
| huntergdavis/steamclienttermux | Public reproducible client environment using patched PRoot/glibc | Original-client bootstrap and runtime dependency reference | Linux environment still required; no independent client core |
| xXJSONDeruloXx/steam-droid | JNI steamservice loader and analysis journal | Examples of symbols/loader failures | Incomplete scaffold with stubs, not proven game SDK login; no LICENSE/COPYING found in inspected checkout |
| SteamRE/open-steamworks | Old reverse-engineered headers | Historical ABI reference only | Not a client implementation; upstream describes it as largely abandoned |
| SteamKit / Autorun CM provider | Network authentication/library/protocol implementation | Account, licensed library and future service backend | No game-facing C++ interface/vtable/callback implementation by itself |

GameNetworkingSockets supplies networking primitives, not the entire Steam
client/user/storage API. Box64/FEX provide CPU translation, not those APIs.
OpenSteamClient describes itself as partially open source; the inspected root
README is essentially a redirect, not evidence of a fully open backend.

## Inspected concrete sources

Downloaded source only, without executing upstream scripts, installing packages,
fetching Valve binaries or changing console state:

- ../references/steam-arm64-termux, commit
  9782c5154cf6217661b05bed30b160556f8c7885.
  LICENSING.md distinguishes MIT bridge/shims, glibc LGPL, Proton/SDK terms and
  download-only Valve components. server.cpp dlopens steamclient, resolves
  Steam_CreateSteamPipe / Steam_ConnectToGlobalUser / CreateInterface and callback
  exports, then obtains ISteamRemoteStorage and other real interfaces. It
  attaches an existing authenticated client; it does not log in our CM token.
  protocol.h and the Proton patch expose bounded serialized requests across
  AF_UNIX/SOCK_SEQPACKET. RemoteStorage uses chunked messages but allocates up to
  a full 64 MiB file; use bounded streaming on Switch instead. Its write handler
  checks range bounds but does not itself track full chunk coverage: do not
  transplant it as a crash-safe transaction. The patch contains multiple legacy
  SteamClient layouts, demonstrating why one shared vtable is insufficient.
  Upstream's gameplay claims are not independently verified on our hardware.
- ../references/steam-droid, commit
  2a7a2f4a78fcc42316799fe121dbcca63c7fe7e8.
  app/src/main/cpp/steam_bridge.cpp loads steamservice.so and provides JNI entry
  points; steam_api_stub.cpp supplies placeholder interfaces. No game-facing
  authenticated Steamworks backend was established by this source inspection.
- GBE upstream dll/dll.cpp, steam_client.cpp, steam_remote_storage.cpp and
  steam_apps.cpp inspected through upstream raw sources. Client creation builds
  real emulated storage/callback objects; old interface versions are selected
  explicitly. BIsSubscribed() in stock steam_apps.cpp returns true, so its
  account/ownership behavior cannot be mistaken for our authenticated provider.
  Use only an adapted backend for owned installations, with clear local vs
  real-service capability states. No emulator DLL was deployed.

## Implementation sequence for our project

1. Extract required interface revisions from game SDK binaries and model a
   shared capability table. Imports alone are insufficient. Keep x86/x64 and
   legacy interface layouts separate; record unsupported versions in diagnostics.
2. Evaluate source-built GBE PE API/client modes against the existing Wine toolchain.
   API replacement is simpler but needs reversible deployment; client mode may
   preserve original steam_api but has additional discovery/registry requirements.
   Choose only after a probe proves original API initialization and vtable calls.
3. Implement local account/AppID-bound saves and callback lifecycle first, with
   test clients and failure injection on PC. Do not move plaintext vault tokens
   into guest configuration. Local saves must remain durable and isolated per
   account/game; absent cloud service must not be reported as uploaded.
4. Connect supported backend methods to Autorun's native CM service gradually.
   Ownership, cloud, stats and tickets are different services; library presence
   does not supply a real encrypted ticket or all online APIs.
5. Test Dishonored and Balatro after the common backend meets the probe contract.
   Their different failures exercise RemoteStorage and SDK init. Do not introduce
   game-specific NULL-pointer patches or assume all games are then compatible.
6. Longer-term authentic-client option: provide a native binary loader/runtime
   and host lifecycle, or implement additional service APIs ourselves. Merely
   porting the MIT socket transport will not create the missing Valve client.

This research supersedes the assumption that the only useful bootstrap reference
is GameNative. It does not change the installed Switch build (2026.10.06-8).

## Primary links

- https://github.com/moio9/steam-arm64-termux/tree/main/steam-bridge
- https://github.com/moio9/steam-arm64-termux/blob/main/LICENSING.md
- https://github.com/moio9/steam-arm64-termux/tree/main/public-source/lsteamclient
- https://github.com/ValveSoftware/Proton/tree/proton_11.0/lsteamclient
- https://github.com/Detanup01/gbe_fork
- https://github.com/alex47exe/gse_fork
- https://github.com/Droid-Deck/DroidDeck
- https://github.com/huntergdavis/steamclienttermux
- https://github.com/xXJSONDeruloXx/steam-droid
- https://github.com/SteamRE/open-steamworks
- https://github.com/SteamRE/SteamKit
- https://github.com/ValveSoftware/GameNetworkingSockets
- https://github.com/OpenSteamClient/OpenSteamClient
