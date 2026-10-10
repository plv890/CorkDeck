#define _POSIX_C_SOURCE 200809L
#include "steam_local.h"
#include "steam_file.h"
#include "steam_settings.h"
#include "../system_language.h"
#include "../launcher_list.h"
#include <mbedtls/sha256.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const struct { const char *name,*hash,*previous_hashes[7]; size_t size; unsigned machine; } payloads[] = {
    {"steam_api.dll","916e26cb3786d2a3f74b36ae31f6e5997ae227fa6120e33b5f57c1cf3e95326c",{"9f142b8b5d915026907231635f3e9495fdbbcad139df967ac67ac0f1de1dbbb6","6c07b437415e6a36ef28a0b5a0b738469071b527a848e6fd9ae852d92daa0436","8de20d1249db83aca4236ce9b1ee685e7507b50d8f5b934125bd07e3039cd648","e5aa414897739634065da8f88595cb32543b95eafe62b508fa42c646ba0b8abe","d9aec7f1d5452d142ba1a6eb9ffa709f2e53caa93c0e8ede5ecc562d7a24f9f7","8e9087f2156aa7397e70b12cf87204123e72b4d18448e77e06dacaa3d7b49331","bc87cd5f712fc8d65ff06e69ac26ce450aec6daa435f2894934fbc5630bad155"},586752,0x14c},
    {"steam_api64.dll","667ff936d43d858d8690f0192dd8583be1a02b7fae152efa612f796e2db12ef7",{"02b2f2c7ac99b2ef2cbc6601882f43faefeb2812dd02b0004c5dc4ec5fb56197","e1c6aff3f88086fda769fe074b514f3ce8e01aa027515fda62d16739cd5a6fb7","dc061d912d5ce3933793002a2cd2e2391329768a3887eda724756f9e22a442e0","0503fc69e9ea089a59544b2279927215d5e15f0f9cdbb5988b41d76b7a323623","fa96bd051b9c74ebb753f786309dbbfb3a9e7c9236ad430bbc8869eb3f1e7010","d7bbb6abee6796742267cff9ed515b548864347e3700c60f8c337082c526c02e","a246cee2c9119ff901ed195e421470cdf3f14fc277ebc506aca8754444be8d6f"},585216,0x8664}
};
static int previous_payload(unsigned index,const char *hash) {
    for(unsigned i=0;i<7;++i)if(!strcmp(hash,payloads[index].previous_hashes[i]))return 1;
    return 0;
}
static int join(char out[1024],const char *base,const char *leaf) {
    int n=snprintf(out,1024,"%s/%s",base,leaf);return n>0&&n<1024;
}
static int regular(const char *path) {
    struct stat s;return !lstat(path,&s)&&S_ISREG(s.st_mode);
}
static int kind(const char *path) {
    struct stat s;if(lstat(path,&s))return errno==ENOENT?0:-1;
    return S_ISREG(s.st_mode)?1:-1;
}
static int directories(const char *path) {
    char copy[1024];struct stat s;
    if(!path||strlen(path)>=sizeof(copy))return 0;
    strcpy(copy,path);
    for(char *p=copy+1;*p;++p)if(*p=='/'){
        *p=0;if(lstat(copy,&s)||!S_ISDIR(s.st_mode))return 0;*p='/';
    }
    return 1;
}
static int hash_file(const char *path,char hex[65]) {
    unsigned char buffer[16384],digest[32];size_t n;int ok=0;
    mbedtls_sha256_context sha;FILE *f;
    if(!directories(path)||!regular(path)||!(f=fopen(path,"rb")))return 0;
    mbedtls_sha256_init(&sha);
    if(mbedtls_sha256_starts_ret(&sha,0))goto done;
    while((n=fread(buffer,1,sizeof(buffer),f)))if(mbedtls_sha256_update_ret(&sha,buffer,n))goto done;
    if(ferror(f)||mbedtls_sha256_finish_ret(&sha,digest))goto done;
    for(int i=0;i<32;++i)snprintf(hex+2*i,3,"%02x",digest[i]);ok=1;
done:mbedtls_sha256_free(&sha);fclose(f);return ok;
}
static int matches(const char *path,unsigned index) {
    struct stat s;char hex[65];
    return !lstat(path,&s)&&S_ISREG(s.st_mode)&&(size_t)s.st_size==payloads[index].size&&
           hash_file(path,hex)&&!strcmp(hex,payloads[index].hash);
}
static unsigned machine(const unsigned char *data,size_t size) {
    if(size<64||memcmp(data,"MZ",2))return 0;
    uint32_t pe=(uint32_t)data[60]|(uint32_t)data[61]<<8|(uint32_t)data[62]<<16|(uint32_t)data[63]<<24;
    if(pe>size-6||memcmp(data+pe,"PE\0\0",4))return 0;
    return data[pe+4]|(unsigned)data[pe+5]<<8;
}
/* The local Steam library is carried inside the application (assets/steamworks,
 * built from assets/steamworks/source) and is put on the card when the one
 * there is not this build's: nobody has to copy it beside the application.
 * Weak: the host tests are built without it. */
extern const unsigned char corkdeck_steam_api[] __attribute__((weak));
extern const size_t corkdeck_steam_api_size __attribute__((weak));
extern const unsigned char corkdeck_steam_api64[] __attribute__((weak));
extern const size_t corkdeck_steam_api64_size __attribute__((weak));
static int carried(const char *folder,const char *payload,unsigned index) {
    const unsigned char *data=index?corkdeck_steam_api64:corkdeck_steam_api;
    const size_t *size=index?&corkdeck_steam_api64_size:&corkdeck_steam_api_size;
    char temporary[1100];FILE *out;int ok;
    if(!data||!size||*size!=payloads[index].size)return 0;
    if(mkdir(folder,0777)&&errno!=EEXIST)return 0;
    snprintf(temporary,sizeof(temporary),"%s.corkdeck-part",payload);
    if(kind(temporary)<0||!(out=fopen(temporary,"wb")))return 0;
    ok=fwrite(data,1,*size,out)==*size&&!fflush(out)&&!fsync(fileno(out));
    if(fclose(out))ok=0;
    if(!ok||!steam_file_replace(temporary,payload)){unlink(temporary);return 0;}
    return matches(payload,index);
}
static int config(const char *source,const char *destination,uint32_t app,uint64_t account,unsigned index,const char *language) {
    static const struct{const char *key,*prefix;} versions[]={
        {"user","SteamUser"},{"apps","STEAMAPPS_INTERFACE_VERSION"},{"utils","SteamUtils"},
        {"friends","SteamFriends"},{"client","SteamClient"},{"userstats","STEAMUSERSTATS_INTERFACE_VERSION"},
        {"remotestorage","STEAMREMOTESTORAGE_INTERFACE_VERSION"},{"screenshots","STEAMSCREENSHOTS_INTERFACE_VERSION"},{"matchmaking","SteamMatchMaking"},
        {"matchmakingservers","SteamMatchMakingServers"},{"networking","SteamNetworking"},{"gameserver","SteamGameServer"},
        /* Families the local library only answers "no" in: the version the game was built for
         * still decides how many functions the table has and what each takes. */
        {"ugc","STEAMUGC_INTERFACE_VERSION"},{"http","STEAMHTTP_INTERFACE_VERSION"},{"controller","SteamController"},{"inventory","STEAMINVENTORY_INTERFACE_V"},{"music","STEAMMUSIC_INTERFACE_VERSION"},{"musicremote","STEAMMUSICREMOTE_INTERFACE_VERSION"},{"video","STEAMVIDEO_INTERFACE_V"},{"applist","STEAMAPPLIST_INTERFACE_VERSION"},{"htmlsurface","STEAMHTMLSURFACE_INTERFACE_VERSION_"},{"unifiedmessages","STEAMUNIFIEDMESSAGES_INTERFACE_VERSION"},{"gameserverstats","SteamGameServerStats"},{"parentalsettings","STEAMPARENTALSETTINGS_INTERFACE_VERSION"},{"remoteplay","STEAMREMOTEPLAY_INTERFACE_VERSION"}
    };
    struct stat s;unsigned char *data=NULL;FILE *f=NULL,*out=NULL;char temp[1100];int ok=0;
    if(!regular(source)||lstat(source,&s)||s.st_size<64||s.st_size>4*1024*1024)return 0;
    if(!(data=malloc((size_t)s.st_size))||!(f=fopen(source,"rb")))goto done;
    if(fread(data,1,(size_t)s.st_size,f)!=(size_t)s.st_size||machine(data,(size_t)s.st_size)!=payloads[index].machine)goto done;
    snprintf(temp,sizeof(temp),"%s.part",destination);
    if(!directories(temp)||kind(temp)<0)goto done;
    if(!(out=fopen(temp,"wb")))goto done;
    if(fprintf(out,"[autorun]\napp_id=%u\nsteam_id=%llu\n",app,(unsigned long long)account)<0)goto done;
    if(fprintf(out,"language=%s\nui_language=%s\n",language,nx_system_language().steam)<0)goto done;
    for(size_t v=0;v<sizeof(versions)/sizeof(*versions);++v){
        size_t prefix=strlen(versions[v].prefix);char chosen[128]="";
        for(size_t i=0;i+prefix+4<=(size_t)s.st_size;++i){
            if(memcmp(data+i,versions[v].prefix,prefix))continue;
            const unsigned char *p=data+i+prefix;
            if(p[0]<'0'||p[0]>'9'||p[1]<'0'||p[1]>'9'||p[2]<'0'||p[2]>'9'||p[3])continue;
            if(!chosen[0]||strcmp((const char*)data+i,chosen)>0)snprintf(chosen,sizeof(chosen),"%s",data+i);
        }
        if(chosen[0]&&fprintf(out,"%s=%s\n",versions[v].key,chosen)<0)goto done;
    }
    if(fflush(out)||fsync(fileno(out)))goto done;
    if(fclose(out)){out=NULL;goto done;}out=NULL;
    ok=steam_file_replace(temp,destination);
done:if(out)fclose(out);if(f)fclose(f);free(data);return ok;
}

int steam_local_prepare(const char *root,const char *executable,unsigned executable_machine) {
    char directory[1024],base[1024],marker[1024],line[256];uint32_t app=0;uint64_t owner=0;int registered=0;
    FILE *f;struct stat s;
    if(!root||!executable||strlen(executable)>=sizeof(directory)||!directories(executable))return STEAM_LOCAL_NONE;
    strcpy(directory,executable);char *slash=strrchr(directory,'/');if(!slash)return STEAM_LOCAL_NONE;*slash=0;
    strcpy(base,directory);
    for(unsigned depth=0;depth<8;++depth){
        if(!join(marker,base,"autorun-steam-install.ini"))return STEAM_LOCAL_CONFLICT;
        if(regular(marker))break;
        slash=strrchr(base,'/');if(!slash)return STEAM_LOCAL_NONE;*slash=0;
    }
    if(!regular(marker)||!(f=fopen(marker,"rb")))return STEAM_LOCAL_NONE;
    char language[32];snprintf(language,sizeof(language),"%s",nx_system_language().steam);
    while(fgets(line,sizeof(line),f)){
        unsigned long long id;
        if(!strcmp(line,"autorun_steam_install=1\n"))registered=1;
        else if(sscanf(line,"appid=%u",&app)==1){}
        else if(sscanf(line,"account=%llu",&id)==1)owner=(uint64_t)id;
        else if(!strncmp(line,"language=",9)){
            line[strcspn(line,"\r\n")]=0;
            if(steam_language_valid(line+9))snprintf(language,sizeof(language),"%s",line+9);
        }
    }
    fclose(f);if(!registered||!app||!owner)return STEAM_LOCAL_NONE;
    int found=0;
    for(unsigned index=0;index<2;++index){
        if(payloads[index].machine!=executable_machine)continue;
        char target[1024],backup[1100],source[1024],payload[1024],temporary[1100],settings[1024];
        if(!join(target,directory,payloads[index].name))return STEAM_LOCAL_CONFLICT;
        snprintf(backup,sizeof(backup),"%s.autorun-original",target);
        if(lstat(target,&s)){if(lstat(backup,&s))continue;if(!regular(backup)||rename(backup,target))return STEAM_LOCAL_CONFLICT;}
        if(!regular(target))return STEAM_LOCAL_CONFLICT;
        ++found;
        if(!join(source,root,"steamworks-local")||!join(payload,source,payloads[index].name)||
           (!matches(payload,index)&&!carried(source,payload,index)))return STEAM_LOCAL_PAYLOAD_MISSING;
        if(!join(settings,directory,"autorun-steam.ini"))return STEAM_LOCAL_CONFLICT;
        if(kind(settings)<0)return STEAM_LOCAL_CONFLICT;
        if(regular(backup)){
            char current[65],original[65];
            if(!hash_file(target,current)||!hash_file(backup,original))return STEAM_LOCAL_IO;
            if(!matches(target,index)&&strcmp(current,original)&&!previous_payload(index,current))return STEAM_LOCAL_CONFLICT;
        }else if(kind(backup)<0)return STEAM_LOCAL_CONFLICT;
        if(!config(regular(backup)?backup:target,settings,app,owner,index,language))return STEAM_LOCAL_IO;
        if(matches(target,index))continue;
        snprintf(temporary,sizeof(temporary),"%s.autorun-part",target);
        if(kind(temporary)<0)return STEAM_LOCAL_CONFLICT;
        FILE *in=fopen(payload,"rb"),*out=fopen(temporary,"wb");int ok=in&&out;unsigned char block[16384];size_t n;
        if(ok){while((n=fread(block,1,sizeof(block),in)))if(fwrite(block,1,n,out)!=n){ok=0;break;}if(ferror(in)||fflush(out)||fsync(fileno(out)))ok=0;}
        if(in)fclose(in);if(out&&fclose(out))ok=0;
        if(!ok||!matches(temporary,index)){unlink(temporary);return STEAM_LOCAL_IO;}
        if(!regular(backup)&&rename(target,backup)){unlink(temporary);return STEAM_LOCAL_IO;}
        if(!steam_file_replace(temporary,target)||!matches(target,index)){
            if(!regular(target))rename(backup,target);
            return STEAM_LOCAL_IO;
        }
    }
    return found?STEAM_LOCAL_READY:STEAM_LOCAL_NONE;
}
const char *steam_local_error(int result) {
    switch(result){
    case STEAM_LOCAL_PAYLOAD_MISSING:return "Verified local Steamworks DLLs are missing. Install the matching runtime component.";
    case STEAM_LOCAL_CONFLICT:return "Local Steamworks files conflict with existing files. Original DLLs were preserved.";
    default:return "Unable to prepare local Steamworks. Original DLL backups are preserved.";
    }
}

/* Separate opt-in Steam2 provider: genuine Steam.dll remains unchanged and the
 * client DLL has an explicit local-only filename. No title-name heuristics. */
static int legacy_matches(const char *path,size_t size,const char *hash) {
    struct stat s;char digest[65];
    return !lstat(path,&s)&&S_ISREG(s.st_mode)&&(size_t)s.st_size==size&&
           hash_file(path,digest)&&!strcmp(digest,hash);
}
static int legacy_number(const char *text,uint64_t limit,uint64_t *number) {
    if(!text||!*text)return 0;
    for(const char *p=text;*p;++p)if(*p<'0'||*p>'9')return 0;
    char *end;errno=0;unsigned long long value=strtoull(text,&end,10);
    if(errno||*end||!value||value>limit)return 0;
    *number=value;return 1;
}
static int legacy_word(unsigned char c) {
    return (c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||c=='-';
}
/* A Steam2-era program is recognised by what its file is, never by its title:
 * the first-generation Steam wrapper keeps its encrypted loader in a ".bind"
 * section, and an unwrapped one names Steam.dll in plain or wide text. */
int steam_legacy_program(const char *executable) {
    static const char name[]="steam.dll";
    enum { NAME=sizeof(name)-1, KEEP=2*NAME+2, BLOCK=65536 };
    unsigned char *bytes;size_t have=0,n;uint64_t total=0;int found=0;FILE *f;
    if(!regular(executable)||!(f=fopen(executable,"rb")))return 0;
    if(!(bytes=malloc(KEEP+BLOCK))){fclose(f);return 0;}
    n=fread(bytes,1,4096,f);
    if(n>=64&&!memcmp(bytes,"MZ",2)){
        uint32_t pe=(uint32_t)bytes[60]|(uint32_t)bytes[61]<<8|(uint32_t)bytes[62]<<16|(uint32_t)bytes[63]<<24;
        if(pe<n&&n-pe>=24&&!memcmp(bytes+pe,"PE\0\0",4)){
            unsigned count=bytes[pe+6]|(unsigned)bytes[pe+7]<<8,optional=bytes[pe+20]|(unsigned)bytes[pe+21]<<8;
            for(size_t at=(size_t)pe+24+optional;count--&&at+40<=n;at+=40)
                if(!memcmp(bytes+at,".bind\0\0\0",8)){found=1;break;}
        }
    }
    have=n;
    /* Bounded: a program larger than this is not searched past its start. */
    while(!found&&have){
        for(size_t i=0;i+NAME<=have&&!found;++i){
            size_t k;
            for(k=0;k<NAME&&(bytes[i+k]|0x20)==(unsigned char)name[k];++k){}
            if(k==NAME&&(!i||!legacy_word(bytes[i-1])))found=1;
            if(found||i+2*NAME>have)continue;
            for(k=0;k<NAME&&(bytes[i+2*k]|0x20)==(unsigned char)name[k]&&!bytes[i+2*k+1];++k){}
            if(k==NAME&&(i<2||bytes[i-1]||!legacy_word(bytes[i-2])))found=1;
        }
        total+=have;if(found||total>(64u<<20))break;
        size_t keep=have<KEEP?have:KEEP;
        memmove(bytes,bytes+have-keep,keep);
        n=fread(bytes+keep,1,BLOCK,f);
        if(!n)break;
        have=keep+n;total-=keep;
    }
    free(bytes);fclose(f);return found;
}
int steam_legacy_prepare(const char *root,const char *executable,unsigned arch,
                         struct steam_legacy_context *context) {
    static const char steam_hash[]=STEAM_LEGACY_DLL_SHA256;
    static const char client_hash[]="65c11224b38272d06537f2c90dcc111127335fa563c089e97133e602f42fee87";
    char directory[1024],base[1024],marker[1024],line[256],provider[1024],client[1024];
    char source[1024],target[1024],temp[1100],settings[1024],install_dos[256];
    uint64_t app=0,owner=0;int registered=0,optin=0,optout=0,invalid=0,seen_app=0,seen_owner=0;
    FILE *f;struct stat s;
    if(!context)return STEAM_LOCAL_CONFLICT;
    memset(context,0,sizeof(*context));
    if(!root||!executable||strlen(executable)>=sizeof(directory)||!directories(executable))return STEAM_LOCAL_NONE;
    strcpy(directory,executable);char *slash=strrchr(directory,'/');if(!slash)return STEAM_LOCAL_NONE;*slash=0;
    strcpy(base,directory);
    for(unsigned depth=0;depth<8;++depth){
        if(!join(marker,base,"autorun-steam-install.ini"))return STEAM_LOCAL_CONFLICT;
        if(regular(marker))break;
        slash=strrchr(base,'/');if(!slash)return STEAM_LOCAL_NONE;*slash=0;
    }
    if(!regular(marker)||lstat(marker,&s)||s.st_size>4096||!(f=fopen(marker,"rb")))return STEAM_LOCAL_NONE;
    while(fgets(line,sizeof(line),f)){
        if(!strchr(line,'\n')&&!feof(f)){invalid=1;break;}
        line[strcspn(line,"\r\n")]=0;
        if(!strcmp(line,"autorun_steam_install=1"))registered=1;
        else if(!strcmp(line,"legacy_local_client=1"))optin=1;
        else if(!strcmp(line,"legacy_local_client=0"))optout=1;
        else if(!strncmp(line,"appid=",6)){
            if(seen_app++||!legacy_number(line+6,UINT32_MAX,&app))invalid=1;
        }else if(!strncmp(line,"account=",8)){
            if(seen_owner++||!legacy_number(line+8,UINT64_MAX,&owner))invalid=1;
        }
    }
    if(ferror(f))invalid=1;fclose(f);
    if(!registered||optout)return STEAM_LOCAL_NONE;
    /* The marker may name the choice either way. Without one, the program's
     * own file decides; a Steam.dll already prepared beside it is that same
     * decision made by an earlier start and saves reading the program again. */
    if(!optin){
        if(arch!=0x14c||!join(target,directory,"Steam.dll"))return STEAM_LOCAL_NONE;
        if(!legacy_matches(target,STEAM_LEGACY_DLL_SIZE,steam_hash)&&!steam_legacy_program(executable))return STEAM_LOCAL_NONE;
    }
    if(invalid||!app||!owner||(owner>>56)!=1||((owner>>52)&15)!=1||
       ((owner>>32)&0xfffff)>4||!(uint32_t)owner||arch!=0x14c)return STEAM_LOCAL_CONFLICT;
    /* The registered marker's parent is the installation, not the DLL store or
     * the EXE's possibly nested bin directory. Both mappings must fit MAX_PATH. */
    if(!launcher_dos_path(base,install_dos,sizeof(install_dos))||
       !join(provider,root,"steamworks-local/legacy-client")||
       !join(client,provider,"steamclient-local.dll")||
       !launcher_dos_path(client,context->client_dos_path,sizeof(context->client_dos_path)))return STEAM_LOCAL_CONFLICT;
    if(!legacy_matches(client,1241600,client_hash)||!join(source,provider,"Steam.dll")||
       !legacy_matches(source,STEAM_LEGACY_DLL_SIZE,steam_hash)){
        memset(context,0,sizeof(*context));return STEAM_LOCAL_PAYLOAD_MISSING;
    }
    if(!join(target,directory,"Steam.dll"))return STEAM_LOCAL_CONFLICT;
    int target_kind=kind(target);
    if(target_kind<0||(target_kind==1&&!legacy_matches(target,STEAM_LEGACY_DLL_SIZE,steam_hash)))return STEAM_LOCAL_CONFLICT;
    if(!target_kind){
        snprintf(temp,sizeof(temp),"%s.autorun-legacy-part",target);
        if(kind(temp)!=0)return STEAM_LOCAL_CONFLICT;
        FILE *in=fopen(source,"rb"),*out=fopen(temp,"wb");int ok=in&&out;unsigned char bytes[16384];size_t n;
        if(ok){while((n=fread(bytes,1,sizeof(bytes),in)))if(fwrite(bytes,1,n,out)!=n){ok=0;break;}
            if(ferror(in)||fflush(out)||fsync(fileno(out)))ok=0;}
        if(in)fclose(in);if(out&&fclose(out))ok=0;
        if(!ok||!legacy_matches(temp,STEAM_LEGACY_DLL_SIZE,steam_hash)){unlink(temp);return STEAM_LOCAL_IO;}
        if(kind(target)!=0){unlink(temp);return STEAM_LOCAL_CONFLICT;}
        if(!steam_file_replace(temp,target)||!legacy_matches(target,STEAM_LEGACY_DLL_SIZE,steam_hash))return STEAM_LOCAL_IO;
    }
    if(!join(settings,provider,"autorun-steam.ini")||kind(settings)<0)return STEAM_LOCAL_CONFLICT;
    snprintf(temp,sizeof(temp),"%s.part",settings);
    if(kind(temp)!=0)return STEAM_LOCAL_CONFLICT;
    if(!(f=fopen(temp,"wb")))return STEAM_LOCAL_IO;
    int ok=fprintf(f,"[autorun]\nlocal_client_adapter=1\napp_id=%u\nsteam_id=%llu\ninstall_dir=%s\nlanguage=%s\nui_language=%s\n",
                   (unsigned)app,(unsigned long long)owner,install_dos,
                   nx_system_language().steam,nx_system_language().steam)>0;
    if(fflush(f)||fsync(fileno(f)))ok=0;if(fclose(f))ok=0;
    if(!ok){unlink(temp);return STEAM_LOCAL_IO;}
    if(!steam_file_replace(temp,settings))return STEAM_LOCAL_IO;
    context->app_id=(unsigned)app;return STEAM_LOCAL_READY;
}
