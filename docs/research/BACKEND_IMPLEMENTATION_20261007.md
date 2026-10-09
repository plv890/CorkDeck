# Backend implementation and testing — 2026-10-07

<!-- migration-checkpoint-20261007 -->

Актуальное состояние и итоговые результаты: [пакет передачи следующему чату](docs/handoff/2026-10-07/README.md).
Этот документ сохраняет последовательность этапа; его прежние current/HOME/policy/GDB-pending формулировки относятся к моменту записи и не переопределяют последний checkpoint.


## Screenshots compatibility

Implemented exact Screenshots001/002/003 interfaces from pinned GBE headers
`7103add7ca6ef3ef8353279219880068f2494fc8`. Correction to the research report:
legacy headers were already present in the reference; the prior search missed
them. No new external source was fetched/copied.

Generated legacy and flat factories, legacy SteamScreenshots export and
launcher inference from preserved original SDK. Hook state is shared across
versions, protected by the existing mutex and reset by Shutdown. Unsupported
capture/library/tag operations return unavailable/invalid handles. Trigger has
no overlay/capture side effect; no screenshot-request callback is synthesized.

Synthetic x86/x64 probes passed: exact interfaces through factory/client,
cross-version hook state, failed unsupported writes, shutdown reset plus the
existing ownership/storage/callback checks. Host native preparer ASan/UBSan
passed original-version inference, backup/recovery, idempotence and unknown-mod
conflict preservation. Previous two own SDK hashes stay allowlisted.

Installed coherent runtime/SDK revision 2026.10.07-2 with staging/final readback.
Journal: `artifacts/backend-implementation-20261007/installation.json`.
Matching NRO/ELF retained there. Both SDK DLLs and component build.json have SD
backups `.before-screenshots-20261007`; matching -1 runtime/manifest backups use
the same suffix. No game EXE, save, original SDK or vault was overwritten.
Adjacent own SDK migration is performed by the existing guarded preparer on
launch; target readbacks and hardware outcomes are recorded below.

Current SDK x86: 466,432 bytes, SHA256
`d9aec7f1d5452d142ba1a6eb9ffa709f2e53caa93c0e8ede5ecc562d7a24f9f7`.
x64: 442,880 bytes, SHA256
`fa96bd051b9c74ebb753f786309dbbfb3a9e7c9236ad430bbc8869eb3f1e7010`.
Synthetic results: `artifacts/steamworks-minimal/check-20261006T222402173307Z/results.json`.

## Planned performance comparisons

Dishonored's actual retained logs select ordinary DXVK 3.1.1, unlike Ninja's
GPLAsync 3.1.1-1. Upstream 3.1.1 source documents GPL True's base-library path
and suppression of background optimized pipeline jobs. Verify actual target
payload/config and effective options before comparing. Fix compiler count at
three across CPU policy comparisons; otherwise mode 3 automatically creates
four workers, as already observed.

Changes to game-adjacent dxvk.conf must be backed up byte-for-byte and restored
after each experiment; preserve manual settings and caches. Successful presents
and startup alone do not establish a speedup. Use the same visible scene/route,
record wall/CPU timing, heap and frame pacing, wait 5–10 minutes for startup,
and acknowledge any clock/scene/diagnostic-overhead confounds.

FEX TSO remains 1. No JIT option or hardware ISA override changed. Missing
FEX checkout/ARM64 execution prevents certain core fixtures on this host; those
are not claimed passed. Native pipeline stage/queue instrumentation remains
separate work from this SDK compatibility change.

## Follow-up callback ABI fix — revision 2026.10.07-3

The -2 target passed STEAM INITIALIZED and reached graphics, then terminated
with c0000005 inside SteamAPI_RunCallbacks. Full saved log:
`20261006T222843Z-da7cb447/10SNX.log`. The user confirmed no physical input.
The process went to HOME because teardown retained GPU-lent pages and could
not safely return to the launcher. This is not a passed -2 hardware acceptance.

The local SDK called foreign callbacks through MinGW's C++ overload vtable.
Windows/MSVC reverses the two overloaded Run slots; calling the wrong slot
corrupts x86 argument cleanup. The pinned GBE reference explicitly documents
this overload issue in steam_client.cpp and callback_wrapper.h. A synthetic
foreign/MSVC-layout callback reproduced wrong-overload delivery on x64 before
the fix; the previous same-compiler callback probe had missed it.

Implemented explicit Windows ABI dispatch: slot 1 for ordinary callback, slot 0
for the three-argument call-result method, with thiscall on x86. Callback
registration now maintains registered flags/id, unregister clears the flag,
and callback versus call-result registrations are not removed indiscriminately.
Shutdown clears registered callback flags. No callback suppression or fake
success was used. API behavior remains local-only and bounded.

Foreign-layout x86/x64 probes and preparer ASan/UBSan passed. Results:
`artifacts/steamworks-minimal/check-20261006T223522975547Z/results.json`.
Installed coherent -3 runtime and SDK with readback/rollback journal under
`artifacts/backend-callbacks-20261007`. All three earlier own SDK hashes remain
allowlisted; original payloads/settings/saves are preserved.

Native -3 SHA256 `0718598fa8b0ae529c2508cf95c611bdce471b8b71d4f1637ca880a6373ea0af`.
SDK x86 SHA256 `bc87cd5f712fc8d65ff06e69ac26ce450aec6daa435f2894934fbc5630bad155`;
x64 `a246cee2c9119ff901ed195e421470cdf3f14fc277ebc506aca8754444be8d6f`.
Current sizes remain 40,563,768 / 466,432 / 442,880 bytes respectively.
