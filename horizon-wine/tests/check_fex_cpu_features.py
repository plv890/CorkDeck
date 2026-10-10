#!/usr/bin/env python3
"""Run the Horizon CPU feature bridge and CRC32 JIT on an AArch64 host."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import arm64_host

root = Path(__file__).resolve().parents[2]
horizon_wine = root / "horizon-wine"
source = Path(os.environ.get("WINE_NX_FEX_DIR", horizon_wine / "toolchains/fex-2609"))
build = Path(os.environ.get("WINE_NX_FEX_CORE_BUILD_DIR", horizon_wine / "toolchains/build-fex-2609-core-check"))
# FEX's core is built for the machine it runs on (check-fex-core.sh configures
# it), which here has to be the one it translates for.
if not arm64_host.NATIVE or not (build / "build.ninja").is_file():
    arm64_host.skip("needs an AArch64 Linux host with FEX's core configured by check-fex-core.sh")
subprocess.run(["cmake", "--build", str(build), "--target", "FEXCore", "JemallocDummy", "Common", "-j", "4"], check=True)
definitions = re.findall(r"^#define (CPU_FEATURE_\w+) (0x[0-9a-fA-F]+)",
                         (source / "Source/Windows/include/winternl.h").read_text(), re.M)
assert definitions
with tempfile.TemporaryDirectory(prefix="fex-cpu-") as directory:
    output = Path(directory) / "cpu-features"
    args = [os.environ.get("WINE_NX_HOST_CXX", "clang++"), "-std=c++20", "-O2", "-g", "-Wall", "-Wextra",
            "-DARCHITECTURE_arm64=1", "-DFEX_DISABLE_TELEMETRY=1", "-DFEX_HORIZON_CACHE_TEST", "-DFEX_HORIZON",
            "-D__WINESRC__", "-DWINE_UNIX_LIB", "-D_WIN64", "-DWIN32_LEAN_AND_MEAN", "-fshort-wchar"]
    args += ["-D" + name + "=" + value for name, value in definitions]
    for path in (root / "include", horizon_wine / "fex", source / "FEXCore/include", source / "FEXHeaderUtils",
                 source / "FEXCore/Source", source / "Source", source / "External/fmt/include",
                 source / "External/unordered_dense/include", source / "External/xxhash",
                 build / "generated", build / "include", build / "FEXCore/Source"):
        args += ["-isystem", str(path)]
    args += [str(horizon_wine / "tests/fex_cpu_features.cpp"), str(source / "Source/Windows/Common/CPUFeatures.cpp"),
             "-Wl,--start-group"]
    args += [str(build / library) for library in (
        "FEXCore/Source/libFEXCore.a", "FEXCore/Source/libFEXCore_Base.a", "FEXCore/Source/libJemallocDummy.a",
        "Source/Common/libCommon.a", "External/fmt/libfmt.a", "External/xxhash/cmake_unofficial/libxxhash.a",
        "External/cephes/libcephes_128bit.a", "External/SoftFloat-3e/libsoftfloat_3e.a")]
    args += ["-Wl,--end-group", "-pthread", "-o", str(output)]
    subprocess.run(args, check=True)
    for mode in ("32", "64"):
        subprocess.run([str(output), mode], check=True)
