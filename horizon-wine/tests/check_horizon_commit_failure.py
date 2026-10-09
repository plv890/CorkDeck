"""Execute production protection helpers with a failed physical commit."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
text=(root/'dlls/ntdll/unix/virtual.c').read_text()
def function(signature):
    start=text.index(signature);end=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
fixture=r'''
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define __SWITCH__ 1
typedef unsigned char BYTE;
typedef int BOOL;
typedef unsigned int NTSTATUS;
typedef unsigned long ULONG;
typedef size_t SIZE_T;
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_ACCESS_DENIED 0xc0000022u
#define STATUS_NO_MEMORY 0xc0000017u
#define STATUS_INVALID_PAGE_PROTECTION 0xc0000045u
#define VPROT_READ 1
#define VPROT_WRITE 2
#define VPROT_EXEC 4
#define VPROT_COMMITTED 8
#define VPROT_WRITEWATCH 16
#define VPROT_WRITECOPY 32
#define SEC_IMAGE 64
struct file_view { unsigned protect; };
static int use_kernel_writewatch,enable_write_exceptions;
static const unsigned page_shift=12;
static BYTE current[1024],original[1024];
static char memory[1024*4096];
static int fail_errno, calls;
static BYTE get_page_vprot(void *p) { return current[((char*)p-memory)>>12]; }
static void set_page_vprot(void *p,size_t size,BYTE prot) {
    memset(current+(((char*)p-memory)>>12),prot,size>>12);
}
static void set_page_vprot_bits(void *p,size_t size,BYTE set,BYTE clear) {
    for(size_t i=0;i<size/4096;i++) {
        BYTE *b=current+(((char*)p-memory)>>12)+i; *b=(*b|set)&~clear;
    }
}
static BYTE get_horizon_page_vprot(BYTE p) { return p; }
static int is_vprot_exec_write(BYTE p) { return (p&6)==6; }
static int mprotect_range(void *base,size_t size,BYTE set,BYTE clear) {
    if(!calls++ && fail_errno) { errno=fail_errno;return -1; }
    if(fail_errno) assert(!memcmp(current,original,size>>12));
    errno=0;return 0;
}
static int get_vprot_flags(ULONG p,unsigned *out,int image) { *out=p;return 0; }
static int is_view_valloc(struct file_view *v) { return 1; }
'''+function('static BOOL set_vprot(')+'\n'+function('static NTSTATUS set_protection(')+r'''
int main(void) {
    struct file_view v={0};
    for(int big=0;big<2;big++) {
        size_t pages=big?700:4;
        for(size_t i=0;i<pages;i++) current[i]=original[i]=(i%2)?0:VPROT_READ|VPROT_COMMITTED;
        fail_errno=ENOMEM;calls=0;
        assert(set_protection(&v,memory,pages*4096,VPROT_READ|VPROT_WRITE)==STATUS_NO_MEMORY);
        assert(errno==ENOMEM && calls==2 && !memcmp(current,original,pages));
        fail_errno=EACCES;calls=0;
        assert(set_protection(&v,memory,pages*4096,VPROT_READ)==STATUS_ACCESS_DENIED);
        assert(errno==EACCES && !memcmp(current,original,pages));
        fail_errno=0;calls=0;
        assert(!set_protection(&v,memory,pages*4096,VPROT_READ|VPROT_WRITE));
        for(size_t i=0;i<pages;i++) assert(current[i]==(VPROT_READ|VPROT_WRITE|VPROT_COMMITTED));
    }
}
'''
with tempfile.TemporaryDirectory() as temp:
    src=Path(temp)/'test.c';out=Path(temp)/'test';src.write_text(fixture)
    subprocess.run(['gcc','-fsanitize=address,undefined','-g',str(src),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('failed commit metadata rollback, errno and retry: ASan/UBSan passed')
