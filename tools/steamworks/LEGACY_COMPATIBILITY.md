# Local SDK legacy extensions — 8 October 2026

The generated factories now include `SteamUser008`, `SteamFriends004`, and
`SteamClient007`. Each is a separate class implementing its own pinned header;
none aliases a newer interface pointer. GBE ABI reference revision remains
`7103add7ca6ef3ef8353279219880068f2494fc8`, with headers
`sdk/steam/isteamuser008.h`, `isteamfriends004.h`, and `isteamclient007.h`.
Architecture-specific preprocessing retains the struct-return slots declared
by the reference macros. User versions below 008, Friends below 004, and Client
below 007 remain unavailable: their older account/client state is not implemented.

`GetUserDataFolder` returns an existing account/AppID-scoped local directory.
It rejects null, zero-sized and undersized buffers and clears the first byte of
a writable insufficient buffer. This is the same namespace used by local
RemoteStorage. RemoteStorage's leaf-name validation, per-file limit, quota and
atomic replacement remain unchanged. Games writing directly to the returned
directory use ordinary filesystem I/O; the RemoteStorage API quota is not a
filesystem quota, and cannot enforce limits on those direct writes.

`GetAPICallFailureReason` now returns `InvalidHandle` for an unknown/expired
call and `None` for a retained successful result, rather than defaulting to
enum zero (`SteamGone`). No network authentication or ticket is generated.
Legacy connection authentication returns failure and leaves the caller's blob
unchanged. The session remains offline.

The synthetic probe executes our DLLs only, with no game EXE. It covers both
x86 and x64, legacy client-to-user lookup, old Friends struct-return slots,
identity/offline state, authentication failure, directory buffer boundaries,
and the existing durable save/callback/context checks. Build and check commands:

```text
wsl -e python3 "/mnt/d/autorun fork/autorun/tools/steamworks/build.py"
python autorun/tools/steamworks/check.py
```

Successful local validation does not establish that Assassin's Creed's genuine
legacy Steam.dll client path is available, or that the SDK fixes its launch.
These payloads require the runtime's guarded hash migration before deployment.

## Separate explicit local client flavour

`local.cpp` also supports a separate `AUTORUN_LOCAL_CLIENT_ADAPTER` build.
It shares the backend and exact generated interfaces with the normal SDK;
there is no copied implementation. Its filename is `steamclient-local.dll`
(x86) or `steamclient-local64.dll` (x64). These files are clearly identified by
the export `AutorunLocalClientAdapterRole`, returning
`autorun-local-offline-client-adapter-v1`. Only this role exports the cdecl
`CreateInterface(const char *, int *)` factory, for `SteamClient023` only.

Every initialization entry point requires trusted sibling configuration:

```ini
[autorun]
local_client_adapter=1
app_id=480
steam_id=76561198000000000
install_dir=C:\absolute\actual\owned\installation
```

The values above are synthetic probe settings. Production configuration must
come from the actual installation metadata, not a guessed ownership/account.
AppID must be decimal, nonzero, in uint32 range; SteamID must be decimal and
valid Public/Individual. A relative or UNC install directory is rejected.
`install_dir` must canonicalize to an existing local drive directory without
a final reparse point. It is independent from the component DLL directory.
Configuration changes cannot silently retarget an initialized context.

The role allocates monotonically increasing process-local handles, invalidates
them on release/shutdown, rejects stale handles and wrong interface families,
and supports one active pipe. The Client023 getters provide only exact
User023, Utils010 and Apps009. These handles represent local SDK state, not
an actual client IPC connection. `BLoggedOn` and `SteamAPI_IsSteamRunning`
return false. Subscription/ownership/license results are unavailable in this
role; GetAuthSessionTicket fails, sets the output size to zero and leaves the blob alone.
`GetAppInstallDir` returns only the configured AppID's actual install directory
with checked buffer bounds. The normal SDK's client role/factory visibility
and ownership policy remain separate.

Historical ABI caution: pinned current `isteamutils.h` is Utils011; its
GetAppID is slot 8. Genuine legacy Steam.dll asks for Utils010, whose GetAppID
is slot 9. Use the exact `isteamutils010.h` pointer type to call this interface.

Primary semantic references:

- [Valve ISteamClient](https://partner.steamgames.com/doc/api/ISteamClient#CreateSteamPipe)
- [Valve BLoggedOn](https://partner.steamgames.com/doc/api/ISteamUser#BLoggedOn)
- [Valve GetAppInstallDir](https://partner.steamgames.com/doc/api/ISteamApps#GetAppInstallDir)

The explicit offline handle model is a limited approximation; these documents
do not establish an actual Valve client/authenticated session for this mode.

Build/test the extra role into a separate experimental output:

```text
wsl -d AvatarLab -e python3 "/mnt/d/autorun fork/autorun/tools/steamworks/build.py" --output "/mnt/d/autorun fork/artifacts/steamworks-experimental-legacy-client-20261008" --with-client-adapter
python autorun/tools/steamworks/check.py --output artifacts/steamworks-experimental-legacy-client-20261008
```

The build refuses to emit an adapter into the deployed SDK4 artifact directory.
SDK4 DLLs, probes, manifest and source were archived before integration in
`artifacts/steamworks-sdk4-archive-20261008`. The experimental output contains
separate normal/adapter DLLs and separate probes for both architectures.
All four own PC probes passed, including invalid/missing configuration,
role/factory visibility, stale handles, exact Utils010 AppID, actual install
directory, insufficient buffers, ticket failure and shutdown/reinitialization.

The prior isolated prototype passed genuine unchanged Steam.dll's
SteamStartup -> SteamGetCurrentAppId(480) -> SteamCleanup twice in one process,
in network-disabled Wine. See
`artifacts/legacy-steam-20261008/adapter-pc-results/lifecycle.log`.
The shared candidate needs its own genuine lifecycle evidence before runtime
provisioning. Nothing here establishes an Assassin's Creed launch or supplies
Steam authentication, encrypted tickets, DRM/WinVerifyTrust hooks or EXE patches.
