#!/usr/bin/env python3
"""Source-extracted read-only local primary identity, not Steam authentication."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
def function(text,signature):
 start=text.index(signature);end=text.index('{',start)+1;depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]+'\n'
code=function((root/'dlls/ntdll/unix/horizon.c').read_text(),
              'static unsigned int horizon_token_read_access(')
code+=function((root/'dlls/ntdll/unix/horizon.c').read_text(),
               'static unsigned int horizon_server_duplicate_object_handle(')
code+=(root/'dlls/ntdll/unix/horizon_security_server.h').read_text()
code+=function((root/'dlls/ntdll/unix/horizon_registry_server.h').read_text(),
               'static int horizon_server_handle_registry_user(')
fixture=r'''
#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wine/server_protocol.h"
#define HORIZON_STATUS_SUCCESS 0u
#define HORIZON_STATUS_INVALID_HANDLE 0xc0000008u
#define HORIZON_STATUS_INVALID_PARAMETER 0xc000000du
#define HORIZON_STATUS_OBJECT_TYPE_MISMATCH 0xc0000024u
#define HORIZON_STATUS_ACCESS_DENIED 0xc0000022u
#define HORIZON_STATUS_NO_MEMORY 0xc0000017u
#define HORIZON_STATUS_BUFFER_TOO_SMALL 0xc0000023u
#define HORIZON_STATUS_NOT_IMPLEMENTED 0xc0000002u
#define HORIZON_CURRENT_PROCESS_HANDLE 0xffffffffu
#define HORIZON_CURRENT_THREAD_HANDLE 0xfffffffeu
#define HORIZON_SERVER_OBJECT_PROCESS 1
#define HORIZON_SERVER_OBJECT_THREAD 2
#define HORIZON_SERVER_OBJECT_TOKEN 3
#define HORIZON_THREAD_ALL_ACCESS 0x1fffffu
#define HORIZON_PROCESS_ALL_ACCESS 0x1fffffu
struct horizon_server_request_header { int req; unsigned request_size,reply_size; };
struct horizon_server_reply_header { unsigned error,reply_size; };
struct horizon_server_object { int type; unsigned refs; };
struct horizon_server_connection { int reply_fd;struct horizon_server_object *thread; };
struct horizon_server_handle_entry { unsigned handle,access;struct horizon_server_object *object; };
static pthread_mutex_t horizon_server_objects_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct horizon_server_handle_entry *entries[64];
static unsigned next_handle,live_objects;
static int fail_alloc;
static struct horizon_server_object *horizon_primary_token;
static struct horizon_server_connection *horizon_server_current;
static unsigned char reply_buffer[64],payload[64];
static unsigned payload_size;
static struct horizon_server_handle_entry *horizon_server_find_handle_locked(unsigned handle) {
 return handle && handle<=next_handle?entries[handle-1]:NULL;
}
static unsigned horizon_server_alloc_handle(void) { assert(next_handle<64);return ++next_handle; }
static void horizon_server_link_handle_locked(struct horizon_server_handle_entry *entry) { entries[entry->handle-1]=entry; }
static unsigned horizon_thread_map_access(unsigned access) { return access; }
static unsigned horizon_process_map_access(unsigned access) { return access; }
static struct horizon_server_handle_entry *horizon_server_create_handle_for_object_locked(struct horizon_server_object *object) {
 if(fail_alloc)return NULL;
 struct horizon_server_handle_entry *entry=calloc(1,sizeof(*entry));assert(entry);
 entry->handle=horizon_server_alloc_handle();entry->object=object;object->refs++;
 horizon_server_link_handle_locked(entry);return entry;
}
static struct horizon_server_handle_entry *horizon_server_create_handle_locked(int type) {
 if(fail_alloc)return NULL;
 struct horizon_server_object *object=calloc(1,sizeof(*object));assert(object);
 object->type=type;live_objects++;
 return horizon_server_create_handle_for_object_locked(object);
}
static void close_handle(unsigned handle) {
 struct horizon_server_handle_entry *entry=horizon_server_find_handle_locked(handle);assert(entry);
 struct horizon_server_object *object=entry->object;entries[handle-1]=NULL;free(entry);
 if(!(--object->refs)) { if(object==horizon_primary_token)horizon_primary_token=NULL;free(object);live_objects--; }
}
static int horizon_server_write_reply(int fd,const void *reply,size_t size,const void *data,size_t length) {
 (void)fd;assert(size<=sizeof(reply_buffer)&&length<=sizeof(payload));
 memset(reply_buffer,0,sizeof(reply_buffer));memcpy(reply_buffer,reply,size);
 payload_size=length;if(length)memcpy(payload,data,length);return 0;
}
static unsigned status(void) { return ((struct horizon_server_reply_header *)reply_buffer)->error; }
static unsigned opened(void) { return ((struct open_token_reply *)reply_buffer)->token; }
_Static_assert(REQ_open_token==221 && REQ_get_token_sid==229,"Wire opcodes changed");
_Static_assert(offsetof(struct open_token_request,flags)==24,"Token request layout");
_Static_assert(sizeof(struct open_token_reply)==16,"Token reply layout");
'''
tests=r'''
int main(void) {
 struct horizon_server_connection connection={0};
 struct open_token_request request={.handle=HORIZON_CURRENT_PROCESS_HANDLE,.access=8};
 struct get_token_sid_request query={.which_sid=1};
 horizon_server_handle_open_token(&connection,(void *)&request);
 assert(!status());unsigned first=opened();assert(first && live_objects==1);
 request.access=0x80000000u;
 horizon_server_handle_open_token(&connection,(void *)&request);
 assert(!status());unsigned second=opened();assert(second!=first);
 assert(entries[first-1]->object==entries[second-1]->object && horizon_primary_token->refs==2);
 assert(entries[second-1]->access==0x20008);
 unsigned duplicate=0;
 assert(!horizon_server_duplicate_object_handle(first,0,2,&duplicate) && duplicate!=first);
 assert(entries[duplicate-1]->object==horizon_primary_token && entries[duplicate-1]->access==8);
 close_handle(duplicate);
 assert(horizon_server_duplicate_object_handle(first,0x20,0,&duplicate)==HORIZON_STATUS_ACCESS_DENIED && !duplicate);
 assert(horizon_primary_token->refs==2);
 fail_alloc=1;horizon_server_handle_open_token(&connection,(void *)&request);
 assert(status()==HORIZON_STATUS_NO_MEMORY&&!opened()&&horizon_primary_token->refs==2);
 fail_alloc=0;
 query.handle=first;query.__header.reply_size=0;
 horizon_server_handle_registry_user(&connection,(void *)&query);
 assert(status()==HORIZON_STATUS_BUFFER_TOO_SMALL);
 assert(((struct get_token_sid_reply *)reply_buffer)->sid_len==28 && !payload_size);
 query.__header.reply_size=28;
 horizon_server_handle_registry_user(&connection,(void *)&query);
 assert(!status()&&payload_size==28&&payload[0]==1&&payload[1]==5);
 unsigned expected[5]={21,0,0,0,1000};assert(!memcmp(payload+8,expected,sizeof(expected)));
 query.which_sid=4;horizon_server_handle_registry_user(&connection,(void *)&query);
 assert(status()==HORIZON_STATUS_NOT_IMPLEMENTED&&!payload_size);query.which_sid=1;
 request.access=0;request.attributes=0;
 horizon_server_handle_open_token(&connection,(void *)&request);unsigned no_query=opened();assert(!status());
 query.handle=no_query;horizon_server_handle_registry_user(&connection,(void *)&query);
 assert(status()==HORIZON_STATUS_ACCESS_DENIED);close_handle(no_query);
 request.access=0x20;horizon_server_handle_open_token(&connection,(void *)&request);
 assert(status()==HORIZON_STATUS_ACCESS_DENIED&&!opened());
 request.access=8;request.attributes=1;horizon_server_handle_open_token(&connection,(void *)&request);
 assert(status()==HORIZON_STATUS_INVALID_PARAMETER);request.attributes=0;
 request.handle=99;horizon_server_handle_open_token(&connection,(void *)&request);
 assert(status()==HORIZON_STATUS_INVALID_HANDLE);
 struct horizon_server_handle_entry *process=horizon_server_create_handle_locked(HORIZON_SERVER_OBJECT_PROCESS);
 request.handle=process->handle;horizon_server_handle_open_token(&connection,(void *)&request);
 assert(status()==HORIZON_STATUS_ACCESS_DENIED);process->access=0x1000;
 horizon_server_handle_open_token(&connection,(void *)&request);assert(!status());close_handle(opened());
 query.handle=process->handle;horizon_server_handle_registry_user(&connection,(void *)&query);
 assert(status()==HORIZON_STATUS_OBJECT_TYPE_MISMATCH);
 request.flags=1;request.handle=HORIZON_CURRENT_THREAD_HANDLE;
 horizon_server_handle_open_token(&connection,(void *)&request);assert(status()==0xc000007cu&&!opened());
 request.flags=4;horizon_server_handle_open_token(&connection,(void *)&request);
 assert(status()==HORIZON_STATUS_INVALID_PARAMETER);
 close_handle(first);assert(horizon_primary_token&&horizon_primary_token->refs==1);
 query.handle=first;horizon_server_handle_registry_user(&connection,(void *)&query);
 assert(status()==HORIZON_STATUS_INVALID_HANDLE);
 close_handle(second);close_handle(process->handle);assert(!horizon_primary_token&&!live_objects);
 request.flags=0;request.handle=HORIZON_CURRENT_PROCESS_HANDLE;fail_alloc=1;
 horizon_server_handle_open_token(&connection,(void *)&request);
 assert(status()==HORIZON_STATUS_NO_MEMORY&&!opened()&&!horizon_primary_token);
 fail_alloc=0;request.access=0x02000000u;
 horizon_server_handle_open_token(&connection,(void *)&request);assert(!status());
 unsigned last=opened();assert(entries[last-1]->access==0x20008);close_handle(last);
 assert(!live_objects);
 puts("Local primary identity: ordinary handles, query rights, SID size negotiation, shared object lifetime, invalid targets, no impersonation/mutation and allocation failure passed");
}
'''
with tempfile.TemporaryDirectory(prefix='horizon-security-') as folder:
 path=Path(folder);(path/'test.c').write_text(fixture+code+tests)
 subprocess.run([os.environ.get('WINE_NX_HOST_CC','/usr/bin/clang'),'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-fsanitize=address,undefined','-no-pie','-D__WINESRC__','-D_WIN64','-I'+str(root/'include'),str(path/'test.c'),'-pthread','-o',str(path/'test')],check=True)
 subprocess.run([str(path/'test')],check=True)
