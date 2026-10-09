/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 * Passive bounded wall-time samples. No thread suspension, allocations, disk
 * writes or blocking locks in instrumented calls. Native callers are NOT x86 PCs.
 */
#include "performance.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
#define SLOTS 96
struct bucket { unsigned kind, tid, guest; uintptr_t caller; uint64_t n, ns, max, slow, errors, cpu_n, cpu_ns; };
static struct bucket buckets[SLOTS];
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t generation, deadline, started, window_started, dropped;
static __thread unsigned samples[NX_PERF_KINDS];
static const char *names[NX_PERF_KINDS] = {"qpc", "nt-wait", "nt-delay", "vk-present",
    "vk-submit", "vk-fence", "vk-acquire", "vk-pipeline"};
#ifdef __SWITCH__
#ifndef static_assert   /* libnx's service calls with buffers use it */
#define static_assert _Static_assert
#endif
/* What Horizon OC itself says beside the clocks read above: the power mode it
 * is in, the clocks it aims at and the ones it set, its list of graphics
 * clocks, load and temperature. A clock that stays below the profile's is
 * explained here or nowhere: the mode, the list, the governor and the heat
 * are all its own. The first words of its report are laid out for clients
 * (hocclk/clock_manager.h, "stable"); the application and the mode follow. */
static void report_clock_manager(void)
{
    static const char *const modes[] = {"handheld", "charging", "charging-usb", "charging-official", "docked"};
    extern void wine_nx_runtime_trace(const char *);
    struct { u32 module, max; } list_args = {1 /* graphics */, 64};
    static u32 context[0x500 / 4];
    u32 list[64], count = 0, mode, i;
    u64 nothing = 0;
    char line[512];
    Service service;
    int n;

    if (R_FAILED(smGetService(&service, "hoc:clk"))) return;
    if (R_SUCCEEDED(serviceDispatchIn(&service, 2, nothing,
            .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
            .buffers = { { context, sizeof(context) } })))
    {
        mode = context[48];
        snprintf(line, sizeof(line), "[HOC] mode=%s application=%08x%08x set cpu=%u gpu=%u mem=%u MHz, "
                 "real cpu=%u gpu=%u mem=%u, governor=%#x, override cpu=%u gpu=%u, load cpu-max=%u gpu=%u, "
                 "temperature soc=%d gpu=%d C, gpu rail %u uV",
                 mode < 5 ? modes[mode] : "?", context[47], context[46],
                 context[0] / 1000000, context[1] / 1000000, context[2] / 1000000,
                 context[5] / 1000000, context[6] / 1000000, context[7] / 1000000, context[3],
                 context[10] / 1000000, context[11] / 1000000, context[31], context[30],
                 (int)context[15] / 1000, (int)context[21] / 1000, context[41]);
        wine_nx_runtime_trace(line);
    }
    if (R_SUCCEEDED(serviceDispatchInOut(&service, 11, list_args, count,
            .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
            .buffers = { { list, sizeof(list) } })) && count && count <= 64)
    {
        n = snprintf(line, sizeof(line), "[HOC] graphics clocks it can set, MHz:");
        for (i = 0; i < count && n < (int)sizeof(line) - 8; i++) n += snprintf(line + n, sizeof(line) - n, " %u", list[i] / 1000000);
        wine_nx_runtime_trace(line);
    }
    serviceClose(&service);
}

static void report_clocks(void)
{
    const PcvModuleId ids[3] = {PcvModuleId_CpuBus, PcvModuleId_GPU, PcvModuleId_EMC};
    const char *labels[3] = {"cpu", "gpu", "emc"};
    extern void wine_nx_runtime_trace(const char *);
    Result init = clkrstInitialize();
    unsigned i;
    char line[128];
    for (i=0; i<3; i++) {
        ClkrstSession session;
        u32 hz = 0;
        Result rc = init;
        if (R_SUCCEEDED(rc)) {
            rc = clkrstOpenSession(&session, ids[i], 3);
            if (R_SUCCEEDED(rc)) {
                rc = clkrstGetClockRate(&session, &hz);
                clkrstCloseSession(&session);
            }
        }
        snprintf(line, sizeof(line), "[PERF-CLOCK] module=%s hz=%u result=%08x source=clkrst-read", labels[i], hz, rc);
        wine_nx_runtime_trace(line);
    }
    if (R_SUCCEEDED(init)) clkrstExit();
    report_clock_manager();
}
#endif
static uint64_t now_ns(void)
{
#ifdef __SWITCH__
    return armTicksToNs(armGetSystemTick());
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
#endif
}
static int thread_cpu_ns(uint64_t *out)
{
#ifdef __SWITCH__
    u64 ticks;
    if (R_FAILED(svcGetInfo(&ticks, InfoType_ThreadTickCount, CUR_THREAD_HANDLE, UINT64_MAX))) return 0;
    *out = armTicksToNs(ticks);
#elif defined(CLOCK_THREAD_CPUTIME_ID)
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts)) return 0;
    *out = (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
#else
    return 0;
#endif
    return 1;
}
struct nx_perf_token wine_nx_perf_begin(unsigned kind)
{
    struct nx_perf_token t = {0};
    uint64_t end = __atomic_load_n(&deadline, __ATOMIC_ACQUIRE);
    unsigned mask;
    if (!end || kind >= NX_PERF_KINDS) return t;
    mask = kind == NX_PERF_QPC ? 1023 : kind <= NX_PERF_DELAY ? 63 : 0;
    if (++samples[kind] & mask) return t;
    t.generation = __atomic_load_n(&generation, __ATOMIC_ACQUIRE);
    t.tick = now_ns();
    if (t.tick >= end) {
        __atomic_compare_exchange_n(&deadline, &end, 0, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        t.tick = 0;
    }
    if (t.tick && kind == NX_PERF_PIPELINE) t.cpu_valid = thread_cpu_ns(&t.cpu_start_ns);
    return t;
}
void wine_nx_perf_end(struct nx_perf_token t, unsigned kind, unsigned tid,
                      uintptr_t caller, int result)
{
    uint64_t elapsed, cpu_end = 0;
    int cpu_valid;
    unsigned i, empty = SLOTS;
    struct bucket *b;
    if (!t.tick || kind >= NX_PERF_KINDS) return;
    elapsed = now_ns() - t.tick;
    cpu_valid = t.cpu_valid && thread_cpu_ns(&cpu_end) && cpu_end >= t.cpu_start_ns;
    if (pthread_mutex_trylock(&mutex)) { __atomic_add_fetch(&dropped, 1, __ATOMIC_RELAXED); return; }
    if (t.generation != generation) { pthread_mutex_unlock(&mutex); return; }
    for (i = 0; i < SLOTS; i++) {
        b = &buckets[i];
        if (!b->n && empty == SLOTS) empty = i;
        if (b->n && b->kind == kind && b->tid == tid && b->caller == caller && b->guest == t.saved_guest_eip) break;
    }
    if (i == SLOTS) i = empty;
    if (i == SLOTS) { __atomic_add_fetch(&dropped, 1, __ATOMIC_RELAXED); pthread_mutex_unlock(&mutex); return; }
    b = &buckets[i]; b->kind = kind; b->tid = tid; b->caller = caller; b->guest = t.saved_guest_eip;
    b->n++; b->ns += elapsed;
    if (cpu_valid) { b->cpu_n++; b->cpu_ns += cpu_end - t.cpu_start_ns; }
    if (elapsed > b->max) b->max = elapsed;
    if (elapsed >= 1000000) b->slow++;
    if (result < 0) b->errors++;
    pthread_mutex_unlock(&mutex);
}
void wine_nx_perf_start(unsigned seconds)
{
    if (!seconds) { wine_nx_perf_stop(); return; }
    if (seconds > 300 && seconds != NX_PERF_CONTINUOUS) seconds = 300;
    pthread_mutex_lock(&mutex);
    __atomic_store_n(&deadline, 0, __ATOMIC_RELEASE);
    memset(buckets, 0, sizeof(buckets));
    __atomic_store_n(&dropped, 0, __ATOMIC_RELAXED);
    __atomic_add_fetch(&generation, 1, __ATOMIC_RELEASE);
    started = now_ns();
    window_started = started;
    __atomic_store_n(&deadline, seconds == NX_PERF_CONTINUOUS ? UINT64_MAX : started + (uint64_t)seconds * 1000000000, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&mutex);
}
void wine_nx_perf_stop(void) { __atomic_store_n(&deadline, 0, __ATOMIC_RELEASE); }
int wine_nx_perf_read(char *out, size_t capacity)
{
    struct bucket snapshot[SLOTS], swap;
    uint64_t end, start, window, gen, drops, now;
    unsigned i, j, count = 0;
    size_t used;
    int n;
    if (!out || capacity < 2) return EINVAL;
    out[0] = 0;
    if (pthread_mutex_trylock(&mutex)) return EBUSY;
    memcpy(snapshot, buckets, sizeof(snapshot));
    end = __atomic_load_n(&deadline, __ATOMIC_ACQUIRE); start = started;
    window = window_started;
    gen = generation; drops = __atomic_load_n(&dropped, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&mutex);
    now = now_ns();
    for (i = 0; i < SLOTS; i++) if (snapshot[i].n) {
        swap = snapshot[i]; j = count++;
        while (j && snapshot[j-1].ns < swap.ns) { snapshot[j] = snapshot[j-1]; j--; }
        snapshot[j] = swap;
    }
    n = snprintf(out, capacity, "[PERF] v=1 generation=%llu active=%d elapsed_ms=%llu window_ms=%llu continuous=%d buckets=%u dropped=%llu sample=qpc:1024,nt:64,vk:1 scope=native-wall caller=native\n",
        (unsigned long long)gen, !!end && now < end,
        (unsigned long long)(start && now >= start ? (now-start)/1000000 : 0),
        (unsigned long long)(window && now >= window ? (now-window)/1000000 : 0),
        end == UINT64_MAX, count, (unsigned long long)drops);
    used = n > 0 && (size_t)n < capacity ? (size_t)n : capacity-1;
    for (i = 0; i < count && i < 8; i++) {
        struct bucket *b = &snapshot[i];
        char line[320];
        n = snprintf(line, sizeof(line), "[PERF-CALL] kind=%s tid=%x caller=%llx saved_eip=%x n=%llu ns=%llu max_ns=%llu ge1ms=%llu errors=%llu cpu_n=%llu cpu_ns=%llu\n",
            names[b->kind], b->tid, (unsigned long long)b->caller, b->guest, (unsigned long long)b->n,
            (unsigned long long)b->ns, (unsigned long long)b->max,
            (unsigned long long)b->slow, (unsigned long long)b->errors,
            (unsigned long long)b->cpu_n, (unsigned long long)b->cpu_ns);
        if (n <= 0 || (size_t)n >= sizeof(line) || (size_t)n >= capacity-used) break;
        memcpy(out+used, line, n+1); used += n;
    }
    return 0;
}
int wine_nx_perf_control(const char *action, char *out, size_t capacity)
{
    if (!strcmp(action, "start")) wine_nx_perf_start(NX_PERF_CONTINUOUS);
    else if (!strcmp(action, "stop")) wine_nx_perf_stop();
    else if (strcmp(action, "read")) return EINVAL;
    return wine_nx_perf_read(out, capacity);
}
void wine_nx_perf_report(void)
{
    char out[2048], *line, *next;
    static uint64_t finished_generation;
    uint64_t gen = __atomic_load_n(&generation, __ATOMIC_ACQUIRE);
    uint64_t end = __atomic_load_n(&deadline, __ATOMIC_ACQUIRE);
    extern void wine_nx_runtime_trace(const char *) __attribute__((weak));
    if (!gen || finished_generation == gen || !wine_nx_runtime_trace) return;
    if (wine_nx_perf_read(out, sizeof(out))) return;
    for (line = out; *line; line = next) {
        next = strchr(line, '\n'); if (!next) break;
        *next++ = 0; wine_nx_runtime_trace(line);
    }
#ifdef __SWITCH__
    if (end && now_ns() < end) report_clocks();
#endif
    if (!end || now_ns() >= end) finished_generation = gen;
    /* Preserve every interval in the existing log, but release stale startup
     * caller slots so later gameplay can use the same fixed storage. */
    if (end == UINT64_MAX && !pthread_mutex_trylock(&mutex)) {
        if (generation == gen && now_ns() - window_started >= UINT64_C(60000000000)) {
            memset(buckets, 0, sizeof(buckets));
            __atomic_store_n(&dropped, 0, __ATOMIC_RELAXED);
            __atomic_add_fetch(&generation, 1, __ATOMIC_RELEASE);
            window_started = now_ns();
        }
        pthread_mutex_unlock(&mutex);
    }
}
