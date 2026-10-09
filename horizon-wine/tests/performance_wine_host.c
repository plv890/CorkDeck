/* Model only the Wine TLS contract; native compiler checks the actual ABI. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
typedef struct { uint16_t Flags, Machine; } WOW64_CPURESERVED;
typedef struct { uint32_t Eip; } I386_CONTEXT;
typedef struct { void *TlsSlots[4]; struct { void *UniqueThread; } ClientId; } TEB;
#define WOW64_TLS_CPURESERVED 3
#define IMAGE_FILE_MACHINE_I386 0x14c
static TEB *current;
static TEB *NtCurrentTeb(void) { return current; }
#include "../source/performance_wine.h"
int main(void)
{
    TEB teb = {0};
    struct { WOW64_CPURESERVED cpu; I386_CONTEXT ctx; } area = {{0,0x14c},{0x12345678}};
    struct nx_perf_token t;
    wine_nx_perf_start(300);
    t=nx_perf_wine_begin(NX_PERF_PRESENT);
    assert(t.tick && !t.tid && !t.saved_guest_eip); /* native worker */
    wine_nx_perf_end(t,NX_PERF_PRESENT,t.tid,0x111,0);
    current=&teb;
    teb.TlsSlots[3]=&area;
    teb.ClientId.UniqueThread=(void *)(uintptr_t)0x88;
    t=nx_perf_wine_begin(NX_PERF_PRESENT);
    assert(t.tick && t.tid==0x88 && t.saved_guest_eip==0x12345678);
    current=NULL; /* target callback changed TLS; end uses captured identity */
    wine_nx_perf_end(t,NX_PERF_PRESENT,t.tid,0x111,0);
    wine_nx_perf_stop();
    assert(!nx_perf_wine_begin(NX_PERF_PRESENT).tick);
}
