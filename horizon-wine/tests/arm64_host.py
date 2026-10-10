"""A compiler for ARM64 code and a way to run what it builds.

Some fixtures run ntdll's ARM64 code itself rather than a model of it. On an
ARM64 machine the host compiler builds them and they run as they are. Anywhere
else a cross compiler and an emulator do: aarch64-linux-gnu-gcc and
qemu-aarch64 (Debian and Ubuntu: gcc-aarch64-linux-gnu, g++-aarch64-linux-gnu,
qemu-user). With
neither the test is skipped, which is exit status 77; host_tests.py counts
that apart from the failures.
"""
import platform
import shutil
import sys

NATIVE = platform.machine().lower() in ('arm64', 'aarch64')
SKIPPED = 77


def skip(why):
    print('SKIP: ' + why)
    sys.exit(SKIPPED)


def compiler(native='cc', cxx=False):
    """The start of a command line that compiles and links for ARM64: C, or
    C++ with cxx."""
    if NATIVE:
        return [native]
    if shutil.which('qemu-aarch64') and shutil.which('aarch64-linux-gnu-g++' if cxx else 'aarch64-linux-gnu-gcc'):
        # Static, so that the emulator needs no ARM64 loader and libraries.
        # x18 is the TEB's, as on Windows and the console.
        if cxx and shutil.which('clang++'):
            # FEX's headers are written for clang; the cross g++ brings the C++ library.
            return ['clang++', '--target=aarch64-linux-gnu', '-static', '-ffixed-x18']
        # The last options are for warnings the fixtures' usual compiler, clang, does not have.
        return ['aarch64-linux-gnu-g++' if cxx else 'aarch64-linux-gnu-gcc', '-static', '-ffixed-x18',
                '-Wno-unused-but-set-variable', '-Wno-clobbered']
    skip('needs an ARM64 host, or aarch64-linux-gnu-gcc (g++ for C++) with qemu-aarch64')


def flags(arguments):
    """The compiler arguments, without what only the host's own compiler has:
    the sanitizer runtimes are not there for the cross compiler."""
    if NATIVE:
        return list(arguments)
    return [a for a in arguments if not a.startswith(('-fsanitize', '-fno-sanitize'))]


def run(binary):
    """The command line that runs a program compiler() built."""
    return [str(binary)] if NATIVE else ['qemu-aarch64', str(binary)]
