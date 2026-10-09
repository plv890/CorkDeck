/* Optional bounded SD channel for sys-botbase 2.5. One FS worker, no extra listener.
 * FS is acquired only for a command and released before returning.
 * Trusted-LAN protocol, like the original controller protocol: no authentication.
 */
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>

#define ROOT "sdmc:/switch/autorun-remote-test"
#define MAX_FILE (2 * 1024 * 1024)
#define CHUNK 2048
u32 __nx_fs_num_sessions = 1;
static unsigned char data[CHUNK];
static char response[8192];
static size_t response_used;
static int response_overflow;
static void reply_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n=vsnprintf(response+response_used, sizeof(response)-response_used, format, args);
    va_end(args);
    if (n<0 || (size_t)n>=sizeof(response)-response_used) response_overflow=1;
    else response_used+=(size_t)n;
}

#define printf reply_printf

static void error(const char *s) { printf("ERR %s\n", s); }
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c-'0';
    if (c >= 'a' && c <= 'f') return c-'a'+10;
    if (c >= 'A' && c <= 'F') return c-'A'+10;
    return -1;
}
static int digest_ok(const char *s) {
    if (strlen(s) != 64) return 0;
    for (int i=0;i<64;i++) if (!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return 0;
    return 1;
}
static int number(const char *s, unsigned long *out) {
    if (!*s) return 0;
    for (const char *p=s;*p;p++) if (*p<'0'||*p>'9') return 0;
    char *end; *out=strtoul(s,&end,10);
    return !*end && *out<=MAX_FILE;
}
static int log_name(const char *s) {
    size_t n=strlen(s);
    if (!n || n>100 || strstr(s,"..")) return 0;
    for (size_t i=0;i<n;i++) if (!isalnum((unsigned char)s[i]) && !strchr("._-",s[i])) return 0;
    return (n>=4 && !strcmp(s+n-4,".log")) || !strcmp(s,"stdout.txt") || !strcmp(s,"stderr.txt");
}
static void part_path(char *path, const char *digest) {
    snprintf(path,256,ROOT "/test-%s.part",digest);
}
static int hash_file(const char *path, char hex[65]) {
    FILE *f=fopen(path,"rb"); if (!f) return 0;
    Sha256Context ctx; unsigned char hash[32]; sha256ContextCreate(&ctx);
    size_t count,total=0;
    while ((count=fread(data,1,CHUNK,f))) {
        total+=count; if (total>MAX_FILE) { fclose(f); return 0; }
        sha256ContextUpdate(&ctx,data,count);
    }
    int ok=!ferror(f); fclose(f); if (!ok) return 0;
    sha256ContextGetHash(&ctx,hash);
    for (int i=0;i<32;i++) sprintf(hex+2*i,"%02x",hash[i]);
    return 1;
}

static void command(int argc, char **argv) {
    char path[256]; unsigned long offset;
    if (!strcmp(argv[0],"arLogs") && argc==1) {
        DIR *d=opendir("sdmc:/switch/wine/logs");
        if (!d) { error("directory"); return; }
        char names[4096]=""; size_t used=0; int count=0; struct dirent *e;
        while ((e=readdir(d))) if (log_name(e->d_name)) {
            size_t n=strlen(e->d_name);
            if (++count>32 || used+n+1>=sizeof(names)) { closedir(d); error("limit"); return; }
            if (used) names[used++]=',';
            memcpy(names+used,e->d_name,n+1); used+=n;
        }
        closedir(d); printf("OK %s\n",names); return;
    }
    if (!strcmp(argv[0],"arTail") && argc==2 && log_name(argv[1])) {
        snprintf(path,sizeof(path),"sdmc:/switch/wine/logs/%s",argv[1]);
        FILE *f=fopen(path,"rb"); if (!f) { error("open"); return; }
        if (fseek(f,0,SEEK_END)) { fclose(f); error("seek"); return; }
        long size=ftell(f); if (size<0) { fclose(f); error("size"); return; }
        long start=size>CHUNK ? size-CHUNK : 0;
        if (fseek(f,start,SEEK_SET)) { fclose(f); error("seek"); return; }
        size_t n=fread(data,1,CHUNK,f); int ok=!ferror(f); fclose(f);
        if (!ok) { error("read"); return; }
        printf("OK %ld ",start); for (size_t i=0;i<n;i++) printf("%02x",data[i]); printf("\n"); return;
    }
    if (argc<2 || !digest_ok(argv[1])) { error("arguments"); return; }
    part_path(path,argv[1]);
    if (!strcmp(argv[0],"arBegin") && argc==2) {
        if (mkdir(ROOT,0777) && access(ROOT,F_OK)) { error("directory"); return; }
        FILE *f=fopen(path,"wbx"); if (!f) { error("exists-or-open"); return; }
        if (fclose(f)) { error("close"); return; } printf("OK\n"); return;
    }
    if (!strcmp(argv[0],"arWrite") && argc==4 && number(argv[2],&offset)) {
        size_t n=strlen(argv[3]); if (!n || n%2 || n>CHUNK*2 || offset+n/2>MAX_FILE) { error("limit"); return; }
        for (size_t i=0;i<n/2;i++) {
            int a=hexval(argv[3][i*2]),b=hexval(argv[3][i*2+1]);
            if (a<0||b<0) { error("hex"); return; } data[i]=(a<<4)|b;
        }
        FILE *f=fopen(path,"r+b"); if (!f) { error("open"); return; }
        int ok=!fseek(f,0,SEEK_END) && ftell(f)==(long)offset;
        if (ok) ok=fwrite(data,1,n/2,f)==n/2;
        if (fclose(f)) ok=0;
        if (!ok) { error("offset-or-write"); return; } printf("OK %lu\n",offset+n/2); return;
    }
    if (!strcmp(argv[0],"arRead") && argc==4 && number(argv[2],&offset)) {
        unsigned long n;
        if (!number(argv[3],&n)||n>CHUNK) { error("limit"); return; }
        FILE *f=fopen(path,"rb"); if (!f) { error("open"); return; }
        if (fseek(f,offset,SEEK_SET)) { fclose(f); error("seek"); return; }
        size_t got=fread(data,1,n,f); int ok=!ferror(f); fclose(f);
        if (!ok) { error("read"); return; }
        printf("OK "); for (size_t i=0;i<got;i++) printf("%02x",data[i]); printf("\n"); return;
    }
    if (!strcmp(argv[0],"arCommit") && argc==2) {
        u64 pid=0; Result rc=pmdmntGetApplicationProcessId(&pid);
        /* Atmosphere pm ProcessNotFound = module 15, description 1. */
        if (R_VALUE(rc)!=MAKERESULT(15,1)) {
            error(R_SUCCEEDED(rc)?"application-running":"process-query"); return;
        }
        rc=pmdmntGetProcessId(&pid,0x010000000000100DUL); /* hbloader applet */
        if (R_VALUE(rc)!=MAKERESULT(15,1)) {
            error(R_SUCCEEDED(rc)?"applet-running":"process-query"); return;
        }
        char hash[65]; if (!hash_file(path,hash)||strcmp(hash,argv[1])) { error("digest"); return; }
        FILE *f=fopen(path,"rb"); unsigned char header[20];
        if (!f) { error("open"); return; }
        int valid=fread(header,1,20,f)==20 && !memcmp(header+16,"NRO0",4); fclose(f);
        if (!valid) { error("nro"); return; }
        const char *target=ROOT "/probe.nro"; char backup[256]; int saved=0;
        if (!access(target,F_OK)) {
            if (!hash_file(target,hash)) { error("previous-hash"); return; }
            snprintf(backup,sizeof(backup),ROOT "/previous-%s.nro",hash);
            if (!access(backup,F_OK)||rename(target,backup)) { error("backup"); return; } saved=1;
        }
        if (rename(path,target)) {
            if (saved && rename(backup,target)) { error("rollback"); return; }
            error("rename"); return;
        }
        printf("OK %s\n",argv[1]); return;
    }
    error("arguments");
}

static void run_files(int argc, char **argv) {
    Result rc=fsInitialize();
    if (R_FAILED(rc)) { error("fs-service"); return; }
    rc=fsdevMountSdmc();
    if (R_FAILED(rc)) { fsExit(); error("sd-mount"); return; }
    command(argc,argv);
    fsdevUnmountDevice("sdmc"); fsExit();
}
#undef printf

/* Single-flight jobs own a duplicate of the client socket: a disconnected client
 * cannot redirect a late response into another connection or global stdout.
 * No control/freeze mutex is held while FS IPC waits. Never cancel a worker while
 * it owns a service session; busy requests fail without an unbounded queue. */
static Thread files_thread;
static int files_started, job_state, job_fd, job_argc;
static char job_storage[8192];
static char *job_argv[6];

static void files_worker(void *unused) {
    (void)unused;
    for (;;) {
        if (__atomic_load_n(&job_state, __ATOMIC_ACQUIRE)==2) {
            response_used=0; response_overflow=0;
            run_files(job_argc,job_argv);
            const char *bytes=response_overflow ? "ERR response-limit\n" : response;
            size_t left=response_overflow ? strlen(bytes) : response_used;
            while (left) {
                ssize_t n=write(job_fd,bytes,left);
                if (n<0 && errno==EINTR) continue;
                if (n<=0) break;
                bytes+=n; left-=(size_t)n;
            }
            close(job_fd);
            __atomic_store_n(&job_state,0,__ATOMIC_RELEASE);
        }
        svcSleepThread(10000000);
    }
}

int remote_files(int argc, char **argv) {
    if (!strcmp(argv[0],"arVersion") && argc==1) { printf("OK 1\n"); return 0; }
    if (argc<1 || argc>6) { printf("ERR arguments\n"); return 0; }
    if (!files_started) {
        Result rc=threadCreate(&files_thread,files_worker,NULL,NULL,0x10000,0x2c,-2);
        if (R_FAILED(rc)) { printf("ERR worker-create\n"); return 0; }
        rc=threadStart(&files_thread);
        if (R_FAILED(rc)) { threadClose(&files_thread); printf("ERR worker-start\n"); return 0; }
        files_started=1;
    }
    int expected=0;
    if (!__atomic_compare_exchange_n(&job_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
        printf("ERR busy\n"); return 0;
    }
    size_t used=0;
    for (int i=0;i<argc;i++) {
        size_t len=strlen(argv[i])+1;
        if (len>sizeof(job_storage)-used) {
            __atomic_store_n(&job_state,0,__ATOMIC_RELEASE);
            printf("ERR arguments-limit\n"); return 0;
        }
        job_argv[i]=job_storage+used;
        memcpy(job_argv[i],argv[i],len); used+=len;
    }
    job_fd=dup(STDOUT_FILENO);
    if (job_fd<0) {
        __atomic_store_n(&job_state,0,__ATOMIC_RELEASE);
        printf("ERR socket-duplicate\n"); return 0;
    }
    job_argc=argc;
    __atomic_store_n(&job_state,2,__ATOMIC_RELEASE);
    return 0;
}
