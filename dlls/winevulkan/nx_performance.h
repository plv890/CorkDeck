/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#ifdef __SWITCH__
#include "../../horizon-wine/source/performance_wine.h"
#define NX_VK_PERF_BEGIN(kind) struct nx_perf_token nx_perf = nx_perf_wine_begin(kind)
#define NX_VK_PERF_END(kind) do { if (nx_perf.tick) wine_nx_perf_end(nx_perf, kind, \
    nx_perf.tid, \
    (uintptr_t)__builtin_return_address(0), params->result); } while (0)
#else
#define NX_VK_PERF_BEGIN(kind)
#define NX_VK_PERF_END(kind)
#endif
