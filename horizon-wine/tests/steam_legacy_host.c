#define _POSIX_C_SOURCE 200809L
#include "../source/steam/steam_local.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The runtime's paths are fixed strings; relative to a scratch directory they
 * are an ordinary tree, so the real path mapping is what gets tested. */
#define ROOT "sdmc:/switch/wine"
#define GAME ROOT "/drive_c/Steam/Game"
#define EXE GAME "/bin/game.exe"
#define ADJACENT GAME "/bin/Steam.dll"
#define PROVIDER ROOT "/steamworks-local/legacy-client"
#define MARKER GAME "/autorun-steam-install.ini"
#define HEAD "autorun_steam_install=1\nappid=480\naccount=76561198000000000\n"

static void copy(const char*a,const char*b){FILE*in=fopen(a,"rb"),*out=fopen(b,"wb");assert(in&&out);char bytes[16384];size_t n;while((n=fread(bytes,1,sizeof(bytes),in)))assert(fwrite(bytes,1,n,out)==n);assert(!ferror(in));fclose(in);assert(!fclose(out));}
static void write_file(const char*name,const void*bytes,size_t size){FILE*f=fopen(name,"wb");assert(f);assert(fwrite(bytes,1,size,f)==size);assert(!fclose(f));}
static void read_file(const char*name,char*out,size_t size){FILE*f=fopen(name,"rb");assert(f);size_t n=fread(out,1,size-1,f);out[n]=0;fclose(f);}
static int exists(const char*name){struct stat s;return !lstat(name,&s);}
static void marker(const char*text){write_file(MARKER,text,strlen(text));}
/* A minimal x86 PE: one section, optionally the wrapper's, and optional bytes
 * placed where the scanner has to carry them across a read boundary. */
static void program(int bind,const void*text,size_t size,size_t at){
    size_t total=at+size+4096;unsigned char*image=calloc(1,total);assert(image);
    memcpy(image,"MZ",2);image[60]=64;memcpy(image+64,"PE\0\0",4);image[68]=0x4c;image[69]=1;
    image[70]=1;image[84]=0xe0;memcpy(image+64+24+0xe0,bind?".bind":".text",5);
    if(size)memcpy(image+at,text,size);
    write_file(EXE,image,total);free(image);
}
static int prepare(unsigned arch,struct steam_legacy_context*context){return steam_legacy_prepare(ROOT,EXE,arch,context);}

int main(int argc,char**argv){
    static const char wide[]="S\0t\0e\0a\0m\0.\0d\0l\0l\0";
    char directory[]="/tmp/steam-legacy-XXXXXX",source[1024],buffer[4096];struct steam_legacy_context context;
    assert(argc==2);assert(mkdtemp(directory));assert(!chdir(directory));
    assert(!system("mkdir -p '" GAME "/bin' '" ROOT "/steamworks-local'"));
    program(1,NULL,0,4096);
    assert(prepare(0x14c,&context)==STEAM_LOCAL_NONE);                  /* not a registered installation */
    marker(HEAD);
    program(0,NULL,0,4096);
    assert(!steam_legacy_program(EXE)&&prepare(0x14c,&context)==STEAM_LOCAL_NONE);
    program(0,"gamesteam.dll",13,5000);
    assert(!steam_legacy_program(EXE));                                 /* another library's name */
    program(0,"\\STEAM.DLL",10,4096+65536-4);
    assert(steam_legacy_program(EXE));                                  /* across a read boundary */
    program(0,wide,sizeof(wide)-1,4096+2*65536-7);
    assert(steam_legacy_program(EXE));
    program(1,NULL,0,4096);
    assert(steam_legacy_program(EXE));
    assert(prepare(0x14c,&context)==STEAM_LOCAL_PAYLOAD_MISSING&&!context.app_id&&!context.client_dos_path[0]);
    assert(!exists(ADJACENT));
    assert(!mkdir(PROVIDER,0700));
    snprintf(source,sizeof(source),"%s/Steam.dll",argv[1]);copy(source,PROVIDER "/Steam.dll");
    snprintf(source,sizeof(source),"%s/steamclient-local.dll",argv[1]);copy(source,PROVIDER "/steamclient-local.dll");
    assert(prepare(0x8664,&context)==STEAM_LOCAL_NONE&&!exists(ADJACENT)); /* Steam2 is x86 only */
    assert(prepare(0x14c,&context)==STEAM_LOCAL_READY&&context.app_id==480);
    assert(!strcmp(context.client_dos_path,"Z:\\switch\\wine\\steamworks-local\\legacy-client\\steamclient-local.dll"));
    assert(exists(ADJACENT)&&!exists(ADJACENT ".autorun-legacy-part"));
    read_file(PROVIDER "/autorun-steam.ini",buffer,sizeof(buffer));
    assert(strstr(buffer,"local_client_adapter=1\n")&&strstr(buffer,"app_id=480\n")&&
           strstr(buffer,"steam_id=76561198000000000\n")&&strstr(buffer,"install_dir=C:\\Steam\\Game\n"));
    assert(prepare(0x14c,&context)==STEAM_LOCAL_READY);                 /* idempotent */
    program(0,NULL,0,4096);
    assert(prepare(0x14c,&context)==STEAM_LOCAL_READY);                 /* the earlier decision stands */
    marker(HEAD "legacy_local_client=0\n");
    assert(prepare(0x14c,&context)==STEAM_LOCAL_NONE&&!context.app_id);  /* refused by the marker */
    assert(!unlink(ADJACENT));
    marker(HEAD "legacy_local_client=1\n");
    assert(prepare(0x14c,&context)==STEAM_LOCAL_READY&&exists(ADJACENT)); /* forced by the marker */
    assert(prepare(0x8664,&context)==STEAM_LOCAL_CONFLICT);
    program(1,NULL,0,4096);
    marker("autorun_steam_install=1\nappid=480\nappid=481\naccount=76561198000000000\n");
    assert(prepare(0x14c,&context)==STEAM_LOCAL_CONFLICT);
    marker("autorun_steam_install=1\nappid=480\naccount=123\n");
    assert(prepare(0x14c,&context)==STEAM_LOCAL_CONFLICT);              /* not an individual account */
    marker("autorun_steam_install=1\nappid=0x1e0\naccount=76561198000000000\n");
    assert(prepare(0x14c,&context)==STEAM_LOCAL_CONFLICT);
    marker(HEAD);
    write_file(ADJACENT,"a user's own file",17);
    assert(prepare(0x14c,&context)==STEAM_LOCAL_CONFLICT);
    read_file(ADJACENT,buffer,sizeof(buffer));assert(!strcmp(buffer,"a user's own file"));
    assert(!unlink(ADJACENT)&&!symlink("/tmp/not-ours",ADJACENT));
    assert(prepare(0x14c,&context)==STEAM_LOCAL_CONFLICT);
    assert(!unlink(ADJACENT));
    write_file(PROVIDER "/steamclient-local.dll","changed",7);
    assert(prepare(0x14c,&context)==STEAM_LOCAL_PAYLOAD_MISSING&&!exists(ADJACENT)&&!context.client_dos_path[0]);
    puts("Legacy Steam provider: recognition from the program file, marker overrides, verified payload, "
         "install-root configuration, idempotence and preservation of unknown files passed");
    return 0;
}
