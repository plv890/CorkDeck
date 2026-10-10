#!/usr/bin/env python3
"""Run ntdll's actual ARM64 setjmp/longjmp code on an ARM64 host.

Only the object-format assembler directives are replaced. RtlUnwind is a trap:
NULL-frame returns must bypass it, while ordinary frames must still reach it.
"""
import ast
from pathlib import Path
import platform
import re
import subprocess
import tempfile
import arm64_host

root = Path(__file__).resolve().parents[2]
source = (root / 'dlls/ntdll/signal_arm64.c').read_text()
cc = arm64_host.compiler('clang')
prefix = '_' if platform.system() == 'Darwin' else ''

def assembly(name):
    block = source.split('__ASM_GLOBAL_FUNC( ' + name + ',', 1)[1].split(' )', 1)[0]
    code = ''.join(ast.literal_eval(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', block))
    code = re.sub(r'\.seh_\w+[^\n]*\n', '', code)
    return f'.p2align 2\n.globl {prefix}{name}\n{prefix}{name}:\n' + code + '\n'

body = source.split('void __cdecl NTDLL_longjmp( _JUMP_BUFFER *buf, int retval )', 1)[1]
body = body[:body.index('\n}') + 2]
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
typedef struct {
    uint64_t Frame, Reserved, X[10], Fp, Lr, Sp;
    uint32_t Fpcr, Fpsr;
    double D[8];
} _JUMP_BUFFER;
_Static_assert(sizeof(_JUMP_BUFFER) == 0xc0, "ARM64 jump buffer ABI");
typedef struct {
    uint32_t ExceptionCode, ExceptionFlags;
    void *ExceptionRecord, *ExceptionAddress;
    uint32_t NumberParameters;
    uintptr_t ExceptionInformation[15];
} EXCEPTION_RECORD;
#define STATUS_LONGJUMP 0x80000026u
#define DWORD_PTR uintptr_t
#define IntToPtr(x) ((void *)(intptr_t)(x))
extern int NTDLL__setjmpex(_JUMP_BUFFER *, void *) __attribute__((returns_twice));
extern void longjmp_regs(_JUMP_BUFFER *, int) __attribute__((noreturn));
static void RtlUnwind(void *frame, void *target, EXCEPTION_RECORD *rec, void *value) {
    /* Reaching this from a NULL-frame jump is the build-23 regression. */
    _exit(frame && target && rec->ExceptionCode == STATUS_LONGJUMP && value ? 42 : 91);
}
void NTDLL_longjmp(_JUMP_BUFFER *buf, int retval)
'''
tests = r'''
static void jump_deep(_JUMP_BUFFER *buf, int value, int depth) {
    volatile uint64_t stack[64];
    stack[depth] = (uint64_t)depth;
    if (depth) jump_deep(buf, value, depth - 1);
    /* Force a different FP rounding mode and exception status. */
    __asm__ volatile("msr fpcr, %0\n\tmsr fpsr, %1" :: "r"((uint64_t)0x400000), "r"((uint64_t)1));
    NTDLL_longjmp(buf, value);
    abort();
}
static void exercise(int value) {
    _JUMP_BUFFER outer, inner;
    volatile int stage = 0;
    uint64_t fpcr, fpsr;
    int result;
    __asm__ volatile("mrs %0, fpcr\n\tmrs %1, fpsr" : "=r"(fpcr), "=r"(fpsr));
    result = NTDLL__setjmpex(&outer, NULL);
    if (!result) {
        if (!NTDLL__setjmpex(&inner, NULL)) jump_deep(&inner, 7, 8);
        stage = 1;
        jump_deep(&outer, value, 12);
    }
    assert(result == (value ? value : 1) && stage == 1);
    uint64_t actual_fpcr, actual_fpsr;
    __asm__ volatile("mrs %0, fpcr\n\tmrs %1, fpsr" : "=r"(actual_fpcr), "=r"(actual_fpsr));
    assert(actual_fpcr == fpcr && actual_fpsr == fpsr);
}
int main(void) {
    for (int i = 0; i < 100; ++i) { exercise(0); exercise(1); exercise(37); exercise(-5); }
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        _JUMP_BUFFER buf;
        if (!NTDLL__setjmpex(&buf, &buf)) NTDLL_longjmp(&buf, 1);
        _exit(92);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 42);
    puts("ARM64 ntdll longjmp: nested NULL-frame returns, stack/FP restoration, zero/nonzero values and normal unwind routing passed");
}
'''
with tempfile.TemporaryDirectory(prefix='wine-nx-longjmp-') as tmp:
    folder = Path(tmp)
    (folder / 'test.c').write_text(fixture + body + tests)
    (folder / 'jumps.S').write_text('.text\n' + assembly('NTDLL__setjmpex') + assembly('longjmp_regs'))
    for optimization in ('-O0', '-O2'):
        subprocess.run(cc + [optimization, '-Wall', '-Wextra', '-Werror', str(folder / 'test.c'),
                        str(folder / 'jumps.S'), '-o', str(folder / 'test')], check=True)
        subprocess.run(arm64_host.run(folder / 'test'), check=True)
    # Prove that the prior implementation fails this exact test.
    old_body = body.replace('    if (!buf->Frame) longjmp_regs( buf, retval );\n', '')
    assert old_body != body
    (folder / 'test.c').write_text(fixture + old_body + tests)
    subprocess.run(cc + ['-O2', str(folder / 'test.c'), str(folder / 'jumps.S'),
                    '-o', str(folder / 'baseline')], check=True)
    assert subprocess.run(arm64_host.run(folder / 'baseline')).returncode == 91
    print('Build-23 regression reproduced: NULL frame still entered RtlUnwind')
