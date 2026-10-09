/* Synthetic collector checks, no games. */
#include "../source/performance.h"
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static void *worker(void *unused)
{
    unsigned i;
    (void)unused;
    for (i=0; i<10000; i++) {
        struct nx_perf_token t = wine_nx_perf_begin(NX_PERF_SUBMIT);
        wine_nx_perf_end(t,NX_PERF_SUBMIT,2,0x222,0);
    }
    return NULL;
}
int main(void)
{
    char out[4096];
    struct nx_perf_token t;
    pthread_t threads[4];
    struct timespec delay = {0,3000000};
    unsigned i,n=0;
    assert(!wine_nx_perf_begin(NX_PERF_QPC).tick);
    wine_nx_perf_start(300);
    for(i=0;i<1024;i++) n += !!wine_nx_perf_begin(NX_PERF_QPC).tick;
    assert(n==1);
    t=wine_nx_perf_begin(NX_PERF_FENCE);
    nanosleep(&delay,NULL);
    wine_nx_perf_end(t,NX_PERF_FENCE,1,0x111,-1);
    assert(!wine_nx_perf_read(out,sizeof(out)));
    assert(strstr(out,"vk-fence") && strstr(out,"ge1ms=1 errors=1"));
    t=wine_nx_perf_begin(NX_PERF_PIPELINE);
    assert(t.cpu_valid);
    nanosleep(&delay,NULL);
    wine_nx_perf_end(t,NX_PERF_PIPELINE,1,0x333,0);
    assert(!wine_nx_perf_read(out,sizeof(out)));
    {
        char *row=strstr(out,"kind=vk-pipeline"), *cpu;
        assert(row && strstr(row,"cpu_n=1"));
        cpu=strstr(row,"cpu_ns=");assert(cpu);
        assert(strtoull(cpu+7,NULL,10)<2000000); /* sleeping wall time is not CPU work */
    }
    t=wine_nx_perf_begin(NX_PERF_PRESENT);
    wine_nx_perf_start(1); /* A completed old window cannot contaminate a new one. */
    wine_nx_perf_end(t,NX_PERF_PRESENT,1,0x111,0);
    assert(!wine_nx_perf_read(out,sizeof(out)) && !strstr(out,"vk-present"));
    for(i=0;i<4;i++) assert(!pthread_create(&threads[i],NULL,worker,NULL));
    for(i=0;i<4;i++) pthread_join(threads[i],NULL);
    assert(!wine_nx_perf_read(out,sizeof(out)) && strstr(out,"vk-submit"));
    for(i=0;i<150;i++) {
        t=wine_nx_perf_begin(NX_PERF_SUBMIT);
        wine_nx_perf_end(t,NX_PERF_SUBMIT,3,i,0);
    }
    assert(!wine_nx_perf_read(out,sizeof(out)) && strstr(out,"buckets=96"));
    delay.tv_sec=1; delay.tv_nsec=100000000;
    nanosleep(&delay,NULL);
    assert(!wine_nx_perf_begin(NX_PERF_SUBMIT).tick);
    assert(!wine_nx_perf_read(out,sizeof(out)) && strstr(out,"active=0"));
    assert(!wine_nx_perf_read(out,2) && out[1]==0);
    assert(wine_nx_perf_read(out,1));
    assert(wine_nx_perf_control("bad",out,sizeof(out)));
    wine_nx_perf_stop();
    assert(!wine_nx_perf_begin(NX_PERF_SUBMIT).tick);
    wine_nx_perf_start(NX_PERF_CONTINUOUS);
    assert(wine_nx_perf_begin(NX_PERF_SUBMIT).tick);
    assert(!wine_nx_perf_read(out,sizeof(out)) && strstr(out,"continuous=1"));
    wine_nx_perf_stop();
    puts("PASS: timing, sampling, contention, bounds, generation and expiry");
}
