#!/bin/sh
# Main Autorun shell and manual-game launch regression. Synthetic PE/artwork,
# no Steam account or network; reuse upstream launcher_host fixtures.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
python3 "$root/tools/make-embed.py" "$build/icon.c" wine_nx_icon_any "$root/assets/autorun.jpg"
for target in launcher steam-adapter; do
    if [ "$target" = launcher ]; then
        sources="$root/tests/launcher_host.c $root/source/launcher.c"
    else
        sources="$root/tests/steam_launcher_adapter_host.c"
    fi
clang -std=gnu11 -Wall -Wextra -Werror -no-pie -g -fsanitize=address,undefined \
    -I "$root/source" $(sdl2-config --cflags) \
    $sources \
    "$root/source/launcher_catalog.c" "$root/source/launcher_ui.c" \
    "$root/source/launcher_audio.c" "$root/source/launcher_graphics.c" \
    "$root/source/launcher_forwarder.c" "$root/source/launcher_image.c" \
    "$root/source/launcher_setup.c" "$root/source/setup_boot.c" \
    "$root/source/steamgriddb.c" "$root/source/steam/"*.c "$root/source/diagnostics/lan_log.c" \
    "$root/source/launcher_svg.c" "$build/icon.c" "$root/vendor/qrcodegen/qrcodegen.c" \
    $(sdl2-config --libs) -lSDL2_ttf -lpng -lturbojpeg -lcurl -lwslay -lmbedcrypto \
    -lminizip -llzma -lzstd -lz -lcrypto -lm -o "$build/$target"
done
mkdir -p "$build/card/sdmc:/switch/wine/logs" "$build/shots"
(cd "$build/card" && "$build/steam-adapter")
cat > "$build/script" <<EOF
wait 30
shot $build/shots/main-steam.png
key up
key right
key right
wait 15
shot $build/shots/steam-header.png
key a
wait 5
shot $build/shots/steam-unlinked.png
key a
wait 5
key right
wait 15
shot $build/shots/downloads-header.png
key b
wait 5
EOF
(cd "$build/card" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$build/launcher" "$root/../fonts/tahoma.ttf" "$build/script" --carousel-fixture > "$build/result" 2>&1) || { cat "$build/result"; exit 1; }
cat "$build/result"
if [ -n "${STEAM_UI_SHOTS:-}" ]; then mkdir -p "$STEAM_UI_SHOTS"; cp "$build/shots/"*.png "$STEAM_UI_SHOTS/"; fi
grep -q '8 catalog games (8 registered, 8 shown)' "$build/result"
# Upstream launcher_host deliberately has no graphics-manager object. Select
# the existing Wine renderer in test fixtures, so the manual Play path can run
# without installing DXVK. Production defaults and launch code are unchanged.
for settings in "$build/card/sdmc:/switch/wine/drive_c/"*/Game.wine-nx.txt; do
    printf '\nd3d=wine\n' >> "$settings"
done
cat > "$build/manual-script" <<EOF
wait 20
key a
wait 5
EOF
(cd "$build/card" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$build/launcher" "$root/../fonts/tahoma.ttf" "$build/manual-script" > "$build/manual-result" 2>&1) || { cat "$build/manual-result"; exit 1; }
cat "$build/manual-result"
grep -q "launcher returned 1 target 'sdmc:/switch/wine/drive_c/Afterlight/Game.exe'" "$build/manual-result"
echo 'Main Steam/Downloads shell and existing manual-game launch path passed.'
