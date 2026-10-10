#!/usr/bin/env python3
"""Exercise Wine's adapter API and verify its cross-architecture wire layout."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--mingw-include", help="MinGW CRT headers for the Windows ABI checks")
options = parser.parse_args()
# Without the option, the headers of the toolchain the Windows side is built with.
if not options.mingw_include:
    bundled = sorted((root / 'horizon-wine/toolchains').glob('llvm-mingw-*/generic-w64-mingw32/include'))
    if bundled:
        options.mingw_include = str(bundled[-1])

with tempfile.TemporaryDirectory(prefix="horizon-nsi-") as directory:
    directory = Path(directory)
    layouts = []
    for target in ("i686-w64-windows-gnu", "x86_64-w64-windows-gnu", "aarch64-linux-gnu"):
        output = directory / (target + ".ll")
        args = ["clang", "--target=" + target, "-ffreestanding", "-D__WINESRC__", "-Iinclude", "-S", "-emit-llvm"]
        if target.startswith("aarch64"):
            args += ["-D_WIN64", "-DWINE_UNIX_LIB"]
        elif options.mingw_include:
            args += ["-isystem", options.mingw_include]
        subprocess.run(args + ["horizon-wine/tests/horizon_nsi_abi.c", "-o", str(output)], cwd=root, check=True)
        line = next(line for line in output.read_text().splitlines() if "@nsi_wire_layout =" in line)
        layouts.append(re.findall(r"i32 (\d+)", line))
    assert layouts[0] and layouts[0] == layouts[1] == layouts[2], layouts
    print("NSI wire sizes/offsets match on i386, AMD64 and native AArch64")
    output = directory / "nsi-test"
    subprocess.run([
        "clang", "-std=gnu11", "-DWIN32_LEAN_AND_MEAN", "-D_WIN64", "-DWINE_UNIX_LIB", "-DWINBASEAPI=",
        "-DWINE_NO_DEBUG_MSGS", "-DWINE_NO_TRACE_MSGS", "-fshort-wchar", "-ffunction-sections",
        "-include", "horizon-wine/tests/nsi_host_shim.h", "-Dwcslen=test_wcslen", "-Dwcscpy=test_wcscpy",
        "-Dwcscmp=test_wcscmp", "-Iinclude", "-Idlls/ntdll/unix", "-g", "-fsanitize=address,undefined",
        "-Wno-format", "-Wno-macro-redefined", "-Wl,--gc-sections", "horizon-wine/tests/horizon_nsi.c",
        "dlls/nsi/nsi.c", "dlls/iphlpapi/iphlpapi_main.c", "-o", str(output)
    ], cwd=root, check=True)
    subprocess.run([str(output)], check=True)
