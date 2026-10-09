/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#ifndef NX_REQUEST_TRACE_H
#define NX_REQUEST_TRACE_H
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#define NX_REQUEST_SLOTS 256
struct nx_request_record {
    uint64_t token, started, caller;
    int64_t timeout;
    unsigned int tid, handle, code, flags, words[5];
    char role, name[40];
};
struct nx_request_table {
    pthread_mutex_t mutex;
    uint64_t serial, dropped;
    struct nx_request_record rows[NX_REQUEST_SLOTS];
};
/* Callers never retain request pointers. Token generations reject stale ends
 * after a slot is recycled. No game lock or I/O is taken under this mutex. */
static inline uint64_t nx_request_begin(struct nx_request_table *table,
                                      const struct nx_request_record *record) {
    unsigned int i; uint64_t token = 0;
    pthread_mutex_lock(&table->mutex);
    for (i = 0; i < NX_REQUEST_SLOTS; ++i) if (!table->rows[i].token) {
        table->rows[i] = *record;
        if (!++table->serial) ++table->serial;
        token = table->rows[i].token = table->serial;
        break;
    }
    if (!token) ++table->dropped;
    pthread_mutex_unlock(&table->mutex);
    return token;
}
static inline void nx_request_end(struct nx_request_table *table, uint64_t token) {
    unsigned int i;
    if (!token) return;
    pthread_mutex_lock(&table->mutex);
    for (i = 0; i < NX_REQUEST_SLOTS; ++i) if (table->rows[i].token == token) {
        table->rows[i].token = 0; break;
    }
    pthread_mutex_unlock(&table->mutex);
}
#endif
