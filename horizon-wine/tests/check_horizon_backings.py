#!/usr/bin/env python3
"""Run the actual backing/splitting code with a simulated kernel unmap.
Check last-view ownership, failed unmaps, zero fill and file writeback with pools,
that alias sources are tracked, and that pages still aliased never go back.
"""
from pathlib import Path
import re
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
s = (root / 'dlls/ntdll/unix/horizon.c').read_text()
def function(name):
    start = s.index(name)
    i = s.index('{', start) + 1
    depth = 1
    while depth:
        depth += (s[i] == '{') - (s[i] == '}')
        i += 1
    return s[start:i] + '\n'
fixture = r'''
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "wine/rbtree.h"
#include "horizon_pool.h"
#include "horizon_mman.h"
typedef int BOOL;
typedef void VirtmemReservation;
#define TRUE 1
#define FALSE 0
#define WARN(...) ((void)0)
#define horizon_trace(...) ((void)0)
#define memalign(a,s) aligned_alloc(a,s)
char *fake_heap_start;
static int writebacks, fail_unmap;
static void *unmapped_source;
static int check_code_memory_syscalls(void) { return 0; }
static int get_effective_horizon_prot(int p) { return p; }
static void remove_reservation(VirtmemReservation *r) { (void)r; }
static int read_fd_at(int fd, void *ptr, size_t size, off_t off)
{ return pread(fd, ptr, size, off) < 0 ? -1 : 0; }
static void write_fd_at(int fd, const void *ptr, size_t size, off_t off)
{ writebacks++; assert(pwrite(fd, ptr, size, off) == (ssize_t)size); }
static int unmap_code_memory_range(void *addr, void *source, size_t size)
{ (void)addr; (void)size; unmapped_source = source; return fail_unmap ? -1 : 0; }
'''
for name in ['horizon_backing', 'horizon_mapping']:
    fixture += re.search(r'^struct ' + name + r'\n\{.*?^\};', s, re.M | re.S).group(0) + '\n'
a = s.index('static struct horizon_backing backing_slots[')
b = s.index('void horizon_memory_pool_stats(', a)
# Defined elsewhere in horizon.c, used by the slice.
fixture += 'static pthread_mutex_t mapping_mutex = PTHREAD_MUTEX_INITIALIZER;\n'
fixture += 'static struct horizon_page_pool section_pages;\n'
fixture += s[a:b]
fixture += function('static int compare_mapping(')
fixture += re.search(r'^static struct rb_tree mappings = .*?;', s, re.M).group(0) + '\n'
for name in ['static void list_add_mapping(', 'static void list_remove_mapping(',
             'static struct horizon_mapping *find_overlap_mapping(', 'static void free_backing(',
             'static void release_backing(', 'static void destroy_backing(',
             'static struct horizon_mapping *alloc_mapping(', 'static struct horizon_backing *create_backing_locked(',
             'static int split_backing_mapping(']:
    fixture += function(name)
fixture += r'''
int main(void)
{
    const size_t page = 4096;
    void *addr = (void *)0x20000000;
    struct horizon_backing *b = create_backing_locked(3*page, PROT_READ|PROT_WRITE, -1, 0, MAP_ANON);
    struct horizon_mapping *m, *left, *right;
    unsigned char *original = b->heap_addr;
    size_t i;
    m = alloc_mapping(addr, 3*page, b, 0, NULL, PROT_READ|PROT_WRITE);
    list_add_mapping(m);
    memset(original, 0x7b, 3*page);
    fail_unmap = 1;
    assert(split_backing_mapping(m, (char *)addr+page, page) == -1);
    assert(b->refs == 1 && find_overlap_mapping(addr, page) == m);
    fail_unmap = 0;
    assert(!split_backing_mapping(m, (char *)addr+page, page));
    assert(unmapped_source == original+page && b->refs == 2);
    left = find_overlap_mapping(addr, page);
    right = find_overlap_mapping((char *)addr+2*page, page);
    assert(left && right && !find_overlap_mapping((char *)addr+page, page));
    assert(right->source_offset == 2*page);
    assert(!split_backing_mapping(left, addr, page));
    assert(b->refs == 1);
    /* A backing remains allocated until its final view disappears. */
    struct horizon_backing *other = create_backing_locked(3*page, PROT_READ|PROT_WRITE, -1, 0, MAP_ANON);
    assert(other->heap_addr != original && original[2*page] == 0x7b);
    assert(!split_backing_mapping(right, (char *)addr+2*page, page));
    assert(!mappings.root);
    b = create_backing_locked(3*page, PROT_READ|PROT_WRITE, -1, 0, MAP_ANON);
    assert(b->heap_addr == original);
    for (i=0; i<3*page; i++) assert(!original[i]);
    destroy_backing(b);
    destroy_backing(other);
    /* Shared file backings are written before being returned to the pool. */
    char path[] = "/tmp/wine-nx-pool-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && write(fd, "hello", 5) == 5);
    b = create_backing_locked(2*page, PROT_READ|PROT_WRITE, fd, 0, MAP_SHARED);
    assert(b && b->fd != fd && !memcmp(b->heap_addr, "hello", 5));
    for (i=5; i<2*page; i++) assert(!((unsigned char *)b->heap_addr)[i]);
    ((char *)b->heap_addr)[0] = 'H';
    m = alloc_mapping(addr, 2*page, b, 0, NULL, PROT_READ|PROT_WRITE);
    list_add_mapping(m);
    assert(!split_backing_mapping(m, addr, 2*page) && writebacks == 1);
    char ch;
    assert(pread(fd, &ch, 1, 0) == 1 && ch == 'H');
    close(fd); unlink(path);
    /* Oversized allocations take whole blocks of their own and release them. */
    b = create_backing_locked(3*1024*1024, PROT_READ|PROT_WRITE, -1, 0, MAP_ANON);
    assert(b && !horizon_pages_free(&backing_pages, b->heap_addr, b->size));
    assert(!((uintptr_t)b->heap_addr % HORIZON_POOL_ARENA) && backing_pages.blocks == 1);
    destroy_backing(b);
    /* No alias source shares a kernel memory block with anything else. */
    for (i=0; i<HORIZON_POOL_ARENAS; i++) if (backing_pages.arenas[i].memory)
        assert(!((uintptr_t)backing_pages.arenas[i].memory % HORIZON_POOL_ARENA));

    /* The kernel leaves the source of an alias unmapped, so a backing freed
     * while one is live keeps its pages instead of handing them on. */
    b = create_backing_locked(2*page, PROT_READ|PROT_WRITE, -1, 0, MAP_ANON);
    fake_heap_start = (char *)((uintptr_t)b->heap_addr & ~(uintptr_t)0xffffff);
    original = b->heap_addr;
    note_alias_source(original, (void *)0x30000000, 2*page, TRUE);
    assert(alias_source_range_live(original, 2*page));
    assert(!alias_source_range_live(original + 2*page, page));
    char line[256];
    strcpy(line, "[EXC]");
    alias_source_report(original + page + 8, line, sizeof(line));
    assert(!strcmp(line, "[EXC] alias=live"));
    strcpy(line, "[EXC]");
    alias_source_report(fake_heap_start - page, line, sizeof(line));
    assert(!strcmp(line, "[EXC] alias=untracked"));
    destroy_backing(b);
    struct horizon_backing *reused = create_backing_locked(2*page, PROT_READ|PROT_WRITE, -1, 0, MAP_ANON);
    assert(reused->heap_addr != original);
    destroy_backing(reused);
    /* Once the alias goes, a fault there names the range it had been. */
    note_alias_source(original, (void *)0x30000000, 2*page, FALSE);
    assert(!alias_source_range_live(original, 2*page));
    strcpy(line, "[EXC]");
    alias_source_report(original + page, line, sizeof(line));
    assert(strstr(line, " alias=unmapped ") && strstr(line, "dst=0x30000000"));
    assert(strstr(line, "size=0x2000") && strstr(line, "age=0"));
    strcpy(line, "[EXC]");
    alias_source_report(original + 8*page, line, sizeof(line));
    assert(!strcmp(line, "[EXC] alias=none"));
    assert(horizon_pages_free(&backing_pages, original, 2*page));  /* the run leaks these */

    for (i=0; i<HORIZON_POOL_ARENAS; i++) if (backing_pages.arenas[i].memory)
    {
        assert(backing_pages.arenas[i].free_pages == HORIZON_POOL_PAGES);
        free(backing_pages.arenas[i].memory);
    }
    puts("Backing integration: failed unmaps, split views, last-reference release, zero fill, "
         "writeback, own blocks and live alias sources passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'backings.c'
    c.write_text(fixture)
    exe = Path(tmp) / 'backings'
    subprocess.run(['cc', '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I'+str(root/'include'), '-I'+str(root/'dlls/ntdll/unix'), str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
