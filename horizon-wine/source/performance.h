/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#ifndef WINE_NX_PERFORMANCE_H
#define WINE_NX_PERFORMANCE_H
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#define NX_PERF_CONTINUOUS UINT_MAX
enum nx_perf_kind { NX_PERF_QPC, NX_PERF_WAIT, NX_PERF_DELAY, NX_PERF_PRESENT,
    NX_PERF_SUBMIT, NX_PERF_FENCE, NX_PERF_ACQUIRE, NX_PERF_PIPELINE, NX_PERF_KINDS };
struct nx_perf_token { uint64_t tick, generation; uint32_t saved_guest_eip, tid;
    uint64_t cpu_start_ns; unsigned cpu_valid; };
struct nx_perf_token wine_nx_perf_begin(unsigned kind);
void wine_nx_perf_end(struct nx_perf_token token, unsigned kind, unsigned tid,
                      uintptr_t native_caller, int result);
void wine_nx_perf_start(unsigned seconds);
void wine_nx_perf_stop(void);
int wine_nx_perf_read(char *out, size_t capacity);
int wine_nx_perf_control(const char *action, char *out, size_t capacity);
void wine_nx_perf_report(void);
#endif
