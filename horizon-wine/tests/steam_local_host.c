#define _POSIX_C_SOURCE 200809L
#include "../source/steam/steam_local.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void copy(const char*a,const char*b){FILE*in=fopen(a,"rb"),*out=fopen(b,"wb");assert(in&&out);char bytes[16384];size_t n;while((n=fread(bytes,1,sizeof(bytes),in)))assert(fwrite(bytes,1,n,out)==n);assert(!ferror(in));fclose(in);assert(!fclose(out));}
static void write_file(const char*name,const void*bytes,size_t size){FILE*f=fopen(name,"wb");assert(f);assert(fwrite(bytes,1,size,f)==size);assert(!fclose(f));}
static void read_file(const char*name,char*out,size_t size){FILE*f=fopen(name,"rb");assert(f);size_t n=fread(out,1,size-1,f);out[n]=0;fclose(f);}
int main(int argc,char**argv){
    assert(argc==2||argc==3);char directory[]="/tmp/steam-local-XXXXXX";assert(mkdtemp(directory));assert(!chdir(directory));
    assert(!mkdir("game",0700)&&!mkdir("game/bin",0700)&&!mkdir("runtime",0700));
    unsigned char original[512]={0};memcpy(original,"MZ",2);original[60]=64;memcpy(original+64,"PE\0\0",4);original[68]=0x4c;original[69]=1;
    strcpy((char*)original+100,"STEAMREMOTESTORAGE_INTERFACE_VERSION006");strcpy((char*)original+160,"SteamUser008");
    strcpy((char*)original+220,"STEAMSCREENSHOTS_INTERFACE_VERSION002");
    strcpy((char*)original+300,"SteamClient007");strcpy((char*)original+340,"SteamFriends004");
    write_file("game/bin/steam_api.dll",original,sizeof(original));write_file("game/bin/game.exe","fake",4);
    char root[1024],exe[1024];snprintf(root,sizeof(root),"%s/runtime",directory);snprintf(exe,sizeof(exe),"%s/game/bin/game.exe",directory);
    assert(steam_local_prepare(root,exe,0x14c)==STEAM_LOCAL_NONE);
    const char*marker="autorun_steam_install=1\nappid=480\naccount=76561198000000000\nlanguage=russian\n";
    write_file("game/autorun-steam-install.ini",marker,strlen(marker));
    assert(steam_local_prepare(root,exe,0x14c)==STEAM_LOCAL_PAYLOAD_MISSING);
    assert(!mkdir("runtime/steamworks-local",0700));char payload[1024];snprintf(payload,sizeof(payload),"%s/steam_api.dll",argv[1]);copy(payload,"runtime/steamworks-local/steam_api.dll");
    assert(steam_local_prepare(root,exe,0x14c)==STEAM_LOCAL_READY);
    char buffer[4096];read_file("game/bin/autorun-steam.ini",buffer,sizeof(buffer));
    assert(strstr(buffer,"appid=")==NULL&&strstr(buffer,"app_id=480")&&strstr(buffer,"user=SteamUser008")&&strstr(buffer,"remotestorage=STEAMREMOTESTORAGE_INTERFACE_VERSION006"));
    assert(strstr(buffer,"client=SteamClient007")&&strstr(buffer,"friends=SteamFriends004"));
    assert(strstr(buffer,"screenshots=STEAMSCREENSHOTS_INTERFACE_VERSION002"));
    assert(strstr(buffer,"language=russian\n") && strstr(buffer,"ui_language=english\n"));
    assert(steam_local_prepare(root,exe,0x14c)==STEAM_LOCAL_READY);
    assert(!unlink("game/bin/steam_api.dll"));
    assert(steam_local_prepare(root,exe,0x14c)==STEAM_LOCAL_READY); // Interrupted replacement recovers original.
    if(argc==3){
        char previous[1024];struct stat before,after;
        snprintf(previous,sizeof(previous),"%s/previous-steam_api.dll",argv[2]);
        copy(previous,"game/bin/steam_api.dll");assert(!stat("game/bin/steam_api.dll",&before));
        assert(steam_local_prepare(root,exe,0x14c)==STEAM_LOCAL_READY);
        assert(!stat("game/bin/steam_api.dll",&after)&&after.st_size!=before.st_size);
        // Original ABI inference must survive an upgrade from our real installed DLL.
        read_file("game/bin/autorun-steam.ini",buffer,sizeof(buffer));
        assert(strstr(buffer,"user=SteamUser008")&&strstr(buffer,"client=SteamClient007"));
    }
    write_file("game/bin/steam_api.dll","user modification",17);
    assert(steam_local_prepare(root,exe,0x14c)==STEAM_LOCAL_CONFLICT);
    assert(!unlink("game/bin/steam_api.dll"));copy("game/bin/steam_api.dll.autorun-original","game/bin/steam_api.dll");
    assert(!unlink("game/bin/autorun-steam.ini"));assert(!symlink("/tmp/not-ours","game/bin/autorun-steam.ini"));
    assert(steam_local_prepare(root,exe,0x14c)==STEAM_LOCAL_CONFLICT);
    puts("Local SDK: pattern discovery, verified payload, version inference, backup/recovery, idempotence and conflict preservation passed");
}
