#include <assert.h>
#include "../source/request_trace.h"
static struct nx_request_table table = { .mutex = PTHREAD_MUTEX_INITIALIZER };
static void *worker(void *arg) {
    struct nx_request_record r = { .tid = (unsigned int)(uintptr_t)arg };
    for (int i=0;i<10000;i++) {
        uint64_t token = nx_request_begin(&table,&r);
        assert(token); nx_request_end(&table,token);
    }
    return NULL;
}
int main(void) {
    struct nx_request_record r = { .tid=4, .timeout=INT64_MAX };
    uint64_t tokens[NX_REQUEST_SLOTS], stale;
    pthread_t threads[4];
    for(int i=0;i<NX_REQUEST_SLOTS;i++) assert((tokens[i]=nx_request_begin(&table,&r)));
    assert(!nx_request_begin(&table,&r) && table.dropped==1);
    stale=tokens[0]; nx_request_end(&table,stale);
    tokens[0]=nx_request_begin(&table,&r); assert(tokens[0]!=stale);
    nx_request_end(&table,stale); assert(table.rows[0].token==tokens[0]);
    for(int i=0;i<NX_REQUEST_SLOTS;i++) nx_request_end(&table,tokens[i]);
    for(int i=0;i<4;i++) assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)(i+1)));
    for(int i=0;i<4;i++) pthread_join(threads[i],NULL);
    for(int i=0;i<NX_REQUEST_SLOTS;i++) assert(!table.rows[i].token);
    return 0;
}
