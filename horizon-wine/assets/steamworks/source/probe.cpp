/* Synthetic game-side ABI/storage/callback probe; executes no game code. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define STEAM_WIN32
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "steam/steam_api.h"
#include "steam/isteamremotestorage006.h"
#include "steam/isteamuserstats012.h"
#include "steam/isteamapps005.h"
#include "steam/isteamscreenshots001.h"
#include "steam/isteamscreenshots002.h"
#include "steam/isteamuser008.h"
#include "steam/isteamfriends004.h"
#include "steam/isteamclient007.h"

/* Calls a function that returns a CSteamID as a game built with Visual C++ does:
 * the address to write it to goes first, and comes back. The probe itself is
 * built with the library's compiler, whose own way of returning one is another. */
#if defined(__i386__)
#define ABI_THISCALL __attribute__((thiscall))
#else
#define ABI_THISCALL
#endif
template<class C,class... P,class... A> static CSteamID msvc_call(C *object,CSteamID (C::*method)(P...),A... arguments){
    union{CSteamID (C::*method)(P...);struct{intptr_t pointer,adjustment;}raw;}u;u.method=method;
    size_t slot=(size_t)(u.raw.pointer-1)/sizeof(void*);
    using Fn=CSteamID*(ABI_THISCALL *)(C*,CSteamID*,P...);
    CSteamID result((uint64)0x1111111111111111ull);
    volatile unsigned canary=0x5aa55aa5;
    CSteamID *returned=((Fn)(*(void***)object)[slot])(object,&result,arguments...);
    if(returned!=&result||canary!=0x5aa55aa5)return CSteamID((uint64)0xbadbadbadbadbadull);
    return result;
}
#define CHECK(x) do{if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
template<class T> T symbol(HMODULE h,const char*n){return (T)GetProcAddress(h,n);}
struct Callback final:CCallbackBase{
    void *foreign_vtable[3];
    int calls=0, long_calls=0; void(*remove)(CCallbackBase*)=nullptr;
    Callback(){
        // MSVC places overloaded Run methods in reverse declaration order.
        // A MinGW-produced callback alone cannot test that foreign ABI.
        void **original=*(void***)this;
        foreign_vtable[0]=original[1];foreign_vtable[1]=original[0];foreign_vtable[2]=original[2];
        *(void***)this=foreign_vtable;
        m_iCallback=UserStatsReceived_t::k_iCallback;
    }
    void Run(void*p)override {auto*r=(UserStatsReceived_t*)p;if(r->m_nGameID==480&&r->m_eResult==k_EResultOK)++calls;if(remove)remove(this);}
    void Run(void*p,bool,SteamAPICall_t)override{++long_calls;Run(p);}
    int GetCallbackSizeBytes()override{return sizeof(UserStatsReceived_t);}
    bool registered()const{return (m_nCallbackFlags&1)!=0;}
};
static unsigned context_calls;
static void initialize_context(void*p){++context_calls;*(void**)p=(void*)0x1234;}
int main(int argc,char**argv){
    if(argc!=2)return 2;
    HMODULE h=LoadLibraryA(argv[1]);CHECK(h);
    auto init=symbol<bool(*)()>(h,"SteamAPI_Init");
    auto shutdown=symbol<void(*)()>(h,"SteamAPI_Shutdown");
    auto lookup=symbol<void*(*)(const char*)>(h,"SteamInternal_CreateInterface");
    auto pump=symbol<void(*)()>(h,"SteamAPI_RunCallbacks");
    auto reg=symbol<void(*)(CCallbackBase*,int)>(h,"SteamAPI_RegisterCallback");
    auto unreg=symbol<void(*)(CCallbackBase*)>(h,"SteamAPI_UnregisterCallback");
    auto context=symbol<void*(*)(void*)>(h,"SteamInternal_ContextInit");
    CHECK(init&&shutdown&&lookup&&pump&&reg&&unreg&&context);
    auto restart=symbol<bool(*)(uint32)>(h,"SteamAPI_RestartAppIfNecessary");
    CHECK(restart&&!restart(0)&&!restart(480)&&!restart(22300)); // Never asks the game to quit.
    CHECK(!init()); // Missing account/AppID configuration must fail.
    FILE*f=fopen("autorun-steam.ini","wb");CHECK(f);
    fputs("[autorun]\napp_id=480\nsteam_id=76561198000000000\nlanguage=russian\nui_language=german\n",f);fclose(f);
    CHECK(init());CHECK(!lookup("UnknownInterface999"));
    auto *language_apps=(ISteamApps*)lookup(STEAMAPPS_INTERFACE_VERSION);CHECK(language_apps);
    auto *language_utils=(ISteamUtils*)lookup(STEAMUTILS_INTERFACE_VERSION);CHECK(language_utils);
    CHECK(!strcmp(language_apps->GetCurrentGameLanguage(),"russian"));
    CHECK(!strcmp(language_utils->GetSteamUILanguage(),"german"));
    auto *shot1=(ISteamScreenshots001*)lookup("STEAMSCREENSHOTS_INTERFACE_VERSION001");
    auto *shot2=(ISteamScreenshots002*)lookup("STEAMSCREENSHOTS_INTERFACE_VERSION002");
    auto *shot3=(ISteamScreenshots*)lookup(STEAMSCREENSHOTS_INTERFACE_VERSION);
    CHECK(shot1&&shot2&&shot3);
    auto *client=(ISteamClient*)lookup(STEAMCLIENT_INTERFACE_VERSION);CHECK(client);
    auto *client7=(ISteamClient007*)lookup("SteamClient007");CHECK(client7);
    CHECK(client7->CreateSteamPipe()==1 && client7->ConnectToGlobalUser(1)==1);
    CHECK(!client7->ConnectToGlobalUser(99));
    auto *user8=(ISteamUser008*)lookup("SteamUser008");CHECK(user8);
    CHECK(client7->GetISteamUser(1,1,"SteamUser008")== (ISteamUser*)user8);
    CHECK(user8->GetHSteamUser()==1 && !user8->BLoggedOn());
    CHECK(msvc_call(user8,&ISteamUser008::GetSteamID).ConvertToUint64()==76561198000000000ull);
    char auth[32];memset(auth,0x5a,sizeof(auth));
    CHECK(!user8->InitiateGameConnection(auth,sizeof(auth),CSteamID((uint64)1),CGameID((uint64)480),0,0,false,nullptr,0));
    CHECK(auth[0]==0x5a && auth[sizeof(auth)-1]==0x5a);
    auto *friends4=(ISteamFriends004*)lookup("SteamFriends004");CHECK(friends4);
    CHECK(!strcmp(friends4->GetPersonaName(),"Autorun"));
    CHECK(friends4->GetPersonaState()==k_EPersonaStateOffline && !friends4->GetFriendCount(k_EFriendFlagImmediate));
    CHECK(!friends4->GetFriendByIndex(0,k_EFriendFlagImmediate).IsValid());
    CHECK(!friends4->GetClanByIndex(0).IsValid());
    CHECK(!friends4->GetFriendFromSourceByIndex(CSteamID((uint64)1),0).IsValid());
    CHECK(language_utils->GetAPICallFailureReason(0)==k_ESteamAPICallFailureInvalidHandle);
    char folder[MAX_PATH],small[4]={'x','x','x','x'};
    auto *folder_user=(ISteamUser*)lookup(STEAMUSER_INTERFACE_VERSION);CHECK(folder_user);
    CHECK(folder_user->GetUserDataFolder(folder,sizeof(folder)));
    CHECK(strstr(folder,"autorun-steam-local\\76561198000000000\\480"));
    CHECK(GetFileAttributesA(folder)!=INVALID_FILE_ATTRIBUTES);
    CHECK(!folder_user->GetUserDataFolder(small,sizeof(small)) && !small[0] && small[3]=='x');
    CHECK(!folder_user->GetUserDataFolder(nullptr,1));
    CHECK(!folder_user->GetUserDataFolder(small,0));
    CHECK(folder_user->GetUserDataFolder(folder,(int)strlen(folder)+1));
    CHECK(client->GetISteamScreenshots(1,1,"STEAMSCREENSHOTS_INTERFACE_VERSION002")== (ISteamScreenshots*)shot2);
    CHECK(!shot3->IsScreenshotsHooked());shot1->HookScreenshots(true);CHECK(shot3->IsScreenshotsHooked());
    shot2->HookScreenshots(false);CHECK(!shot3->IsScreenshotsHooked());
    CHECK(!shot1->WriteScreenshot(nullptr,0,0,0));
    CHECK(!shot2->AddScreenshotToLibrary("missing.jpg",nullptr,1,1));
    CHECK(!shot3->SetLocation(0,"test"));shot1->TriggerScreenshot();
    shot3->HookScreenshots(true);shutdown();CHECK(init());CHECK(!shot3->IsScreenshotsHooked());
    auto*network=(ISteamNetworking*)lookup(STEAMNETWORKING_INTERFACE_VERSION);
    CHECK(network&&!network->SendP2PPacket(CSteamID((uint64)1),"x",1,k_EP2PSendReliable,0));
    auto*apps=(ISteamApps*)lookup(STEAMAPPS_INTERFACE_VERSION);CHECK(apps&&apps->BIsSubscribed());
    CHECK(apps->BIsSubscribedApp(480)&&!apps->BIsSubscribedApp(1));
    auto*user=(ISteamUser*)lookup(STEAMUSER_INTERFACE_VERSION);CHECK(user);
    CHECK(msvc_call(user,&ISteamUser::GetSteamID).ConvertToUint64()==76561198000000000ull);
    CHECK(user->UserHasLicenseForApp(msvc_call(user,&ISteamUser::GetSteamID),480)==k_EUserHasLicenseResultHasLicense);
    CHECK(user->UserHasLicenseForApp(CSteamID((uint64)1),480)==k_EUserHasLicenseResultDoesNotHaveLicense);
    CHECK(user->UserHasLicenseForApp(msvc_call(user,&ISteamUser::GetSteamID),1)==k_EUserHasLicenseResultDoesNotHaveLicense);
    {auto*owner_apps=(ISteamApps*)lookup(STEAMAPPS_INTERFACE_VERSION);CHECK(msvc_call(owner_apps,&ISteamApps::GetAppOwner).ConvertToUint64()==76561198000000000ull);}
    auto*oldapps=(ISteamApps005*)lookup("STEAMAPPS_INTERFACE_VERSION005");
    CHECK(oldapps&&oldapps->BIsSubscribedApp(480)&&!oldapps->BIsSubscribedApp(1));
    auto*old=(ISteamRemoteStorage006*)lookup("STEAMREMOTESTORAGE_INTERFACE_VERSION006");
    auto*storage=(ISteamRemoteStorage*)lookup(STEAMREMOTESTORAGE_INTERFACE_VERSION);
    CHECK(old&&storage);const char data[]={0,'A',0,'B'};char read[16]={};
    CHECK(old->FileWrite("test.sav",data,sizeof(data)));CHECK(storage->FileExists("TEST.SAV"));
    CHECK(storage->FileRead("test.sav",read,sizeof(read))==sizeof(data)&&!memcmp(read,data,sizeof(data)));
    CHECK(!old->FileWrite("../escape",data,sizeof(data)));CHECK(!old->FileWrite("bad:stream",data,sizeof(data)));
    CHECK(!old->FileWrite("test.sav",data,-1));CHECK(!old->FileWrite("test.sav",nullptr,4));
    CHECK(storage->GetFileCount()==1);int32 size=-1;
    CHECK(!strcmp(storage->GetFileNameAndSize(0,&size),"test.sav")&&size==sizeof(data));
    uint64 total=0,available=0;CHECK(storage->GetQuota(&total,&available)&&available==total-size);
    CHECK(!storage->IsCloudEnabledForApp()&&!storage->FilePersisted("test.sav"));
    auto*stats=(ISteamUserStats012*)lookup("STEAMUSERSTATS_INTERFACE_VERSION012");CHECK(stats);
    Callback cb;CHECK(!cb.registered());cb.remove=unreg;reg(&cb,UserStatsReceived_t::k_iCallback);CHECK(cb.registered());
    CHECK(stats->RequestCurrentStats());pump();pump();CHECK(cb.calls==1&&cb.long_calls==0&&!cb.registered());
    struct{void(*fn)(void*);uintptr_t counter;void*value;}ctx{initialize_context,0,nullptr};
    CHECK(context(&ctx)==&ctx.value&&ctx.value==(void*)0x1234&&context_calls==1);context(&ctx);CHECK(context_calls==1);
    shutdown();CHECK(!lookup(STEAMREMOTESTORAGE_INTERFACE_VERSION));CHECK(init());
    context(&ctx);CHECK(context_calls==2);CHECK(storage->FileRead("test.sav",read,sizeof(read))==sizeof(data));
    CHECK(storage->FileDelete("test.sav")&&!storage->FileExists("test.sav"));
    shutdown();remove("autorun-steam.ini");FreeLibrary(h);
    puts("PASS: SDK init, old/new vtables, local durable storage, callbacks, context restart; offline-only");
    return 0;
}
