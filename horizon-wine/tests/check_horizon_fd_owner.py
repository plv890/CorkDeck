#!/usr/bin/env python3
"""Descriptors sent to the in-process server reach the connection of the thread
that sent them, however the senders and takers interleave."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'dlls/ntdll/unix/horizon.c').read_text()


def block(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    if source[end] == ';':
        end += 1
    return source[start:end]


message = block('struct horizon_fd_message\n')
queue = block('struct horizon_fd_queue\n')
push = block('static void horizon_fd_queue_push( struct horizon_fd_queue *queue')
pop = block('static int horizon_fd_queue_pop( struct horizon_fd_queue *queue')
fixture = r'''
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define horizon_cond_wait_quit pthread_cond_wait
''' + message + queue + push + pop + r'''
#define CLIENTS 24
#define ROUNDS 400
static struct horizon_fd_queue queue = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, NULL, NULL };
static char owners[CLIENTS];
static pthread_barrier_t barrier;

/* A client sends two descriptors, as a starting thread does; its values name it. */
static void *client( void *arg )
{
    int id = (int)(long)arg, round;

    pthread_barrier_wait( &barrier );
    for (round = 0; round < ROUNDS; round++)
    {
        horizon_fd_queue_push( &queue, id * 100000 + round * 2, 0, &owners[id] );
        if (round % 7 == id % 7) sched_yield();
        horizon_fd_queue_push( &queue, id * 100000 + round * 2 + 1, 0, &owners[id] );
    }
    return NULL;
}

/* Its connection takes two and must get that client's, in the order sent. */
static void *server( void *arg )
{
    int id = (int)(long)arg, round;

    pthread_barrier_wait( &barrier );
    for (round = 0; round < ROUNDS; round++)
    {
        unsigned int handle;
        int first = horizon_fd_queue_pop( &queue, &handle, &owners[id] );
        int second = horizon_fd_queue_pop( &queue, &handle, &owners[id] );

        assert( first == id * 100000 + round * 2 );
        assert( second == first + 1 );
    }
    return NULL;
}

int main(void)
{
    pthread_t clients[CLIENTS], servers[CLIENTS];
    unsigned int handle = 0;
    long i;

    /* Without owners the queue is first in, first out, as it always was. */
    horizon_fd_queue_push( &queue, 11, 5, NULL );
    horizon_fd_queue_push( &queue, 12, 6, NULL );
    assert( horizon_fd_queue_pop( &queue, &handle, NULL ) == 11 && handle == 5 );
    assert( horizon_fd_queue_pop( &queue, &handle, &owners[0] ) == 12 && handle == 6 );
    /* One with an owner is skipped by another taker and found by its own. */
    horizon_fd_queue_push( &queue, 21, 0, &owners[1] );
    horizon_fd_queue_push( &queue, 22, 0, &owners[2] );
    horizon_fd_queue_push( &queue, 23, 0, NULL );
    assert( horizon_fd_queue_pop( &queue, &handle, &owners[2] ) == 22 );
    assert( horizon_fd_queue_pop( &queue, &handle, &owners[3] ) == 23 );
    assert( horizon_fd_queue_pop( &queue, &handle, &owners[1] ) == 21 );
    assert( !queue.head && !queue.tail );

    pthread_barrier_init( &barrier, NULL, 2 * CLIENTS );
    for (i = 0; i < CLIENTS; i++)
    {
        assert( !pthread_create( &servers[i], NULL, server, (void *)i ) );
        assert( !pthread_create( &clients[i], NULL, client, (void *)i ) );
    }
    for (i = 0; i < CLIENTS; i++)
    {
        assert( !pthread_join( clients[i], NULL ) );
        assert( !pthread_join( servers[i], NULL ) );
    }
    assert( !queue.head && !queue.tail );
    puts( "descriptor queue: order without owners, own descriptors under 24 concurrent senders, empty at the end" );
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'fd_owner.c'
    path.write_text(fixture)
    binary = Path(directory) / 'fd_owner'
    subprocess.run(['gcc', '-fsanitize=address,undefined', '-g', '-pthread', '-o', str(binary), str(path)], check=True)
    subprocess.run([str(binary)], check=True, timeout=120)
    # Again without instrumentation, where the threads really run at once.
    subprocess.run(['gcc', '-O2', '-pthread', '-o', str(binary), str(path)], check=True)
    for _ in range(5):
        subprocess.run([str(binary)], check=True, timeout=120, stdout=subprocess.DEVNULL)
