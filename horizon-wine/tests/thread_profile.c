/* Host test for the thread profiler's sample handling (source/thread_profile.h). */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../source/thread_profile.h"

static struct nx_prof_table table;

/* A thread stack for the frame walk, based at 0x1000. */
static uint64_t stack[16];
static uint64_t read_stack( uint64_t address )
{
    assert( address >= 0x1000 && address + 8 <= 0x1000 + sizeof(stack) && !(address & 7) );
    return stack[(address - 0x1000) / 8];
}

/* An x86 stack, based at 0x2000. */
static uint32_t stack32[16];
static uint32_t read_stack32( uint64_t address )
{
    assert( address >= 0x2000 && address + 4 <= 0x2000 + sizeof(stack32) && !(address & 3) );
    return stack32[(address - 0x2000) / 4];
}

int main(void)
{
    assert(nx_thread_policy_helper(0, "dxvk-cs"));
    assert(!nx_thread_policy_helper(1, "dxvk-cs"));
    assert(nx_thread_policy_helper(1, "dxvk-shader-l"));
    assert(nx_thread_policy_helper(2, "dxvk-shader-h"));
    assert(nx_thread_policy_helper(2, "dxvk-cache"));
    assert(!nx_thread_policy_helper(3, "dxvk-cache"));
    assert(nx_thread_policy_helper(4, "dxvk-shader-l"));
    assert(nx_thread_policy_helper(4, "dxvk-cache"));
    assert(!nx_thread_policy_helper(4, "dxvk-cs"));
    assert(!nx_thread_policy_helper(4, "RenderingThread"));
    assert(nx_thread_kernel_policy(3) && nx_thread_kernel_policy(4));
    assert(!nx_thread_kernel_policy(0) && !nx_thread_kernel_policy(2));
    assert(!nx_thread_policy_helper(1, "RenderingThread"));
    uint64_t callers[8];
    uint32_t x86[8];
    unsigned int top[8], n, i;

    /* At a gate: esp 0x2000 holds the return into the stub and its caller's return;
     * ebp 0x2010 -> 0x2020, whose saved ebp points down. */
    stack32[0] = 0x7bc53000; stack32[1] = 0x7bc21000;
    stack32[4] = 0x2020; stack32[5] = 0x10038000;
    stack32[8] = 0x1ff0; stack32[9] = 0x00563000;
    n = nx_prof_x86_callers( 0x2000, 0x2010, 0x2000, 0x2000 + sizeof(stack32), read_stack32, x86, 8 );
    assert( n == 4 && x86[0] == 0x7bc53000 && x86[1] == 0x7bc21000 && x86[2] == 0x10038000 && x86[3] == 0x00563000 );
    /* ebp below esp, misaligned or outside the stack: only the two words at esp. */
    assert( nx_prof_x86_callers( 0x2008, 0x2004, 0x2000, 0x2000 + sizeof(stack32), read_stack32, x86, 8 ) == 2 );
    assert( nx_prof_x86_callers( 0x2000, 0x2011, 0x2000, 0x2000 + sizeof(stack32), read_stack32, x86, 8 ) == 2 );
    assert( nx_prof_x86_callers( 0x2000, 0x9000, 0x2000, 0x2000 + sizeof(stack32), read_stack32, x86, 8 ) == 2 );
    /* esp at the very top of the stack has no words to read. */
    assert( nx_prof_x86_callers( 0x2040, 0x2040, 0x2000, 0x2000 + sizeof(stack32), read_stack32, x86, 8 ) == 0 );
    /* Inside translated code only the frame chain counts. */
    n = nx_prof_x86_frames( 0x2000, 0x2010, 0x2000, 0x2000 + sizeof(stack32), read_stack32, x86, 8 );
    assert( n == 2 && x86[0] == 0x10038000 && x86[1] == 0x00563000 );
    assert( nx_prof_x86_frames( 0x2020, 0x2010, 0x2000, 0x2000 + sizeof(stack32), read_stack32, x86, 8 ) == 0 );
    assert( nx_prof_x86_frames( 0x2000, 0x2012, 0x2000, 0x2000 + sizeof(stack32), read_stack32, x86, 8 ) == 0 );

    /* At the svc instruction or after it; neither is not a system call. */
    assert( nx_prof_svc_at( 0xd4000301, 0xf81f0fe0 ) == 0x18 );
    assert( nx_prof_svc_at( 0xd65f03c0, 0xd4000761 ) == 0x3b );
    assert( nx_prof_svc_at( 0xd65f03c0, 0xd503201f ) == -1 );

    /* Frame records at 0x1010 -> 0x1030 -> 0x1050, whose saved fp points down: stop there. */
    stack[2] = 0x1030; stack[3] = 0x4000a0;   /* frame at 0x1010 */
    stack[6] = 0x1050; stack[7] = 0x4000b0;   /* frame at 0x1030 */
    stack[10] = 0x1008; stack[11] = 0x4000c0; /* frame at 0x1050 */
    n = nx_prof_callers( 0x400090, 0x1010, 0x1000, 0x1000 + sizeof(stack), read_stack, callers, 8 );
    assert( n == 4 && callers[0] == 0x400090 && callers[1] == 0x4000a0 && callers[2] == 0x4000b0 &&
            callers[3] == 0x4000c0 );
    /* The link register repeated by the first record counts once; the limit holds. */
    n = nx_prof_callers( 0x4000a0, 0x1010, 0x1000, 0x1000 + sizeof(stack), read_stack, callers, 2 );
    assert( n == 2 && callers[0] == 0x4000a0 && callers[1] == 0x4000b0 );
    /* An fp outside the stack or misaligned is not followed. */
    assert( nx_prof_callers( 0x400090, 0x5000, 0x1000, 0x1000 + sizeof(stack), read_stack, callers, 8 ) == 1 );
    assert( nx_prof_callers( 0x400090, 0x1011, 0x1000, 0x1000 + sizeof(stack), read_stack, callers, 8 ) == 1 );
    assert( nx_prof_callers( 0x400090, 0x1078, 0x1000, 0x1000 + sizeof(stack), read_stack, callers, 8 ) == 1 );
    assert( nx_prof_pair_key( 0x123, NX_PROF_NO_SITE ) == 0x123ffffffffull );

    /* svc #0 and svc #0x18 (WaitSynchronization); nop and hvc are not svcs. */
    assert( nx_prof_svc_number( 0xd4000001 ) == 0 );
    assert( nx_prof_svc_number( 0xd4000301 ) == 0x18 );
    assert( nx_prof_svc_number( 0xd4000021 ) == 1 );
    assert( nx_prof_svc_number( 0xd503201f ) == -1 );
    assert( nx_prof_svc_number( 0xd4000002 ) == -1 );

    /* Translated code wins; outside the runtime is a PE module; in it, an svc or not. */
    assert( nx_prof_classify( 1, 1, 5 ) == NX_PROF_X86 );
    assert( nx_prof_classify( 0, 0, -1 ) == NX_PROF_IMAGE );
    assert( nx_prof_classify( 0, 1, 0x18 ) == NX_PROF_SVC );
    assert( nx_prof_classify( 0, 1, -1 ) == NX_PROF_NATIVE );
    assert( nx_prof_svc_key( 0x18, 0x123456 ) == ((0x18ull << 40) | 0x123456) );

    /* Counts by key, fullest first, including a key of 0. */
    for (i = 0; i < 5; i++) nx_prof_add( &table, 0x401000 );
    for (i = 0; i < 9; i++) nx_prof_add( &table, 0 );
    for (i = 0; i < 2; i++) nx_prof_add( &table, 0xfad00020 );
    n = nx_prof_top( &table, top, 8 );
    assert( n == 3 );
    assert( table.buckets[top[0]].key == 0 && table.buckets[top[0]].count == 9 );
    assert( table.buckets[top[1]].key == 0x401000 && table.buckets[top[1]].count == 5 );
    assert( table.buckets[top[2]].key == 0xfad00020 && table.buckets[top[2]].count == 2 );

    /* Asking for fewer keeps the fullest. */
    n = nx_prof_top( &table, top, 2 );
    assert( n == 2 && table.buckets[top[0]].count == 9 && table.buckets[top[1]].count == 5 );

    /* More keys than buckets: every sample is either counted or dropped. */
    memset( &table, 0, sizeof(table) );
    for (i = 0; i < 4 * NX_PROF_BUCKETS; i++) nx_prof_add( &table, (uint64_t)i << 8 );
    {
        unsigned long long counted = 0;
        for (i = 0; i < NX_PROF_BUCKETS; i++) counted += table.buckets[i].count;
        assert( counted + table.dropped == 4 * NX_PROF_BUCKETS );
        assert( counted <= NX_PROF_BUCKETS && table.dropped >= 3 * NX_PROF_BUCKETS );
    }

    /* Share of a core: 0.5 s of ticks over 1 s, and nonsense readings as 0. */
    assert( nx_prof_permille( 3000, 2000, 2000 ) == 500 );
    assert( nx_prof_permille( 192000000ull * 10 + 5, 5, 192000000ull * 10 ) == 1000 );
    assert( nx_prof_permille( 10, 20, 100 ) == 0 );
    assert( nx_prof_permille( 10, 0, 0 ) == 0 );

    /* NFSU2 in build 71: the drawing thread (880) shares core 0 with a worker (87)
     * and a light thread; the main thread (840) runs on any core. */
    {
        struct nx_balance_thread threads[] =
        {
            { 880, 0, 0, 0 }, { 840, -1, 0, 0 }, { 256, 2, 0, 0 }, { 87, 0, 0, 0 }, { 16, 1, 0, 0 }, { 5, 0, 0, 0 },
        };
        unsigned int before, after;

        after = nx_balance_assign( threads, 6, 3, &before );
        assert( before == 972 && after == 885 );
        /* The drawing thread keeps its core, the main thread gets one, the rest share the third. */
        assert( threads[0].new_core == 0 && threads[1].new_core == 1 && threads[2].new_core == 2 &&
                threads[3].new_core == 2 && threads[4].new_core == 2 && threads[5].new_core == 0 );
        /* Placed that way, the next round moves nothing. */
        for (i = 0; i < 6; i++) threads[i].core = threads[i].new_core;
        after = nx_balance_assign( threads, 6, 3, &before );
        assert( before == 885 && after == 885 );
        for (i = 0; i < 6; i++) assert( threads[i].new_core == threads[i].core );
    }
    /* A thread the program pinned stays, on a core or not; the heavy one beside it moves. */
    {
        struct nx_balance_thread threads[] = { { 500, 1, 1, 0 }, { 600, 1, 0, 0 }, { 400, -1, 1, 0 } };
        unsigned int before, after;

        after = nx_balance_assign( threads, 3, 2, &before );
        assert( before == 1100 && after == 600 );
        assert( threads[0].new_core == 1 && threads[1].new_core == 0 && threads[2].new_core == -1 );
    }

    puts( "thread profiler: svc decoding, classification, buckets, top, core shares and core balancing passed" );
    return 0;
}
