/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 * Only read the calling thread's own Wine CPU area, never arbitrary guest RAM.
 * This is saved WOW64 state, not an asynchronously sampled instruction pointer.
 */
#ifndef WINE_NX_PERFORMANCE_WINE_H
#define WINE_NX_PERFORMANCE_WINE_H
#include "performance.h"
static inline struct nx_perf_token nx_perf_wine_begin(unsigned kind)
{
    struct nx_perf_token t = wine_nx_perf_begin(kind);
    if (t.tick) {
        /* Unix TLS can be NULL on native workers. Capture once before calling
         * a handler; do not assume every native QPC user is a Wine thread. */
        TEB *teb = NtCurrentTeb();
        WOW64_CPURESERVED *cpu = teb ? teb->TlsSlots[WOW64_TLS_CPURESERVED] : NULL;
        if (teb) t.tid = (unsigned)(uintptr_t)teb->ClientId.UniqueThread;
        if (cpu && cpu->Machine == IMAGE_FILE_MACHINE_I386)
            t.saved_guest_eip = ((I386_CONTEXT *)(cpu + 1))->Eip;
    }
    return t;
}
#endif
