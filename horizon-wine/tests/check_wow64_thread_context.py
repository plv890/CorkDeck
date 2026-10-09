#!/usr/bin/env python3
"""Check Horizon self/remote WoW64 contexts and startup context failures."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
signal = (root / 'dlls/ntdll/unix/signal_arm64.c').read_text()
functions = signal[signal.index('static NTSTATUS check_thread_context_access('):signal.index('extern void wine_nx_call_pe_apc(')]
wow_start = signal.index('void *get_wow_context( CONTEXT *context )')
wow_context = signal[wow_start:signal.index('\n}\n', wow_start) + 3]
syscall = (root / 'dlls/wow64/syscall.c').read_text()
start = syscall.index('static NTSTATUS thread_init(void)')
init = syscall[start:syscall.index('\n}\n', start) + 3]
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winnt.h"
#include "winternl.h"
#include "horizon_wow64.h"
#include "struct32.h"
#define GetCurrentThread() NtCurrentThread()
#define GetCurrentProcess() NtCurrentProcess()
#define horizon_trace(...) ((void)0)
static TEB teb;
static TEB32 teb32;
#define NtCurrentTeb() (&teb)
#define NtCurrentTeb32() (&teb32)
static WOW64_CPURESERVED cpu = {0, IMAGE_FILE_MACHINE_I386};
static I386_CONTEXT saved;
static I386_CONTEXT remote_saved;
static unsigned remote_gets, remote_sets;
static NTSTATUS remote_error;
static unsigned queries, comparisons;
static NTSTATUS query_error;
struct thread_data {};
static struct thread_data thread_data;
static struct { USHORT Machine; } main_image_info = {IMAGE_FILE_MACHINE_I386};
static struct thread_data *get_thread_data(void) { return &thread_data; }
static void *get_cpu_area(struct thread_data *data, USHORT machine) {
    assert(data == &thread_data && machine == IMAGE_FILE_MACHINE_I386);
    return &saved;
}
NTSTATUS WINAPI NtCompareObjects(HANDLE first, HANDLE second) {
    assert(second == GetCurrentThread());
    comparisons++;
    if (first == (HANDLE)4 || first == (HANDLE)8 || first == (HANDLE)12) return STATUS_SUCCESS;
    if (first == (HANDLE)16) return STATUS_NOT_SAME_OBJECT;
    return STATUS_INVALID_HANDLE;
}
NTSTATUS WINAPI NtQueryObject(HANDLE handle, OBJECT_INFORMATION_CLASS type, void *buffer, ULONG size, ULONG *retlen) {
    OBJECT_BASIC_INFORMATION *info = buffer;
    assert(type == ObjectBasicInformation && size == sizeof(*info) && !retlen);
    queries++;
    if (query_error) return query_error;
    memset(info, 0, sizeof(*info));
    if (handle == (HANDLE)4 || handle == (HANDLE)16) info->GrantedAccess = THREAD_GET_CONTEXT | THREAD_SET_CONTEXT;
    else if (handle == (HANDLE)8) info->GrantedAccess = THREAD_GET_CONTEXT;
    else if (handle == (HANDLE)12) info->GrantedAccess = THREAD_SET_CONTEXT;
    else assert(0);
    return STATUS_SUCCESS;
}
static NTSTATUS get_thread_context(HANDLE handle, void *ctx, BOOL *self, USHORT machine) {
    assert(handle == (HANDLE)16 && machine == IMAGE_FILE_MACHINE_I386);
    *self = FALSE; remote_gets++;
    if (remote_error) return remote_error;
    return horizon_transfer_i386_context(&cpu, &remote_saved, ctx, FALSE);
}
static NTSTATUS set_thread_context(HANDLE handle, const void *ctx, BOOL *self, USHORT machine) {
    assert(handle == (HANDLE)16 && machine == IMAGE_FILE_MACHINE_I386);
    *self = FALSE; remote_sets++;
    if (remote_error) return remote_error;
    return horizon_transfer_i386_context(&cpu, &remote_saved, (I386_CONTEXT *)ctx, TRUE);
}
'''
init_fixture = r'''
static USHORT current_machine = IMAGE_FILE_MACHINE_I386;
static WOW64INFO info, *wow64info = &info;
static SYSTEM_DLL_INIT_BLOCK init_block, *pLdrSystemDllInitBlock = &init_block;
static NTSTATUS get_error, set_error;
static unsigned thread_inits, get_calls, set_calls;
static void *pBTCpuGetBopCode(void) { return (void *)0x110000; }
static void cpu_thread_init(void) { thread_inits++; }
static void (*pBTCpuThreadInit)(void) = cpu_thread_init;
static NTSTATUS pBTCpuGetContext(HANDLE thread, HANDLE process, void *unknown, void *ctx) {
    assert(thread == GetCurrentThread() && process == GetCurrentProcess() && !unknown);
    get_calls++;
    if (get_error) return get_error;
    return get_thread_wow64_context((HANDLE)4, ctx, sizeof(I386_CONTEXT));
}
static NTSTATUS pBTCpuSetContext(HANDLE thread, HANDLE process, void *unknown, void *ctx) {
    assert(thread == GetCurrentThread() && process == GetCurrentProcess() && !unknown);
    set_calls++;
    if (set_error) return set_error;
    return set_thread_wow64_context((HANDLE)4, ctx, sizeof(I386_CONTEXT));
}
#define ERR(...) ((void)0)
'''
tests = r'''
int main(void) {
    I386_CONTEXT ctx = {0}, before;
    void *stack;
    unsigned count;
    teb.TlsSlots[WOW64_TLS_CPURESERVED] = &cpu;
    assert(get_wow_context(NULL) == &saved);
    saved.ContextFlags = ctx.ContextFlags = CONTEXT_I386_FULL;
    saved.Esp = 0x1700000; saved.Eip = 0x110000; saved.Eax = 0x12345678;
    assert(!get_thread_wow64_context(GetCurrentThread(), &ctx, sizeof(ctx)));
    assert(ctx.Esp == saved.Esp && ctx.Eax == saved.Eax && !queries && !comparisons);
    ctx.Esp = 0;
    assert(!get_thread_wow64_context((HANDLE)4, &ctx, sizeof(ctx)) && ctx.Esp == saved.Esp);
    assert(queries == 1 && comparisons == 1);
    ctx.Eax++;
    assert(!set_thread_wow64_context((HANDLE)4, &ctx, sizeof(ctx)) && saved.Eax == ctx.Eax);
    assert(!get_thread_wow64_context((HANDLE)8, &ctx, sizeof(ctx)));
    before = saved;
    assert(set_thread_wow64_context((HANDLE)8, &ctx, sizeof(ctx)) == STATUS_ACCESS_DENIED);
    assert(!memcmp(&before, &saved, sizeof(saved)));
    assert(!set_thread_wow64_context((HANDLE)12, &ctx, sizeof(ctx)));
    before = ctx;
    assert(get_thread_wow64_context((HANDLE)12, &ctx, sizeof(ctx)) == STATUS_ACCESS_DENIED);
    assert(!memcmp(&before, &ctx, sizeof(ctx)));
    count = queries;
    remote_saved = saved; remote_saved.Esp = 0x2800000; remote_saved.Eax = 0x87654321;
    before = saved;
    assert(!get_thread_wow64_context((HANDLE)16, &ctx, sizeof(ctx)));
    assert(ctx.Esp == remote_saved.Esp && ctx.Eax == remote_saved.Eax && remote_gets == 1);
    assert(!memcmp(&before, &saved, sizeof(saved)));
    ctx.Eax++;
    assert(!set_thread_wow64_context((HANDLE)16, &ctx, sizeof(ctx)));
    assert(remote_saved.Eax == ctx.Eax && remote_sets == 1 && !memcmp(&before, &saved, sizeof(saved)));
    remote_error = STATUS_ACCESS_DENIED; before = ctx;
    assert(get_thread_wow64_context((HANDLE)16, &ctx, sizeof(ctx)) == remote_error);
    assert(!memcmp(&before, &ctx, sizeof(ctx)));
    assert(set_thread_wow64_context((HANDLE)16, &ctx, sizeof(ctx)) == remote_error);
    remote_error = 0;
    assert(get_thread_wow64_context(NULL, &ctx, sizeof(ctx)) == STATUS_INVALID_HANDLE);
    assert(set_thread_wow64_context(NULL, &ctx, sizeof(ctx)) == STATUS_INVALID_HANDLE);
    assert(queries == count + 4);
    query_error = STATUS_UNSUCCESSFUL;
    assert(get_thread_wow64_context((HANDLE)4, &ctx, sizeof(ctx)) == query_error);
    query_error = 0;
    assert(get_thread_wow64_context((HANDLE)4, &ctx, 1) == STATUS_INFO_LENGTH_MISMATCH);
    assert(set_thread_wow64_context((HANDLE)4, &ctx, 1) == STATUS_INFO_LENGTH_MISMATCH);
    assert(get_thread_wow64_context((HANDLE)4, NULL, sizeof(ctx)) == STATUS_INVALID_PARAMETER);
    teb.TlsSlots[WOW64_TLS_CPURESERVED] = NULL;
    assert(!get_wow_context(NULL));
    assert(get_thread_wow64_context((HANDLE)4, &ctx, sizeof(ctx)) == STATUS_INVALID_PARAMETER);
    teb.TlsSlots[WOW64_TLS_CPURESERVED] = &cpu;

    get_error = STATUS_NOT_IMPLEMENTED;
    assert(thread_init() == get_error && get_calls == 1 && !set_calls);
    get_error = 0;
    saved.Esp = 0;
    assert(thread_init() == STATUS_BAD_INITIAL_STACK && !set_calls);
    saved.Esp = sizeof(I386_CONTEXT);
    assert(thread_init() == STATUS_BAD_INITIAL_STACK && !set_calls);
    stack = mmap((void *)0x17000000, 65536, PROT_READ | PROT_WRITE,
                 MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE, -1, 0);
    assert(stack == (void *)0x17000000);
    saved.Esp = PtrToUlong((char *)stack + 65536);
    before = saved;
    init_block.pLdrInitializeThunk = 0x210000;
    assert(!thread_init() && set_calls == 1);
    assert(saved.Eip == init_block.pLdrInitializeThunk);
    assert(saved.Esp == before.Esp - sizeof(I386_CONTEXT) - 5 * sizeof(ULONG));
    assert(*(ULONG *)ULongToPtr(saved.Esp) == 0xdeadbabe);
    assert(!memcmp((I386_CONTEXT *)ULongToPtr(before.Esp) - 1, &before, sizeof(before)));
    assert(cpu.Flags & WOW64_CPURESERVED_FLAG_RESET_STATE);
    assert(teb32.WOW32Reserved == 0x110000 && teb.TlsSlots[WOW64_TLS_WOW64INFO] == wow64info);
    saved = before;
    set_error = STATUS_ACCESS_DENIED;
    assert(thread_init() == set_error && set_calls == 2);
    assert(!memcmp(&before, &saved, sizeof(saved)));
    munmap(stack, 65536);
    current_machine = IMAGE_FILE_MACHINE_ARMNT;
    get_error = STATUS_NOT_IMPLEMENTED;
    assert(thread_init() == get_error);
    current_machine = 0;
    assert(thread_init() == STATUS_INVALID_IMAGE_FORMAT);
    puts("WoW64 context: self aliases, access rights, server-owned remote contexts/errors and checked loader frame passed");
}
'''
with tempfile.TemporaryDirectory(prefix='wow64-context-') as directory:
    build = Path(directory)
    file = build / 'context.c'
    file.write_text(fixture + wow_context + functions + init_fixture + init + tests)
    subprocess.run([os.environ.get('WINE_NX_HOST_CC', '/usr/bin/clang'), '-std=gnu11',
                    '-fms-extensions', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers',
                    '-fsanitize=address,undefined', '-no-pie', '-D__WINESRC__', '-D_WIN64',
                    '-I' + str(root / 'include'), '-I' + str(root / 'dlls/ntdll/unix'),
                    '-I' + str(root / 'dlls/wow64'),
                    str(file), '-o', str(build / 'context')], check=True)
    subprocess.run([str(build / 'context')], check=True)
