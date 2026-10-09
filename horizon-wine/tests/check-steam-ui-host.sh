#!/bin/sh
# Offline screenshot/ASan fixture; no real account and no network.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
mkdir -p "$build/logs"
cc=${CC:-cc}
$cc -std=gnu11 -Wall -Wextra -Werror -no-pie -fsanitize=address,undefined \
    $(sdl2-config --cflags) "$root/tests/steam_ui_host.c" \
    "$root/source/steam/steam_ui.c" "$root/source/steam/steam_wire.c" \
    "$root/source/steam/steam_prerequisites.c" "$root/source/steam/steam_prerequisites_ui.c" "$root/source/steam/steam_library_ui.c" "$root/source/steam/steam_content.c" \
    "$root/source/steam/steam_settings.c" "$root/source/steam/steam_file.c" "$root/source/steam/steam_diagnostics.c" "$root/source/steam/steam_artwork.c" \
    "$root/source/launcher_image.c" "$root/source/diagnostics/lan_log.c" \
    "$root/source/launcher_ui.c" "$root/source/launcher_svg.c" \
    "$root/source/launcher_audio.c" "$root/vendor/qrcodegen/qrcodegen.c" \
    $(sdl2-config --libs) -lSDL2_ttf -lcurl -lpng -lturbojpeg -lmbedcrypto -llzma -lzstd -lz -lm -o "$build/ui"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/login.bmp" 0 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/online.bmp" 1 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/library.bmp" 2 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/installed.bmp" 3 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/downloads.bmp" 4 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/steam-settings.bmp" 5 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/saved-session.bmp" 6 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/language-last.bmp" 7 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/game-size.bmp" 8 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/unused.bmp" 9 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/export.bmp" 10 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/lan-url.bmp" 11 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/cancelling.bmp" 12 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/cancelled.bmp" 13 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/cleanup-failure.bmp" 14 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/language-alternative.bmp" 15 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/language-base.bmp" 16 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/language-back.bmp" 17 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/game-language.bmp" 18 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/download-language.bmp" 19 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/integrity-failure.bmp" 20 "$build"
"$build/ui" "$root/../fonts/tahoma.ttf" "$build/prerequisites.bmp" 21 "$build"
if [ -n "${STEAM_UI_SHOTS:-}" ]; then
    mkdir -p "$STEAM_UI_SHOTS"
    cp "$build"/*.bmp "$STEAM_UI_SHOTS/"
fi
echo 'Steam account, library, installed and download UI fixtures passed (synthetic data only).'
