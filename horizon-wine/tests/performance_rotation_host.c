/* Exercise the real collector's minute rotation without a minute-long sleep. */
#include "../source/performance.c"
#include <assert.h>
static unsigned lines;
void wine_nx_runtime_trace(const char *line) { assert(line && *line); lines++; }
int main(void)
{
    struct nx_perf_token t;
    uint64_t gen, session_start;
    unsigned before;
    wine_nx_perf_start(NX_PERF_CONTINUOUS);
    gen=generation; session_start=started;
    t=wine_nx_perf_begin(NX_PERF_SUBMIT);
    wine_nx_perf_end(t,NX_PERF_SUBMIT,4,0x111,0);
    assert(buckets[0].n==1);
    window_started=now_ns()-UINT64_C(61000000000);
    wine_nx_perf_report();
    assert(lines>=2 && generation==gen+1 && started==session_start);
    assert(deadline==UINT64_MAX && !buckets[0].n);
    before=lines;
    wine_nx_perf_stop();
    wine_nx_perf_report();
    assert(lines>before);
    before=lines;
    wine_nx_perf_report();
    assert(lines==before);
}
