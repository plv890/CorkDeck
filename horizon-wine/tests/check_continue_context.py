#!/usr/bin/env python3
"""Check production CONTEXT-to-libnx conversion; hardware tests cover the branch."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'dlls/ntdll/unix/horizon.c').read_text()
start = source.index('void horizon_continue_context( const CONTEXT *context )')
end = source.index('\n}\n', start) + 3
function = source[start:end]
asm_start = function.index('    __asm__ volatile(')
asm_end = function.index('    horizon_restore_exception_context', asm_start)
function = function[:asm_start] + function[asm_end:]
fixture = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define CONTEXT_ARM64 0x400000
#define CONTEXT_ARM64_X18 0x400010
typedef union { uint64_t x; } reg;
typedef struct { reg cpu_gprs[29], fp, lr, sp, pc; uint32_t pstate; unsigned char fpu_gprs[512]; } ThreadExceptionDump;
typedef uintptr_t ULONG_PTR;
typedef struct {
    uint32_t ContextFlags;
    union { struct { uint64_t X0_17[18], X18, X19_28[10]; }; uint64_t X[29]; };
    uint64_t Fp, Lr, Sp, Pc; uint32_t Cpsr, Fpcr, Fpsr; unsigned char V[512];
} CONTEXT;
static ThreadExceptionDump result;
/* Under FEX the translator continues the context; it gets the same x18 rule. */
static int uses_fex, fex_calls;
static CONTEXT fex_resume;
static int horizon_uses_fex(void) { return uses_fex; }
static void wine_nx_fex_continue_context(const CONTEXT *context) { fex_resume = *context; fex_calls++; }
static void *NtCurrentTeb(void) { return (void *)0x7ffc0000; }
static void horizon_restore_exception_context(ThreadExceptionDump *dump) { result = *dump; }
'''
tests = r'''
int main(void) {
    CONTEXT ctx = {0};
    for (int i = 0; i < 29; ++i) ctx.X[i] = 100 + i;
    ctx.ContextFlags = 0x400007;
    ctx.Fp = 200; ctx.Lr = 300; ctx.Sp = 400; ctx.Pc = 500; ctx.Cpsr = 0xa0000000;
    memset(ctx.V, 0x57, sizeof(ctx.V));
    horizon_continue_context(&ctx);
    for (int i = 0; i < 29; ++i) assert(result.cpu_gprs[i].x == (i == 18 ? 0x7ffc0000 : ctx.X[i]));
    assert(result.fp.x == 200 && result.lr.x == 300 && result.sp.x == 400 && result.pc.x == 500);
    assert(result.pstate == ctx.Cpsr && !memcmp(result.fpu_gprs, ctx.V, sizeof(ctx.V)));
    ctx.ContextFlags |= CONTEXT_ARM64_X18;
    horizon_continue_context(&ctx);
    assert(result.cpu_gprs[18].x == 118);
    assert(!fex_calls);
    uses_fex = 1;
    horizon_continue_context(&ctx);
    assert(fex_calls == 1 && fex_resume.X18 == 118 && fex_resume.Pc == 500);
    ctx.ContextFlags = 0x400007;
    horizon_continue_context(&ctx);
    assert(fex_calls == 2 && fex_resume.X18 == 0x7ffc0000 && fex_resume.X[17] == 117 && ctx.X18 == 118);
    puts("NtContinue context conversion: integer/control/SIMD and implicit/explicit x18, natively and under FEX, passed");
}
'''
with tempfile.TemporaryDirectory(prefix='wine-nx-continue-') as tmp:
    c = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    c.write_text(fixture + function + tests)
    subprocess.run(['clang', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
