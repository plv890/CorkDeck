#!/usr/bin/env python3
"""Build autorun-NNN.zip, the SD-card archive: the runtime, the files Wine reads
beside it, Autorun's own setup programs and the default settings.

    package-autorun.py [--x86] [--amd64 ZIP] [--dlls DIR]

NNN is the build number in source/runtime.c. The runtime is the AMD64 one
(build-amd64-components.sh), which runs 32- and 64-bit programs. --x86 ships
the x86-only runtime instead (build-x86.sh), for trying it: 32-bit programs
through Box64, without FEX. The archive holds no Windows modules: those are
the DLL repository's (horizon-dlls), which the package is checked against."""
from pathlib import Path
from pathlib import PurePosixPath
from zipfile import ZipFile, ZIP_DEFLATED
import argparse
import json
import os
import re
import shutil
import subprocess
import sys

horizon_wine = Path(__file__).resolve().parents[1]
root = horizon_wine.parent
tools = horizon_wine / 'tools'
build = horizon_wine / 'build-autorun'
stage_root = build / 'sd-card'
stage = stage_root / 'switch/wine'
marker = re.search(r'nx-wow64-dynarec-(\d+)', (horizon_wine / 'source/runtime.c').read_text()).group(1)
toolchain = Path(os.environ.get('WINE_NX_LLVM_MINGW',
                 horizon_wine / 'toolchains/llvm-mingw-20260505-ucrt-macos-universal')) / 'bin'
# The FEX build when there is one, as build-amd64-components.sh names it.
default_amd64 = next((path for path in (horizon_wine / f'build-switch-amd64/wine-nx-amd64-box64{kind}-mesa-dxvk-vkd3d.zip'
                                        for kind in ('-fex', '')) if path.is_file()),
                     horizon_wine / 'build-switch-amd64/wine-nx-amd64-box64-mesa-dxvk-vkd3d.zip')
parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
parser.add_argument('--amd64', type=Path,
                    default=Path(os.environ.get('WINE_NX_AMD64_PACKAGE', default_amd64)))
parser.add_argument('--x86', action='store_true', help='ship the x86-only runtime instead of the AMD64 one')
parser.add_argument('--dlls', type=Path, default=root / 'horizon-dlls/switch/wine',
                    help="the DLL repository's card tree, to check the package against")
args = parser.parse_args()


def compile_setup(source, output, *libs):
    """One of Autorun's own small i386 programs, which the card runs from C:."""
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(toolchain / 'i686-w64-mingw32-clang'), '-Os', '-Wall', '-Wextra', '-Werror',
                    '-fno-builtin', '-nostdlib', '-Wl,--entry,_start@0', '-Wl,--image-base,0x10000000',
                    '-Wl,--dynamicbase', '-o', str(output), str(source), *libs, '-lkernel32', '-lntdll'],
                   check=True)


# --- What Wine reads beside the runtime --------------------------------------

assert (root / 'nls/locale.nls').is_file(), 'Wine locale.nls is required for a complete installation'
shutil.rmtree(build, ignore_errors=True)
stage.mkdir(parents=True)
for nls in (root / 'nls').glob('*.nls'):
    (stage / 'share/wine/nls').mkdir(parents=True, exist_ok=True)
    shutil.copy2(nls, stage / 'share/wine/nls' / nls.name)
fonts = list((root / 'fonts').glob('*.ttf'))
assert fonts, 'Wine has no fonts to stage'
for folder in ('drive_c/windows/fonts', 'share/wine/fonts'):
    (stage / folder).mkdir(parents=True, exist_ok=True)
    for font in fonts:
        shutil.copy2(font, stage / folder / font.name)
licenses = stage / 'licenses'
licenses.mkdir()
for source, name in ((root / 'COPYING.LIB', 'Wine-LGPL-2.1.txt'),
                     (horizon_wine / 'vendor/box64/LICENSE', 'Box64-MIT.txt'),
                     (horizon_wine / 'licenses/libjpeg-turbo.txt', 'libjpeg-turbo.txt'),
                     (horizon_wine / 'licenses/wslay-MIT.txt', 'wslay-MIT.txt'),
                     (horizon_wine / 'licenses/qrcodegen-MIT.txt', 'qrcodegen-MIT.txt'),
                     (horizon_wine / 'licenses/liblzma-0BSD.txt', 'liblzma-0BSD.txt'),
                     (horizon_wine / 'licenses/zstd-BSD.txt', 'zstd-BSD.txt')):
    shutil.copy2(source, licenses / name)

# --- Autorun's setup programs ------------------------------------------------

drive = stage / 'drive_c'
# What wineboot registers on a computer and nothing does on the Switch:
# DirectShow, DirectX Media Objects and the MP3 decoder. The runtime runs it
# before the first program on a card (source/runtime.c), for every game.
# Read-only prerequisite probes: ordinary x86/x64 Windows executables. Microsoft
# redistributables are fetched on the device, never bundled/relicensed here.
for arch in ('i686', 'x86_64'):
    output = drive / 'windows/AutorunPrerequisites' / ('probe-x86.exe' if arch == 'i686' else 'probe-x64.exe')
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(toolchain / (arch + '-w64-mingw32-clang')), '-Os', '-Wall', '-Wextra', '-Werror',
                    '-fno-builtin', '-nostdlib', '-Wl,--entry,' + ('_start@0' if arch == 'i686' else 'start'),
                    '-Wl,--dynamicbase', '-o', str(output), str(tools / 'autorun_prereq_probe.c'),
                    '-ladvapi32', '-lkernel32', '-lntdll'], check=True)
compile_setup(tools / 'autorun_setup.c', drive / 'windows/autorun-setup.exe', '-lole32', '-ladvapi32')

# WarCraft III: the game is the player's to copy into its folder; the setup
# writes its settings into the card's registry.
compile_setup(tools / 'war3_setup.c', drive / 'WarCraft III Setup/war3-setup.exe', '-ladvapi32')
(drive / 'WarCraft III').mkdir()
(drive / 'WarCraft III Setup/README.txt').write_text('''WarCraft III
============

1. Copy your own WarCraft III installation into switch/wine/drive_c/WarCraft III,
   so the game is at switch/wine/drive_c/WarCraft III/war3.exe.
2. Run war3-setup.exe once from the launcher and wait for it to finish. Run it
   again after changing the resolution in the game's options.
3. Start war3.exe through a forwarder set to a 36-bit or 39-bit address space.
   With 32-bit its DLL layout changes from launch to launch and Game.dll
   sometimes fails to load.

The setup writes each step as a [WAR3 SETUP] line in autorun_runtime.log,
ending with "done, all steps worked":
- the game's video settings: 1280x720, 32-bit colour, 60 Hz, the Switch's own
  screen, where the pointer and the touchscreen are;
- the intro movie plays at startup;
- the game draws with its own OpenGL renderer, which takes about half the CPU
  its Direct3D does through Wine (a war3.args.txt holding -opengl next to
  war3.exe does the same for one launch);
- only the 1280x720 display mode is offered, so the movies fill the width.

The movies play through DirectShow, which Autorun registers before the first
program on the card.
''')

# The Sims 2 Ultimate Collection is shipped installed; what is left is telling
# the game where each of its packs is, which its release does with a batch file
# of reg add lines whose every path comes from the folder it is run in.
sims2 = drive / 'The Sims 2 Setup'
compile_setup(tools / 'sims2_setup.c', sims2 / 'sims2-setup.exe', '-ladvapi32')
# DXVK's settings for the game, which the setup copies beside each executable.
shutil.copy2(tools / 'sims2/dxvk.conf', sims2 / 'dxvk.conf')
(sims2 / 'README.txt').write_text('''The Sims 2 Ultimate Collection
==============================

Copy the collection onto the card -- leave __Installer and Support behind, they
are for a computer -- and run sims2-setup.exe once from the launcher. It writes
what the release's own "Instalar Registros" batch file writes, with the card's
paths, and says what it did in autorun_runtime.log as [SIMS2 SETUP] lines.
Running it again is harmless.

Where the packs go does not matter much. Each one is recognised by the
executable in its TSBin, not by the name of the folder around it, so a release
that calls them Base and EP1-EP9 and one that spells out "The Sims 2 Nightlife"
both work, and the collection may keep a folder of its own around them. Put
sims2-setup.exe's folder beside the packs, or beside the folder holding them.

The setup also copies dxvk.conf from its folder into each pack's TSBin, next to
the executable. It holds the game to 512 MB of video memory: DXVK's own profile
for The Sims 2 reports 2 GB, and on the Switch the game fills the shared 1.5 GB
and crashes. A dxvk.conf already in TSBin is left as it is.

It also sets the game's own Graphics Rules.sgr (TSData\\Res\\Config in each pack)
for the Switch's 1280x720 screen: in its screen resolution option every default
becomes 1280x720, and a maximum below that is raised to it. Nothing else in the
file changes, and the file as it was is kept as Graphics Rules.sgr.original.

The game is the newest expansion's executable, TSBin\\Sims2EP9.exe. It has no
relocations and is linked for 0x400000, so it requires the 39-bit Autorun
forwarder and the Atmosphere low-address patch.

The game's own movies -- the intro, the EA logo, what plays on a television --
are .movie files in Maxis' own format, which the game reads itself: they need no
codec and nothing registered.

VP6 is for the video the game writes rather than the video it reads. It records
gameplay through Video for Windows (AVIStreamWrite and ICSeqCompressFrame are
what Sims2EP9.exe imports), and a custom video made as a VP6 AVI is read the
same way. The codec is the release's to supply and nobody else's to give away:
copy __Installer\\customcomponent\\vp6\\vp6vfw.dll into C:\\windows\\syswow64 on
the card if you want either. The setup registers it under both the names Video
for Windows opens it by, whether or not the file is there.

The language number does two things: the game reads it, and so does the
release's own launcher emulation, which anadius.cfg points at a key of its own
by setting its language to "invalid". The setup writes both. Getting only the
first is how the game comes up saying "open: Invalid handle" and stops.

For a language other than English, put its number in language.txt beside
sims2-setup.exe before running it:

  1 English (United States)   10 Portuguese (Brazil)    17 Chinese (Simplified)
  2 French                    11 Czech                  18 Chinese (Traditional)
  3 German                    13 English (United Kingdom) 20 Polish
  4 Italian                   14 Japanese               21 Thai
  5 Spanish                   15 Korean                 22 Norwegian
  6 Swedish                   16 Russian                23 Portuguese (Portugal)
  7 Finnish                                             24 Hungarian
  8 Dutch
  9 Danish
''')
(drive / 'The Sims 2').mkdir()

# Guitar Hero III reads the key Aspyr's installer writes, with the game's folder
# in it, so its setup goes in that folder and writes where it is.
compile_setup(tools / 'gh3_setup.c', drive / 'Guitar Hero III/gh3-setup.exe', '-lshell32', '-ladvapi32')

# Fallout New Vegas hands itself over to its own launcher unless it recognises
# the card's display: FalloutNV.exe compares sD3DDevice with what adapter 0
# calls itself, and with no FalloutPrefs.ini that default is the empty string,
# which never matches. It then ShellExecutes FalloutNVLauncher.exe and returns.
# The card gets the file the launcher would have written, naming the Switch's
# adapter the way DXVK reports it, at 720p. The game rewrites this file itself
# once its own options are used, so the values are a starting point, not a rule.
fallout = stage / 'drive_c/users/steamuser/Documents/My Games/FalloutNV'
fallout.mkdir(parents=True)
(fallout / 'FalloutPrefs.ini').write_bytes('\r\n'.join((
    '[Display]',
    'sD3DDevice="NVIDIA Tegra X1 (GM20B) (NVK GM20B)"',
    'iAdapter=0',
    'iSize W=1280',
    'iSize H=720',
    'bFull Screen=1',
    'iMultiSample=0',
    '')).encode())

# --- Settings ----------------------------------------------------------------

# The launcher lists every program in drive_c; target.txt only preselects one.
(stage / 'target.txt').write_text('sdmc:/switch/wine/drive_c/WarCraft III Setup/war3-setup.exe\n')
config = stage / 'config'
config.mkdir()
(config / 'settings.json').write_text('''{
  "run-the-chosen-program": true,
  "verbose-log": false,
  "profiler": false,
  "core-balancing": true,
  "display-devices": true,
  "windows-through-opengl": true,
  "vulkan-probe": false,
  "reopen-the-launcher-on-exit": true,
  "dxvk-for-new-games": true,
  "hand-the-process-back-anyway": false,
  "gl-pinned-buffers-cached": true,
  "gl-clean-before-submit": true,
  "gl-clean-test": false
}
''')
# The controller stands in for a keyboard; this lists what each control sends
# and how to change it, with every line commented out so the defaults hold.
(config / 'keys.txt').write_text('''# Keys the controller sends, one NAME=code line each, where code is a Windows
# virtual-key code in decimal or 0x form. Remove the # to change one. A and B
# are not here: they stay the left and right mouse buttons.
#
# UP=0x26      d-pad up, and the left stick pushed up unless LUP is set
# DOWN=0x28
# LEFT=0x25
# RIGHT=0x27
# X=0x20       space
# Y=0x46       f
# L=0x09       tab
# R=0x10       shift
# ZL=0x28      down arrow, a brake in a racing game
# ZR=0x26      up arrow, the accelerator
# PLUS=0x1B    escape
# MINUS=0x09   tab
# STICKL=0x11  control
# STICKR=0x12  alt
# LUP=0        the left stick alone, for a game that walks with one set of keys
# LDOWN=0      and works its menus with another. Unset, it sends what the d-pad
# LLEFT=0      does.
# LRIGHT=0
# RUP=0x26     the right stick, when it is set to send keys rather than move
# RDOWN=0x28   the mouse
# RLEFT=0x25
# RRIGHT=0x27
# TUP=0x26     a finger dragged across the screen, likewise
# TDOWN=0x28
# TLEFT=0x25
# TRIGHT=0x27
#
# And what each of the four that can point does, which is mouse or keys:
#
# LSTICK=keys
# RSTICK=mouse
# DPAD=keys
# TOUCH=mouse
''')

(stage / 'README.txt').write_text(f'''Autorun build {marker}
================

Copy the switch folder to the SD card, merging folders; it replaces the runtime
NRO of any earlier build. The Windows DLLs are not in this archive: Autorun
downloads them itself (Settings, System, Windows DLLs), or copy the switch
folder of autorun-horizon-dlls' release the same way.

The launcher lists the programs in drive_c. Games that need something more:

WarCraft III: see drive_c/WarCraft III Setup/README.txt.

The Sims 2 Ultimate Collection: see drive_c/The Sims 2 Setup/README.txt.

Guitar Hero III: run gh3-setup.exe from the game's folder once.

Need for Speed Underground 2 and Most Wanted: neither executable can be moved in
memory, so start them through a forwarder set to a 32-bit address space.

Fallout New Vegas (GOG): its executable relocates, so it needs no forwarder.
Started with no settings of its own the game hands itself to
FalloutNVLauncher.exe and closes, so the archive brings the settings file it
would have written:

    C:\\users\\steamuser\\Documents\\My Games\\FalloutNV\\FalloutPrefs.ini

It names the Switch's GPU as DXVK reports it, at 1280x720. The game rewrites
that file once its own options are used; if it already holds settings worth
keeping, keep the [Display] sD3DDevice line and merge the rest.

Halo: Combat Evolved: delete the ._ files a Mac leaves beside every file on the
card if the game was copied from one: Halo loads every DLL in its Controls
folder and one of those is not a DLL. Halo walks with w, a, s and d and works
its menus with the arrows, so the left stick takes one set and the d-pad the
other. Put this beside HALO.EXE as HALO.keys.txt, which is applied over
keys.txt:

    LUP=0x57    w, forward: the left stick alone
    LDOWN=0x53  s
    LLEFT=0x41  a
    LRIGHT=0x44 d
    UP=0x26     the arrows, for the menus: the d-pad alone
    DOWN=0x28
    LEFT=0x25
    RIGHT=0x27
    X=0x0d      Enter, to choose a menu item
    Y=0x20      space, to jump
    L=0x45      e, the action key
    R=0x52      r, to reload
    ZL=0x11     left control, to crouch
    ZR=0x09     tab, the scores
    MINUS=0x1b  Escape, to go back

Left 4 Dead 2: left4dead2.exe relocates, so it needs no forwarder either.
Steam's GameOverlayRenderer.dll not loading is expected and harmless.

Settings: switch/wine/config/settings.json holds every one of them, and
config/keys.txt the keys the controller sends. Both are set from the launcher
(Settings, Defaults, Controls, and a program's own Controls row), so neither
has to be written by hand.

Logs: switch/wine/logs/autorun_runtime.log holds the last run.
''')


# --- The AMD64 runtime -------------------------------------------------------

def merge_amd64(archive, root):
    keep = {
        'switch/wine/run-entry.txt',
        'switch/wine/target.txt',
        'switch/wine/vulkan-probe.txt',
    }
    required = {
        'switch/wine/build-manifest.json',
        'switch/wine/wine-nx-runtime.nro',
    }
    with ZipFile(archive) as z:
        assert z.testzip() is None, f'{archive} is damaged'
        names = set()
        for info in z.infolist():
            path = PurePosixPath(info.filename)
            assert path.parts[:2] == ('switch', 'wine') and '..' not in path.parts, info.filename
            assert not ((info.external_attr >> 16) & 0o170000) == 0o120000, info.filename
            folded = info.filename.rstrip('/').casefold()
            assert folded not in names, info.filename
            names.add(folded)
        assert {name.casefold() for name in required} <= names, f'{archive} is not a full AMD64 graphics package'
        manifest = json.loads(z.read('switch/wine/build-manifest.json'))
        features = manifest.get('features', {})
        for feature in ('amd64', 'dynarec', 'vulkan', 'dxvk', 'vkd3d', 'lsfg'):
            assert features.get(feature) is True, f'{archive} has no {feature} support'
        amd64_nro = z.read('switch/wine/wine-nx-runtime.nro')
        match = re.search(rb'nx-amd64-(?:box64-(\d+)|(fex-\d+))\0', amd64_nro)
        assert match, f'{archive} does not contain the AMD64 runtime'
        assert bool(match.group(2)) == bool(features.get('fex')), f'{archive} has inconsistent FEX support'
        for info in z.infolist():
            if info.filename.rstrip('/') in keep:
                continue
            destination = root.joinpath(*PurePosixPath(info.filename).parts)
            if info.is_dir():
                destination.mkdir(parents=True, exist_ok=True)
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                with z.open(info) as source, destination.open('wb') as output:
                    shutil.copyfileobj(source, output)
    return (match.group(1) or match.group(2)).decode()


if args.x86:
    # The card runs Direct3D 9 through DXVK, so the x86 runtime is the one
    # linked with mesa-switch: NVK behind winevulkan. Without it a game on DXVK
    # dies with "Failed to create Vulkan instance".
    nro = horizon_wine / 'build-switch-wow64-mesa-switch/wine-nx-runtime.nro'
    assert nro.is_file(), f'{nro} is missing; build it with build-x86.sh'
    assert b'a Vulkan surface has the screen' in nro.read_bytes(), \
        f'{nro} has no Vulkan display driver; it is not the mesa-switch build'
    assert f'nx-wow64-dynarec-{marker}'.encode() + b'\0' in nro.read_bytes(), \
        f'{nro} is stale; rebuild the runtime for build {marker}'
    shutil.copy2(nro, stage / 'wine-nx-runtime.nro')
    # The build copies LSFG-VK's license out of the switch-dev image.
    shutil.copy2(nro.parent / 'licenses/LSFG-VK-GPL-3.0.txt', licenses / 'LSFG-VK-GPL-3.0.txt')
    shutil.copy2(nro.parent / 'licenses/FFmpeg-LGPL-2.1.txt', licenses / 'FFmpeg-LGPL-2.1.txt')
    print('x86 runtime staged')
else:
    assert args.amd64.is_file(), f'{args.amd64} is missing; run build-amd64-components.sh, or pass --x86'
    print(f'AMD64 runtime build {merge_amd64(args.amd64, stage_root)} merged')
subprocess.run([sys.executable, str(tools / 'verify-package.py'), str(stage), '--dlls', str(args.dlls)],
               check=True)

archive = build / f'autorun-{marker}.zip'
with ZipFile(archive, 'w', ZIP_DEFLATED) as z:
    for f in sorted(stage.rglob('*')):
        if f.is_file() and f.name != '.DS_Store' and f.suffix != '.log':
            z.write(f, f.relative_to(stage_root))
        # Empty folders are places to copy a game into, such as drive_c/WarCraft III.
        elif f.is_dir() and not any(f.iterdir()):
            z.write(f, f.relative_to(stage_root))
with ZipFile(archive) as z:
    assert z.testzip() is None
    files = len(z.infolist())
print(f'{archive} ({archive.stat().st_size / 2**20:.1f} MiB, {files} files)')
