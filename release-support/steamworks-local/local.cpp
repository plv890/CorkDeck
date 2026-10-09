/* Autorun local Steamworks subset. LGPL-3.0-or-later.
 * ABI definitions are obtained from the pinned GBE reference at build time.
 * No network, overlay, client process, worker thread or cloud upload. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define STEAM_WIN32
#define STEAM_API_EXPORTS
#define STEAM_API_FUNCTIONS_IMPL
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <string>
#include <vector>
#include <algorithm>
#include "steam/steam_api.h"
#include "interface_headers.inc"

static bool ready;
static bool screenshots_hooked;
static uint32 appid;
static uint64 account;
static uintptr_t generation=1;
static char directory[MAX_PATH], config[MAX_PATH], save_root[MAX_PATH];
static char game_language[32]="english", ui_language[32]="english";
static HMODULE module;
static CRITICAL_SECTION mutex;
static constexpr uint64 quota = 64u*1024u*1024u;
static constexpr uint32 max_file = 16u*1024u*1024u;
static void *lookup(const char *);
static EUserHasLicenseForAppResult owns_license(uint64 user,uint32 game) {
    return ready && user==account && game==appid ? k_EUserHasLicenseResultHasLicense : k_EUserHasLicenseResultDoesNotHaveLicense;
}
static EUserHasLicenseForAppResult owns_license(CSteamID user,uint32 game) {return owns_license(user.ConvertToUint64(),game);}
struct Guard { Guard(){ EnterCriticalSection(&mutex); } ~Guard(){ LeaveCriticalSection(&mutex); } };

static bool ini(const char *key, char *out, unsigned size) {
    return GetPrivateProfileStringA("autorun",key,"",out,size,config)>0;
}
static bool initialize() {
    Guard guard;
    if (ready) return true;
    const char *supported="|english|russian|german|french|spanish|latam|italian|portuguese|brazilian|polish|ukrainian|czech|turkish|dutch|swedish|danish|norwegian|finnish|romanian|hungarian|greek|bulgarian|arabic|japanese|koreana|schinese|tchinese|thai|vietnamese|indonesian|";
    const char *keys[]={"language","ui_language"};char *outputs[]={game_language,ui_language};
    for(unsigned i=0;i<2;++i){
        char selected[32],token[36];strcpy(outputs[i],"english");
        if(ini(keys[i],selected,sizeof(selected))){
            snprintf(token,sizeof(token),"|%s|",selected);
            if(selected[0]&&!strchr(selected,'|')&&strstr(supported,token))strcpy(outputs[i],selected);
        }
    }
    char value[128];
    if (!ini("app_id",value,sizeof(value))) return false;
    char *end; unsigned long id=strtoul(value,&end,10);
    if (*end || !id || id>UINT32_MAX) return false;
    appid=(uint32)id;
    if (!ini("steam_id",value,sizeof(value))) return false;
    account=strtoull(value,&end,10);
    if (*end || !account) return false;
    // Both keys are generated for a verified owned installation, never tokens.
    snprintf(save_root,sizeof(save_root),"%s\\autorun-steam-local",directory);
    if (!CreateDirectoryA(save_root,nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) return false;
    snprintf(save_root,sizeof(save_root),"%s\\autorun-steam-local\\%llu",directory,(unsigned long long)account);
    if (!CreateDirectoryA(save_root,nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) return false;
    snprintf(save_root,sizeof(save_root),"%s\\autorun-steam-local\\%llu\\%u",directory,(unsigned long long)account,appid);
    if (!CreateDirectoryA(save_root,nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) return false;
    // Recover our interrupted write only; never expose temp files as saves.
    char temp[MAX_PATH];snprintf(temp,sizeof(temp),"%s\\.write-%lu.tmp",save_root,GetCurrentProcessId());
    DWORD attrs=GetFileAttributesA(temp);
    if(attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))DeleteFileA(temp);
    ready=true; ++generation;
    fprintf(stderr,"[STEAM LOCAL] appid=%u ready=1 mode=local-only workers=0\n",appid);
    return true;
}

static bool file_path(const char *name, char out[MAX_PATH]) {
    if (!ready || !name || !*name || strlen(name)>120) return false;
    // First version accepts portable leaf names only; no traversal/ADS/paths.
    if (!strcmp(name,".") || !strcmp(name,"..") || name[0]=='.') return false;
    for (const unsigned char *p=(const unsigned char*)name; *p; ++p)
        if (*p<32 || strchr("/\\:*?\"<>|",*p)) return false;
    if (name[strlen(name)-1]=='.' || name[strlen(name)-1]==' ') return false;
    int n=snprintf(out,MAX_PATH,"%s\\%s",save_root,name);
    if(n<0 || n>=MAX_PATH) return false;
    DWORD attrs=GetFileAttributesA(out);
    return attrs==INVALID_FILE_ATTRIBUTES || !(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
}
static bool user_data_folder(char *out,int size) {
    Guard guard;
    if(out && size>0)out[0]=0;
    if(!ready || !out || size<=0 || strlen(save_root)>=(size_t)size)return false;
    strcpy(out,save_root);return true;
}
static std::vector<WIN32_FIND_DATAA> files() {
    std::vector<WIN32_FIND_DATAA> result;
    if(!ready) return result;
    char path[MAX_PATH]; snprintf(path,sizeof(path),"%s\\*",save_root);
    WIN32_FIND_DATAA f; HANDLE h=FindFirstFileA(path,&f);
    if(h==INVALID_HANDLE_VALUE) return result;
    do { if(!(f.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && f.cFileName[0]!='.') result.push_back(f); } while(FindNextFileA(h,&f));
    FindClose(h);
    std::sort(result.begin(),result.end(),[](const auto&a,const auto&b){return strcmp(a.cFileName,b.cFileName)<0;});
    return result;
}
static int32 save_size(const char *name) {
    Guard guard; char path[MAX_PATH]; if(!file_path(name,path)) return 0;
    WIN32_FILE_ATTRIBUTE_DATA f;
    if(!GetFileAttributesExA(path,GetFileExInfoStandard,&f) || f.nFileSizeHigh || f.nFileSizeLow>max_file) return 0;
    return (int32)f.nFileSizeLow;
}
static bool save_exists(const char *name) { Guard guard; char p[MAX_PATH]; return file_path(name,p)&&GetFileAttributesA(p)!=INVALID_FILE_ATTRIBUTES; }
static int32 save_read(const char *name,void *data,int32 size) {
    Guard guard; char path[MAX_PATH]; if(size<0 || (size&&!data) || !file_path(name,path)) return 0;
    HANDLE f=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE) return 0;
    DWORD n=0; bool ok=ReadFile(f,data,(DWORD)size,&n,nullptr); CloseHandle(f); return ok?(int32)n:0;
}
static bool save_write(const char *name,const void *data,int32 size) {
    Guard guard; char path[MAX_PATH],temp[MAX_PATH];
    if(size<0 || (uint32)size>max_file || (size&&!data) || !file_path(name,path)) return false;
    uint64 used=0; for(const auto&f:files()) used+=((uint64)f.nFileSizeHigh<<32)|f.nFileSizeLow;
    if(used-(uint64)save_size(name)+(uint32)size>quota) return false;
    int len=snprintf(temp,sizeof(temp),"%s\\.write-%lu.tmp",save_root,GetCurrentProcessId());
    if(len<0 || len>=MAX_PATH) return false;
    HANDLE f=CreateFileA(temp,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE) return false;
    DWORD n=0; bool ok=WriteFile(f,data,(DWORD)size,&n,nullptr)&&n==(DWORD)size&&FlushFileBuffers(f);
    CloseHandle(f);
    if(ok) ok=MoveFileExA(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if(!ok) DeleteFileA(temp);
    return ok;
}
static bool save_delete(const char *name) { Guard guard; char p[MAX_PATH]; return file_path(name,p)&&DeleteFileA(p); }
static int32 save_count() { Guard guard; return (int32)files().size(); }
static const char *save_name(int index,int32 *size) {
    Guard guard; static thread_local char name[MAX_PATH]; auto f=files();
    if(size)*size=0; name[0]=0; if(index<0 || (size_t)index>=f.size())return name;
    strcpy(name,f[index].cFileName); if(size)*size=(int32)f[index].nFileSizeLow; return name;
}
static int64 save_time(const char *name) {
    Guard guard; char p[MAX_PATH]; WIN32_FILE_ATTRIBUTE_DATA f;
    if(!file_path(name,p)||!GetFileAttributesExA(p,GetFileExInfoStandard,&f))return 0;
    uint64 ticks=((uint64)f.ftLastWriteTime.dwHighDateTime<<32)|f.ftLastWriteTime.dwLowDateTime;
    return ticks>=116444736000000000ull?(int64)((ticks-116444736000000000ull)/10000000):0;
}
template<class T> static bool save_quota(T *total,T *available) {
    Guard guard; if(!total||!available)return false;
    uint64 used=0; for(const auto&f:files())used+=((uint64)f.nFileSizeHigh<<32)|f.nFileSizeLow;
    *total=(T)quota; *available=(T)(used<quota?quota-used:0); return ready;
}

struct Registration { CCallbackBase *ptr; int id; SteamAPICall_t call; };
// Windows Steam callbacks use MSVC's reversed overloaded Run slot order.
// Calling through MinGW's C++ declaration instead uses the wrong overload.
// ABI reference: pinned GBE steam_client.cpp RegisterCallback / callback_wrapper.h.
class CCallbackMgr {
public:
    static void register_callback(CCallbackBase *p,int id){p->m_iCallback=id;p->m_nCallbackFlags|=1;}
    static void unregister_callback(CCallbackBase *p){p->m_nCallbackFlags&=~1;}
};
static void callback_run(CCallbackBase *p,void *data,SteamAPICall_t call) {
    void **table=*(void***)p;
    if(call) {
        using Fn=void(__thiscall *)(CCallbackBase*,void*,bool,SteamAPICall_t);
        ((Fn)table[0])(p,data,false,call);
    } else {
        using Fn=void(__thiscall *)(CCallbackBase*,void*);
        ((Fn)table[1])(p,data);
    }
}
struct Result { SteamAPICall_t call; int id; std::vector<unsigned char> bytes; bool delivered; };
static std::vector<Registration> registrations;
static std::vector<Result> results;
static SteamAPICall_t next_call=1;
static bool result_complete(SteamAPICall_t call,bool *failed) {
    Guard guard; if(failed)*failed=false;
    for(auto&r:results)if(r.call==call)return true;
    if(failed)*failed=true; return false;
}
static ESteamAPICallFailure result_failure(SteamAPICall_t call) {
    Guard guard;
    for(const auto&r:results)if(r.call==call)return k_ESteamAPICallFailureNone;
    return k_ESteamAPICallFailureInvalidHandle;
}
static bool result_copy(SteamAPICall_t call,void *data,int size,int id,bool *failed) {
    Guard guard; if(failed)*failed=false;
    for(auto&r:results)if(r.call==call&&r.id==id&&size==(int)r.bytes.size()&&data){memcpy(data,r.bytes.data(),size);return true;}
    if(failed)*failed=true; return false;
}
static bool request_stats() {
    Guard guard; if(!ready || results.size()>=64)return false;
    UserStatsReceived_t v{}; v.m_nGameID=appid; v.m_eResult=k_EResultOK; v.m_steamIDUser=CSteamID(account);
    const auto*b=(const unsigned char*)&v;
    results.push_back({next_call++,UserStatsReceived_t::k_iCallback,{b,b+sizeof(v)},false}); return true;
}
#include "interfaces.inc"
static void *lookup(const char *version) {
    if(!ready || !version)return nullptr;
    #include "lookup.inc"
}
static void *legacy(const char *key,const char *fallback) {
    char version[128]; return lookup(ini(key,version,sizeof(version))?version:fallback);
}
extern "C" {
S_API_EXPORT steam_bool S_CALLTYPE SteamAPI_Init(){return initialize();}
S_API_EXPORT steam_bool S_CALLTYPE SteamAPI_InitSafe(){return initialize();}
S_API_EXPORT void S_CALLTYPE SteamAPI_Shutdown(){Guard g; ready=false;screenshots_hooked=false;++generation;for(auto&r:registrations)if(!r.call)CCallbackMgr::unregister_callback(r.ptr);registrations.clear();results.clear();}
S_API_EXPORT HSteamUser S_CALLTYPE SteamAPI_GetHSteamUser(){return ready?1:0;}
S_API_EXPORT HSteamPipe S_CALLTYPE SteamAPI_GetHSteamPipe(){return ready?1:0;}
S_API_EXPORT steam_bool S_CALLTYPE SteamAPI_IsSteamRunning(){return ready;}
/* True tells the game to quit because Steam is starting it again. A game the
 * Steam client started is always answered false, whatever number it passes:
 * the stock Steamworks.NET manager passes no AppID at all, and a demo or an
 * edition sold under another AppID passes the one of the base game. */
S_API_EXPORT steam_bool S_CALLTYPE SteamAPI_RestartAppIfNecessary(uint32){return false;}
S_API_EXPORT void* S_CALLTYPE SteamInternal_CreateInterface(const char*v){return lookup(v);}
S_API_EXPORT void* S_CALLTYPE SteamInternal_FindOrCreateUserInterface(HSteamUser,const char*v){return lookup(v);}
S_API_EXPORT void* S_CALLTYPE SteamInternal_FindOrCreateGameServerInterface(HSteamUser,const char*){return nullptr;}
S_API_EXPORT void* S_CALLTYPE SteamInternal_ContextInit(void *data){
    Guard g;
    struct Context { void(*initialize)(void*); uintptr_t counter; void *first; };
    auto*c=(Context*)data;
    if(!c || !c->initialize || !ready)return nullptr;
    if(c->counter!=generation){c->initialize(&c->first);c->counter=generation;}
    return &c->first;
}
S_API_EXPORT void S_CALLTYPE SteamAPI_ReleaseCurrentThreadMemory(){}
S_API_EXPORT void S_CALLTYPE SteamAPI_SetMiniDumpComment(const char*){}
S_API_EXPORT void S_CALLTYPE SteamAPI_WriteMiniDump(uint32,void*,uint32){}
S_API_EXPORT void SteamAPI_SetTryCatchCallbacks(bool){}
S_API_EXPORT ISteamUser* SteamUser(){return (ISteamUser*)legacy("user",STEAMUSER_INTERFACE_VERSION);}
S_API_EXPORT ISteamApps* SteamApps(){return (ISteamApps*)legacy("apps",STEAMAPPS_INTERFACE_VERSION);}
S_API_EXPORT ISteamUtils* SteamUtils(){return (ISteamUtils*)legacy("utils",STEAMUTILS_INTERFACE_VERSION);}
S_API_EXPORT ISteamFriends* SteamFriends(){return (ISteamFriends*)legacy("friends",STEAMFRIENDS_INTERFACE_VERSION);}
S_API_EXPORT ISteamUserStats* SteamUserStats(){return (ISteamUserStats*)legacy("userstats",STEAMUSERSTATS_INTERFACE_VERSION);}
S_API_EXPORT ISteamRemoteStorage* SteamRemoteStorage(){return (ISteamRemoteStorage*)legacy("remotestorage",STEAMREMOTESTORAGE_INTERFACE_VERSION);}
S_API_EXPORT ISteamScreenshots* SteamScreenshots(){return (ISteamScreenshots*)legacy("screenshots",STEAMSCREENSHOTS_INTERFACE_VERSION);}
S_API_EXPORT ISteamClient* SteamClient(){return (ISteamClient*)legacy("client",STEAMCLIENT_INTERFACE_VERSION);}
S_API_EXPORT ISteamMatchmaking* SteamMatchmaking(){return (ISteamMatchmaking*)legacy("matchmaking",STEAMMATCHMAKING_INTERFACE_VERSION);}
S_API_EXPORT ISteamMatchmakingServers* SteamMatchmakingServers(){return (ISteamMatchmakingServers*)legacy("matchmakingservers",STEAMMATCHMAKINGSERVERS_INTERFACE_VERSION);}
S_API_EXPORT ISteamNetworking* SteamNetworking(){return (ISteamNetworking*)legacy("networking",STEAMNETWORKING_INTERFACE_VERSION);}
S_API_EXPORT ISteamGameServer* SteamGameServer(){return (ISteamGameServer*)legacy("gameserver",STEAMGAMESERVER_INTERFACE_VERSION);}
S_API_EXPORT void SteamGameServer_Shutdown(){}
S_API_EXPORT void S_CALLTYPE SteamAPI_RegisterCallback(CCallbackBase*p,int id){Guard g; if(p&&registrations.size()<128){registrations.erase(std::remove_if(registrations.begin(),registrations.end(),[p](auto&r){return r.ptr==p&&!r.call;}),registrations.end());CCallbackMgr::register_callback(p,id);registrations.push_back({p,id,0});}}
S_API_EXPORT void S_CALLTYPE SteamAPI_UnregisterCallback(CCallbackBase*p){Guard g;if(p)CCallbackMgr::unregister_callback(p);registrations.erase(std::remove_if(registrations.begin(),registrations.end(),[p](auto&r){return r.ptr==p&&!r.call;}),registrations.end());}
S_API_EXPORT void S_CALLTYPE SteamAPI_RegisterCallResult(CCallbackBase*p,SteamAPICall_t c){Guard g;if(p&&c&&registrations.size()<128)registrations.push_back({p,p->GetICallback(),c});}
S_API_EXPORT void S_CALLTYPE SteamAPI_UnregisterCallResult(CCallbackBase*p,SteamAPICall_t c){Guard g;registrations.erase(std::remove_if(registrations.begin(),registrations.end(),[p,c](auto&r){return r.ptr==p&&r.call==c;}),registrations.end());}
S_API_EXPORT void S_CALLTYPE SteamAPI_RunCallbacks(){
    Guard g;
    // Snapshot payloads, then check live registration before every callback.
    auto pending=results;
    for(auto&r:results)r.delivered=true;
    for(auto&r:pending)if(!r.delivered){auto regs=registrations;for(auto&reg:regs){
        if(!ready) return;
        auto found=std::find_if(registrations.begin(),registrations.end(),[&](auto&live){return live.ptr==reg.ptr&&live.id==reg.id&&live.call==reg.call;});
        if(found==registrations.end()||reg.id!=r.id||(reg.call&&reg.call!=r.call)||reg.ptr->GetCallbackSizeBytes()!=(int)r.bytes.size())continue;
        callback_run(reg.ptr,r.bytes.data(),reg.call);
    }}
    results.erase(std::remove_if(results.begin(),results.end(),[](auto&r){return r.delivered;}),results.end());
}
S_API_EXPORT ESteamAPIInitResult S_CALLTYPE SteamAPI_InitFlat(SteamErrMsg*msg){if(msg)(*msg)[0]=0;return initialize()?k_ESteamAPIInitResult_OK:k_ESteamAPIInitResult_FailedGeneric;}
S_API_EXPORT ESteamAPIInitResult S_CALLTYPE SteamInternal_SteamAPI_Init(const char*,SteamErrMsg*msg){return SteamAPI_InitFlat(msg);}
}
#include "flat.inc"
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,void*) {
    if(reason==DLL_PROCESS_ATTACH){module=instance;InitializeCriticalSection(&mutex);DWORD n=GetModuleFileNameA(module,directory,sizeof(directory));if(!n||n>=sizeof(directory))return FALSE;char*p=strrchr(directory,'\\');if(!p)return FALSE;*p=0;snprintf(config,sizeof(config),"%s\\autorun-steam.ini",directory);}
    return TRUE;
}
