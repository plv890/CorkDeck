#include "../source/diagnostics/fault_watch.h"
#include <assert.h>
int main(void)
{
    assert(wine_nx_fault_kind("0004:err:dialog:MessageBoxIndirectW Fatal error in gc") == 1);
    assert(wine_nx_fault_kind("[NXWIN] Rendering thread exception: Fatal error!") == 2);
    assert(wine_nx_fault_kind("err:module:loader_init BASS.dll failed to initialize, aborting") == 3);
    assert(wine_nx_fault_kind("Unhandled page fault on read") == 4);
    assert(wine_nx_fault_kind("[EXC] unhandled status=0xc0000005; parking thread\n") == 4);
    assert(!wine_nx_fault_kind("[EXC] unhandled status=0xc00000050; parking thread"));
    assert(!wine_nx_fault_kind("[EXC] unhandled status=0xc0000005; recovered"));
    assert(wine_nx_fault_kind("[FAIL] map target status=c0000135") == 5);
    assert(wine_nx_fault_kind("err:dialog: Fatal error!") == 6);
    assert(!wine_nx_fault_kind("err: DXGI: Failed to parse display metadata, using blank"));
    assert(!wine_nx_fault_kind("[STARTUP] Initializing graphics; waiting"));
    assert(!wine_nx_fault_kind("[STALL] no frame drawn for 15 s"));
    assert(!wine_nx_fault_kind("GetThreadContext failed is mentioned without a fatal dialog"));
    assert(wine_nx_fault_kind("[EXIT] NtTerminateProcess(self) exit_code=0xc0000005") == 7);
    assert(wine_nx_fault_kind("[EXIT] NtTerminateProcess(self) exit_code=0xc00000fd\n") == 7);
    assert(wine_nx_fault_kind("[EXIT] NtTerminateProcess(self) exit_code=0xc0000135") == 7);
    assert(!wine_nx_fault_kind("[EXIT] NtTerminateProcess(self) exit_code=0x00000000"));
    assert(!wine_nx_fault_kind("[EXIT] NtTerminateProcess(self) exit_code=0xc000013a"));
    assert(!wine_nx_fault_kind("[EXIT] NtTerminateProcess(self) exit_code=0xc00000050"));
    assert(!wine_nx_fault_kind("unrelated text exit_code=0xc0000005"));
    return 0;
}
