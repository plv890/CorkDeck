#!/bin/sh
# The shell on a computer: builds the launcher as an ordinary Linux program with
# SDL's dummy video driver, stands a small card of programs up under "sdmc:",
# drives the screens with a script (tests/launcher_host.c says what a script
# may hold) and saves the frames it asks for as PNG files.
#
#   sh shell-stand.sh SCRIPT OUTPUT_DIR [extra arguments of launcher_host]
#
# Without ASan by default, so a picture comes quickly; STAND_SANITIZE=1 adds it.
set -eu
script=$1
out=$2
shift 2
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
hw="$root/horizon-wine"
notepad="$root/horizon-dlls/switch/wine/drive_c/windows/syswow64/notepad.exe"
[ -f "$notepad" ] || { echo "no $notepad: check out horizon-dlls" >&2; exit 1; }
build="${STAND_BUILD:-/tmp/corkdeck-stand}"
font="${LAUNCHER_FONT:-/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf}"
mkdir -p "$build" "$out"
flags="-std=gnu11 -O1 -g -w -fno-pie"
[ "${STAND_SANITIZE:-0}" = 1 ] && flags="$flags -fsanitize=address,undefined -fno-omit-frame-pointer"

python3 "$hw/tools/make-embed.py" "$build/forwarder-icon.c" wine_nx_icon_any "$hw/assets/autorun.jpg" \
    corkdeck_font "$hw/assets/fonts/Manrope-SemiBold.ttf"
# Everything the shell is made of, except what tests/launcher_host.c stands in
# for: the DLL manager's screens and the platform.
sources="$hw/tests/launcher_host.c $build/forwarder-icon.c $hw/vendor/qrcodegen/qrcodegen.c $hw/source/diagnostics/lan_log.c"
for name in launcher launcher_catalog launcher_ui launcher_audio launcher_graphics launcher_forwarder \
            launcher_image launcher_setup setup_boot steamgriddb launcher_svg; do
    sources="$sources $hw/source/$name.c"
done
for file in "$hw"/source/steam/*.c "$hw"/source/app/*.c "$hw"/source/shell/*.c; do
    [ -f "$file" ] && sources="$sources $file"
done
if [ ! -x "$build/stand" ] || [ -n "$(find "$hw/source" "$hw/tests/launcher_host.c" -newer "$build/stand" -name '*.[ch]' | head -1)" ]; then
    # shellcheck disable=SC2086
    cc $flags -no-pie -I "$hw/source" $(sdl2-config --cflags) $sources \
        $(sdl2-config --libs) -lSDL2_ttf -lpng -lturbojpeg -lcurl -lwslay -lmbedcrypto -lminizip -llzma -lzstd -lz -lcrypto -lpthread -lm \
        -o "$build/stand"
fi

card="$build/card/sdmc:"
rm -rf "$build/card"
mkdir -p "$card/switch/wine/drive_c/openttd" "$card/switch/wine/drive_c/Games/Dishonored" "$card/games/deep/er/still"
for exe in "drive_c/openttd/openttd.exe" "drive_c/Games/Dishonored/Dishonored.exe" "drive_c/7zr.exe"; do
    ln -s "$notepad" "$card/switch/wine/$exe"
done
ln -s "$notepad" "$card/games/deep/er/still/Deep.exe"
[ -n "${STAND_CARD_SETUP:-}" ] && sh "$STAND_CARD_SETUP" "$card"

(cd "$build/card" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy LAUNCHER_SHOTS="$out" \
    setarch x86_64 -R "$build/stand" "$font" "$script" "$@")
# A script names its pictures by file name alone; they are made where it ran.
for shot in "$build/card"/*.png; do
    [ -f "$shot" ] && mv "$shot" "$out/"
done
