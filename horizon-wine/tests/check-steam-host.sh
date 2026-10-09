#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
cc=${CC:-cc}
flags='-std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-pie -no-pie'
$cc $flags -Wno-deprecated-declarations "$root/tests/steam_prerequisites_host.c" \
    "$root/source/steam/steam_file.c" "$root/source/steam/steam_content.c" "$root/source/steam/steam_wire.c" \
    -lcurl -lmbedcrypto -llzma -lzstd -lz -o "$build/prerequisites"
"$build/prerequisites"
$cc $flags "$root/tests/steam_wire.c" "$root/source/steam/steam_wire.c" \
    "$root/source/steam/steam_cm_message.c" "$root/source/steam/steam_auth.c" -lm -o "$build/wire"
"$build/wire"
$cc $flags "$root/tests/steam_directory.c" "$root/source/steam/steam_directory.c" -o "$build/directory"
"$build/directory"
$cc $flags "$root/tests/steam_crypto.c" "$root/source/steam/steam_crypto.c" \
    "$root/source/steam/steam_wire.c" -lmbedcrypto -o "$build/crypto"
"$build/crypto"
$cc $flags -Wno-deprecated-declarations "$root/tests/steam_cm_packets.c" \
    "$root/source/steam/steam_crypto.c" "$root/source/steam/steam_wire.c" \
    "$root/source/steam/steam_cm_message.c" "$root/source/steam/steam_probe.c" \
    "$root/source/steam/steam_directory.c" -lcurl -lwslay -lmbedcrypto -lz -o "$build/packets"
"$build/packets"
$cc $flags $(sdl2-config --cflags) "$root/tests/steam_logon.c" \
    "$root/source/steam/steam_crypto.c" "$root/source/steam/steam_wire.c" \
    "$root/source/steam/steam_cm_message.c" "$root/source/steam/steam_auth.c" \
    "$root/source/steam/steam_library.c" \
    "$root/source/steam/steam_family.c" "$root/source/steam/steam_settings.c" "$root/source/steam/steam_file.c" "$root/source/steam/steam_session.c" "$root/source/steam/steam_device_key.c" \
    $(sdl2-config --libs) -lmbedcrypto -lm -o "$build/logon"
"$build/logon"
$cc $flags "$root/tests/steam_download_formats.c" "$root/source/steam/steam_library.c" \
    "$root/source/steam/steam_app.c" "$root/source/steam/steam_settings.c" "$root/source/steam/steam_file.c" "$root/source/steam/steam_content.c" \
    "$root/source/steam/steam_manifest.c" "$root/source/steam/steam_wire.c" \
    -lmbedcrypto -llzma -lzstd -lz -o "$build/formats"
"$build/formats"
$cc $flags "$root/tests/steam_library_options.c" "$root/source/steam/steam_family.c" \
    "$root/source/steam/steam_library.c" "$root/source/steam/steam_app.c" \
    "$root/source/steam/steam_settings.c" "$root/source/steam/steam_file.c" "$root/source/steam/steam_wire.c" -o "$build/options"
"$build/options"
$cc $flags -DSTEAM_DEVICE_KEY_FIXTURE "$root/tests/steam_session.c" "$root/source/steam/steam_session.c" \
    "$root/source/steam/steam_auth.c" "$root/source/steam/steam_cm_message.c" \
    "$root/source/steam/steam_crypto.c" "$root/source/steam/steam_wire.c" "$root/source/steam/steam_file.c" -lmbedcrypto -lm -o "$build/session"
"$build/session"
$cc $flags "$root/tests/steam_file_host.c" -o "$build/file"
"$build/file"
$cc $flags "$root/tests/steam_servers_host.c" "$root/source/steam/steam_backend.c" \
    "$root/source/steam/steam_app.c" "$root/source/steam/steam_settings.c" "$root/source/steam/steam_file.c" "$root/source/steam/steam_wire.c" -o "$build/servers"
"$build/servers"
$cc $flags "$root/tests/steam_diagnostics_host.c" "$root/source/steam/steam_diagnostics.c" -o "$build/diagnostics"
"$build/diagnostics"
$cc $flags "$root/tests/steam_install_script_host.c" "$root/source/steam/steam_install_script.c" -o "$build/install-script"
"$build/install-script"
$cc $flags -Wno-deprecated-declarations $(sdl2-config --cflags) "$root/tests/steam_install_parallel_host.c" \
    "$root/source/steam/steam_content.c" "$root/source/steam/steam_manifest.c" \
    "$root/source/steam/steam_wire.c" "$root/source/steam/steam_storage.c" "$root/source/steam/steam_engine.c" "$root/source/steam/steam_diagnostics.c" \
    $(sdl2-config --libs) -lminizip -lmbedcrypto -llzma -lzstd -lz -o "$build/install-parallel"
"$build/install-parallel"
$cc $flags "$root/tests/steam_storage_host.c" "$root/source/steam/steam_storage.c" \
    "$root/source/steam/steam_content.c" "$root/source/steam/steam_wire.c" -lmbedcrypto -llzma -lzstd -lz -o "$build/storage"
"$build/storage"
$cc $flags -Wno-deprecated-declarations $(pkg-config --cflags libcurl) \
    "$root/tests/steam_probe.c" "$root/source/steam/steam_probe.c" \
    "$root/source/steam/steam_directory.c" $(pkg-config --libs libcurl) -o "$build/probe"
"$build/probe" "${1:-}"
