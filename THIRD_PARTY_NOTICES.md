# Third-party notices for the Steam integration

The Steam provider implementation is original code under Autorun's existing license.
No GameNative or DepotDownloader GPL code is incorporated.
SteamKit and node-steam-session were inspected as protocol references only.

Visual C++ prerequisites catalog (2026-10-02): original provider/probe code, no
new open-source dependency or reference-project source incorporated. Microsoft
Visual C++ v14 redistributables 14.44.35211.0 are proprietary Microsoft packages,
downloaded on demand directly from Microsoft's servers under their own terms.
They are not present in Autorun's full/update archives and are not relicensed
under LGPL. Do not redistribute them with the fork without reviewing Microsoft's
applicable distribution license. Source catalog pins:

- x86: 13,953,392 bytes, SHA256
  `0c09f2611660441084ce0df425c51c11e147e6447963c3690f97e0b25c55ed64`.
- x64: 25,635,768 bytes, SHA256
  `cc0ff0eb1dc3f5188ae6300faef32bf5beeba4bdd6e8e445a9184072096b713b`.

Both files were obtained through the official Microsoft VS17 permalinks and
checked with Windows Get-AuthenticodeSignature: Valid, signer Microsoft Corporation.
Future catalog updates must repeat origin/signature/hash checks. Horizon uses
the pinned hash of these checked bytes, not a newly implemented signature verifier.
https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist
https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files

wslay is linked from the existing devkitPro portlibs SDK (MIT/Expat).
Copyright (c) 2011, 2012 Tatsuhiro Tsujikawa.
Full license: horizon-wine/licenses/wslay-MIT.txt, also included in the SD package.
Upstream: https://github.com/tatsuhiro-t/wslay

mbedTLS supplies RSA, SHA-1 for the WebSocket handshake, Base64, AES-GCM and
PBKDF2-HMAC-SHA256 for optional encrypted session storage; it is an
existing Autorun SDK dependency, licensed Apache-2.0.
Upstream: https://github.com/Mbed-TLS/mbedtls

libcurl and zlib are existing Autorun dependencies used for transport and bounded
CM Multi decompression. Their upstream notices and licenses remain applicable.
See NOTICES.md and the existing build SDK for other runtime dependencies.

QR Code generator (C), Project Nayuki, MIT.
Pinned revision: 3c6d0b3cefb4e049dc337e82237c9644399716a8.
Source: https://github.com/nayuki/QR-Code-generator
Files: horizon-wine/vendor/qrcodegen/qrcodegen.c and qrcodegen.h, unmodified.
Full notice: horizon-wine/licenses/qrcodegen-MIT.txt, included in SD package.

liblzma 5.8.2 (devkitPro switch-liblzma 5.8.2-1), XZ Utils authors and contributors,
BSD Zero Clause License (0BSD). Only the library is linked; GPL command-line tools
and scripts are not incorporated. SDK package metadata says public domain; upstream
v5.8.2 COPYING explicitly identifies liblzma as 0BSD, so the upstream notice is used.
https://github.com/tukaani-project/xz/blob/v5.8.2/COPYING
Full notice: horizon-wine/licenses/liblzma-0BSD.txt, included in SD package.

Zstandard 1.5.7, Copyright (c) Meta Platforms, Inc. and affiliates.
Already linked by Autorun, now also used for Steam chunk decompression.
BSD-3-Clause option (not its alternative GPL option).
https://github.com/facebook/zstd/blob/v1.5.7/LICENSE
Full notice: horizon-wine/licenses/zstd-BSD.txt, included in SD package.

The parallel CDN pool, recovery/diagnostics helpers and cover-cache worker are
original additions. They reuse existing libcurl, SDL2, PNG and JPEG support
through Autorun's launcher image/UI modules. No additional library or GPL
reference implementation was incorporated for these changes.
