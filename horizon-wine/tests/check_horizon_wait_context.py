#!/usr/bin/env python3
"""Source-extracted ordinary-wait regression: no invented guest registers."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
def function(path,signature):
 text=(root/path).read_text(); start=text.index(signature); end=text.index('{',start)+1; depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]+'\n'
code=function('dlls/ntdll/unix/thread.c','void horizon_wait_context_to_server(')
code+=function('dlls/ntdll/unix/thread.c','void horizon_wait_context_from_server(')
code+=function('dlls/ntdll/unix/server.c','unsigned int server_select(')
fixture=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "wine/server_protocol.h"
/* Platform headers use the host context layout. Test pure-C control flow. */
#define __aarch64__
#define __SWITCH__
static struct { void *teb; } data={(void *)1};
static struct { USHORT Machine; } main_image_info={IMAGE_FILE_MACHINE_I386};
static USHORT native_machine=IMAGE_FILE_MACHINE_ARM64;
static CONTEXT guest;
static int wow=1,ec,captures,conversions,continuations,change_native,calls;
static void *get_thread_data(void) { return &data; }
#define get_thread_data() (&data)
static void *get_wow_context(CONTEXT *c) { (void)c;return wow?&guest:NULL; }
static int is_arm64ec(void) { return ec; }
static NTSTATUS context_to_server(struct context_data *to,USHORT m,const void *c,USHORT from) {
 const CONTEXT *v=c;
 assert(m==from || (m==IMAGE_FILE_MACHINE_AMD64 && ec));
 memset(to,0,sizeof(*to));to->machine=m;to->flags=SERVER_CTX_CONTROL;
 to->ctl.i386_regs.eip=v->Rip;conversions++;return 0;
}
static void context_from_server(void *c,const struct context_data *from,USHORT machine) {
 CONTEXT *v=c;assert(from->machine==machine || (from->machine==IMAGE_FILE_MACHINE_AMD64 && ec));
 v->Rip=from->ctl.i386_regs.eip;
}
static int horizon_capture_context(CONTEXT *c) { captures++;memset(c,0,sizeof(*c));c->Rip=0x1234;return 0; }
static NTSTATUS signal_set_full_context(CONTEXT *c) { assert(c->Rip==0x7777);continuations++;return STATUS_NOT_SUPPORTED; }
static unsigned int server_select_context(const union select_op *op,data_size_t size,UINT flags,
 timeout_t timeout,struct context_data *contexts,struct user_apc *apc) {
 (void)op;(void)size;(void)flags;(void)timeout;(void)apc;calls++;
 if (!contexts) return STATUS_TIMEOUT;
 assert(contexts[0].machine==IMAGE_FILE_MACHINE_ARM64);
 if (wow) { assert(contexts[1].machine==main_image_info.Machine); assert(contexts[1].ctl.i386_regs.eip==0x4567);contexts[1].ctl.i386_regs.eip=0x5678; }
 else assert(contexts[1].machine==(ec?IMAGE_FILE_MACHINE_AMD64:0));
 if(change_native)contexts[0].ctl.i386_regs.eip=0x7777;
 return STATUS_TIMEOUT;
}
'''
# get_thread_data() is a macro, so remove the unused function before -Werror.
fixture=fixture.replace('static void *get_thread_data(void) { return &data; }\n','')
tests=r'''
int main(void) {
 guest.Rip=0x4567;
 assert(server_select(NULL,0,0,123,NULL,NULL)==STATUS_TIMEOUT);
 assert(captures==1 && calls==1 && guest.Rip==0x5678 && !continuations);
 wow=0;
 assert(server_select(NULL,0,0,0,NULL,NULL)==STATUS_TIMEOUT);
 assert(captures==2 && conversions==3 && !continuations);
 /* Explicit suspend snapshots must not be recaptured/replaced. */
 struct context_data explicit_context[2]={{.machine=IMAGE_FILE_MACHINE_ARM64},{0}};
 assert(server_select(NULL,0,0,0,explicit_context,NULL)==STATUS_TIMEOUT && captures==2);
 data.teb=NULL;
 assert(server_select(NULL,0,0,0,NULL,NULL)==STATUS_TIMEOUT && captures==2);
 data.teb=(void *)1;change_native=1;
 assert(server_select(NULL,0,0,0,NULL,NULL)==(unsigned int)STATUS_NOT_SUPPORTED && continuations==1);
 change_native=0;ec=1;main_image_info.Machine=IMAGE_FILE_MACHINE_AMD64;
 assert(server_select(NULL,0,0,0,NULL,NULL)==STATUS_TIMEOUT);
 puts("Ordinary waits: safe-point capture, real WOW64 state, guest restore, explicit/native-only bypass and native continuation passed");
}
'''
with tempfile.TemporaryDirectory(prefix='horizon-wait-context-') as folder:
 path=Path(folder);(path/'test.c').write_text(fixture+code+tests)
 subprocess.run([os.environ.get('WINE_NX_HOST_CC','/usr/bin/clang'),'-std=gnu11','-g','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-fsanitize=address,undefined','-no-pie','-D__WINESRC__','-D_WIN64','-I'+str(root/'include'),str(path/'test.c'),'-o',str(path/'test')],check=True)
 subprocess.run([str(path/'test')],check=True)
