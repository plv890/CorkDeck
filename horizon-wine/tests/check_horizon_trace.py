#!/usr/bin/env python3
"""Quiet thread-context faults reach bounded memory diagnostics without SD opens."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'dlls/ntdll/unix/horizon.c').read_text()
start=source.index('void horizon_trace( const char *fmt, ... )')
code=source[start:source.index('/* Address arbitration',start)]
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
int wine_nx_runtime_verbose;
static unsigned lines,opens;
static char last[384];
static void wine_nx_runtime_trace(const char *message) {
    snprintf(last,sizeof(last),"%s",message);++lines;
}
static FILE *fixture_open(const char *path,const char *mode) {
    (void)path;(void)mode;assert(wine_nx_runtime_verbose);++opens;return NULL;
}
#define fopen fixture_open
'''
tests=r'''
int main(void) {
    const char *failure="[WOW64 CONTEXT] get target=%p status=%08x\n";
    for(unsigned i=0;i<200;i++)horizon_trace(failure,(void *)16,0xc0000022u);
    assert(lines==19 && !opens && strstr(last,"status=c0000022"));
    horizon_trace("[WOW64 CONTEXT] remote target=%p access=%08x",(void *)16,8u);
    horizon_trace("ordinary verbose information");
    assert(lines==19 && !opens);
    horizon_trace("[THREAD CONTEXT] native get target=%p status=%08x",(void *)16,0xc00000bbu);
    assert(lines==20 && strstr(last,"status=c00000bb"));
    horizon_trace("[WOW64 CONTEXT] denied target=%p requested=%08x granted=%08x",(void *)16,8u,2u);
    assert(lines==21 && strstr(last,"denied"));
    horizon_trace("[HMAP] mapping failed status=%08x",0xc0000017u);
    assert(lines==22 && strstr(last,"[HMAP]"));
    wine_nx_runtime_verbose=1;horizon_trace("ordinary verbose information");
    assert(opens==1 && lines==22);
    puts("Quiet context errors: retained, sampled, no SD reopen; verbose and mapping behavior preserved");
}
'''
with tempfile.TemporaryDirectory(prefix='horizon-trace-') as folder:
    path=Path(folder);file=path/'test.c';file.write_text(fixture+code+tests)
    subprocess.run([os.environ.get('WINE_NX_HOST_CC','/usr/bin/clang'),'-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-no-pie','-pthread',str(file),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
