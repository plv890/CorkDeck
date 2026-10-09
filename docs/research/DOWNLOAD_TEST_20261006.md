# Download benchmark — 2026-10-06 (Asia/Tbilisi)

Physical Switch OLED, official Steam provider, Russian language, four parallel CDN requests. Build used for measurements: 2026.10.05-4. Steam restored automatically after a fresh Autorun launch; no Guard or passphrase entry. No compatibility/gameplay claim is made for the downloaded games.

## Hardware results

| Game | Installed bytes | Network bytes | Total time | Result |
|---|---:|---:|---:|---|
| 12 Is Better Than 6 (410110) | 1,273,018,898 | 652,430,352 | 322.463 s | Complete, resumed existing validated staging |
| Balatro (2379780) | 92,337,448 | 85,304,414 | 32.082 s | Complete, fresh download |

Both reached state=6, error=0, HTTP=200, transport=0, integrity_retries=0 and diagnostic_error=0. Both appeared automatically in Steam Installed. Balatro's resume scan was only 29 ms; staging was not reused as in the first test. Size includes the selected game's depots and shared redistributable content.

| Counter, seconds | 12 Is Better Than 6 | Balatro |
|---|---:|---:|
| All HTTPS requests (`network_ms`) | 708.554 | 67.220 |
| Time to first response byte (`first_byte_ms`) | 127.475 | 16.537 |
| Response body (`body_ms`) | 576.713 | 50.134 |
| Decode and chunk integrity checks (`decode_ms`) | 123.149 | 10.930 |
| SD writes (`write_ms`) | 57.879 | 2.169 |
| SD sync (`sync_ms`) | 1.408 | 0.075 |
| Whole-file verification (`verify_ms`) | 67.630 | 4.443 |
| Existing chunk validation (`resume_ms`) | 14.783 | 0.029 |
| Writer queue/drain wait (`writer_wait_ms`) | 8.519 | 0.970 |

These counters overlap across workers. Request times are summed across concurrent requests and can exceed wall time; **do not add these columns or present them as percentages of elapsed time**. First-byte and body times need not exactly equal request time. Network throughput averaged 1.93 MiB/s over the resumed installation's whole wall time and 2.54 MiB/s for Balatro. Active windows were often about 2.5–5 MB/s, with pauses for verification. Five connections for 716/105 requests demonstrate connection reuse, not a new TLS connection per chunk. Peaks reached four active requests.

## Where the visible stall occurs

The first installation spent 28.576 s verifying an existing large file before network requests. Existing chunk scanning also contributed 14.675 s early in the run. Later, progress sat at 756,540,328 bytes in state=4 while the worker checked the file. The UI's elapsed/network rate remained stale until verification returned. This was a busy SD/SHA pass, not an idle CDN request or a failed installer.

Balatro separates fresh download from resume overhead. SD write+sync totals were only 2.244 s, versus decode 10.930 s and file SHA 4.443 s. Raw SD writes therefore do not explain most of this small download. HTTPS/body delivery and CPU content processing both matter; the counters alone cannot distinguish remote CDN bandwidth, Wi-Fi, TLS CPU contention and decoder scheduling. There were no integrity retries to optimize away.

## Changes

Revision 2026.10.06-1 updates elapsed time and verification counters every 500 ms during a large SHA pass, preserves cancellation, and does not increase verified bytes until the digest matches. It sets network rate to zero for verification and resets the subsequent rate window. The authenticated live status now includes first-byte/body, verification/resume and writer-wait counters already present in saved numeric history. Existing SHA and TLS checks remain in place.

Added `tools/remote_switch/sample_download.py`: a bounded read-only collector, DPAPI key input, timestamped status samples and saved numeric history tails. It stops on a terminal transfer state and avoids screenshot traffic during the benchmark.

Host Steam installer and UI ASan/UBSan suites passed. The new observer fixture checks advancing verification time, unchanged validated byte count, throttled publication and cancellation. The native Switch NRO compiled successfully. Deployment acceptance is recorded under `D:/autorun fork/artifacts/download-diagnostics/installation.json`.

## Next speed experiments

1. Measure a matched fresh download at concurrency 2 versus 4 before increasing buffers or request count. Four requests already saturate the configured queue; more concurrency has not been shown beneficial.
2. Profile decrypt/decompress/chunk SHA CPU separately: `decode_ms` currently combines these operations. A hardware crypto/optimized SHA path needs measured benefit and unchanged digest verification.
3. Benchmark larger sequential read buffers for whole-file SHA (currently 32 KiB) on the same SD. Keep resume chunk checking and final file verification until an equivalent integrity-preserving fast path is proven.
4. If CDN variability persists, collect aggregate per-host throughput/first-byte statistics without signed URLs or tokens and compare hosts using the same data. Current logs contain no per-host throughput, so a claim about one slow CDN would be unsupported.

Private raw evidence: ignored `tools/remote_switch/runs/download-benchmark`, `balatro-benchmark`, `balatro-installed.jpg`. Screens with account/temporary access keys must not be published. The temporary LAN service is stopped by Autorun exit; DBI FTP must be closed after the NRO update.

## Deployment acceptance

2026.10.06-1 installed on the physical Switch. NRO size 40,539,192 bytes, SHA256 `4626896c5049f784853f3bd1b286b26045493df1c817279c990243421be7f265`; staged and installed readback matched. Previous NRO/manifest backed up with suffix `.before-download-diagnostics-20261006`. FTP stopped and port 5000 refused connections. New build label and automatic authenticated Steam OK confirmed after replacement. Existing saved vault survived the update. The new SHA heartbeat is host-tested; a new hardware transfer on this revision has not yet been benchmarked. Download speed improvements are not claimed from this observability change.

Follow-up: after the successful new-build settings/account check, a batch intended to reach Banished's details produced a black screen. Subsequent screenshot, closed-log and title probes timed out. No third download or game launch was confirmed; no conclusion about a crash cause can be made from this evidence. The user was asked whether physical HOME still responds. No further blind console input was sent. The two completed transfers above precede this event.
