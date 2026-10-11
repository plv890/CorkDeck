/* Probe of the client flavour (steamclient-local.dll): loads our DLL only. */
#define WIN32_LEAN_AND_MEAN
#define STEAM_WIN32
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include "steam/steam_api.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char**argv){
    CHECK(argc==2);
    HMODULE mod=LoadLibraryA(argv[1]);CHECK(mod);
    auto role=(const char*(__cdecl*)())GetProcAddress(mod,"AutorunLocalClientAdapterRole");
    auto create=(void*(__cdecl*)(const char*,int*))GetProcAddress(mod,"CreateInterface");
    CHECK(role&&create&&!strcmp(role(),"autorun-local-offline-client-adapter-v2"));
    int code=0;
    CHECK(!create(STEAMCLIENT_INTERFACE_VERSION,&code)&&code==1); // No configuration: nothing is given.
    char folder[MAX_PATH];CHECK(GetCurrentDirectoryA(sizeof(folder),folder));
    FILE*f=fopen("autorun-steam.ini","wb");CHECK(f);
    fprintf(f,"[autorun]\nlocal_client_adapter=1\napp_id=480\nsteam_id=76561198000000000\ninstall_dir=%s\nlanguage=english\nui_language=english\n",folder);fclose(f);
    CHECK(!create("SteamClient999",&code)&&code==1);
    // A program of any age is answered in the version it asks for.
    for(const char*version:{"SteamClient007","SteamClient008","SteamClient009","SteamClient012","SteamClient017"}){
        code=1;CHECK(create(version,&code)&&code==0);
    }
    auto*client=(ISteamClient*)create(STEAMCLIENT_INTERFACE_VERSION,&code);CHECK(client&&code==0);
    HSteamPipe pipe=client->CreateSteamPipe();CHECK(pipe);
    HSteamUser user=client->ConnectToGlobalUser(pipe);CHECK(user);
    auto*utils=(ISteamUtils*)client->GetISteamUtils(pipe,STEAMUTILS_INTERFACE_VERSION);CHECK(utils&&utils->GetAppID()==480);
    auto*apps=(ISteamApps*)client->GetISteamApps(user,pipe,STEAMAPPS_INTERFACE_VERSION);
    CHECK(apps&&apps->BIsSubscribed()&&apps->BIsSubscribedApp(480)&&!apps->BIsSubscribedApp(481));
    char where[MAX_PATH];CHECK(apps->GetAppInstallDir(480,where,sizeof(where))==strlen(folder)+1&&!strcmp(where,folder));
    CHECK(!apps->GetAppInstallDir(481,where,sizeof(where))&&!apps->GetAppInstallDir(480,where,2));
    CHECK(client->GetISteamUser(user,pipe,STEAMUSER_INTERFACE_VERSION));
    CHECK(client->GetISteamUser(user,pipe,"SteamUser008")&&client->GetISteamApps(user,pipe,"STEAMAPPS_INTERFACE_VERSION001"));
    CHECK(client->BReleaseSteamPipe(pipe));
    // The names Valve's client library exports beside CreateInterface are there and answer.
    for(const char*name:{"Steam_CreateSteamPipe","Steam_BReleaseSteamPipe","Steam_ConnectToGlobalUser","Steam_CreateGlobalUser","Steam_CreateLocalUser",
            "Steam_ReleaseUser","Steam_BGetCallback","Steam_FreeLastCallback","Steam_GetAPICallResult","Steam_BLoggedOn","Steam_BConnected","Steam_LogOn",
            "Steam_LogOff","Steam_SetLocalIPBinding","Steam_ReleaseThreadLocalMemory","Steam_TerminateGameConnection","Steam_IsKnownInterface",
            "Steam_NotifyMissingInterface","Breakpad_SteamMiniDumpInit","Breakpad_SteamSetAppID","Breakpad_SteamSetSteamID",
            "Breakpad_SteamWriteMiniDumpSetComment","Breakpad_SteamWriteMiniDumpUsingExceptionInfoWithBuildNumber"})CHECK(GetProcAddress(mod,name));
    auto flat_pipe=(HSteamPipe(__cdecl*)())GetProcAddress(mod,"Steam_CreateSteamPipe");
    auto flat_global=(HSteamUser(__cdecl*)(HSteamPipe))GetProcAddress(mod,"Steam_ConnectToGlobalUser");
    auto flat_both=(HSteamUser(__cdecl*)(HSteamPipe*))GetProcAddress(mod,"Steam_CreateGlobalUser");
    auto flat_callback=(bool(__cdecl*)(HSteamPipe,CallbackMsg_t*))GetProcAddress(mod,"Steam_BGetCallback");
    auto flat_known=(bool(__cdecl*)(const char*))GetProcAddress(mod,"Steam_IsKnownInterface");
    auto flat_release=(bool(__cdecl*)(HSteamPipe))GetProcAddress(mod,"Steam_BReleaseSteamPipe");
    HSteamPipe second=flat_pipe();CHECK(second&&flat_global(second));
    HSteamPipe third=0;CHECK(flat_both(&third)&&third);
    CallbackMsg_t message;CHECK(!flat_callback(second,&message));
    CHECK(flat_known("SteamClient007")&&!flat_known("SteamClient999"));
    CHECK(flat_release(second));
    remove("autorun-steam.ini");FreeLibrary(mod);
    puts("PASS: client flavour answers every ISteamClient version, identity, ownership of the one game, install directory");
    return 0;
}
