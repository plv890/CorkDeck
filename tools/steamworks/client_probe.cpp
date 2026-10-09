/* Own offline adapter ABI/lifecycle probe. No game or ownership calls;
 * synthetic local auth verifies failure without issuing a ticket. */
#define WIN32_LEAN_AND_MEAN
#define STEAM_WIN32
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "steam/steam_api.h"
#include "steam/isteamutils010.h"
#define CHECK(x) do{if(!(x)){printf("FAIL line=%d expr=%s\n",__LINE__,#x);return 1;}}while(0)
typedef void *(__cdecl *Factory)(const char*,int*);
int main(int argc,char **argv){
 CHECK(argc==2);
 setvbuf(stdout,nullptr,_IONBF,0);
 char current[MAX_PATH],actual_install[MAX_PATH];CHECK(GetCurrentDirectoryA(sizeof(current),current));
 CHECK(snprintf(actual_install,sizeof(actual_install),"%s\\owned-install",current)<MAX_PATH);
 CHECK(CreateDirectoryA(actual_install,nullptr)||GetLastError()==ERROR_ALREADY_EXISTS);
 HMODULE mod=LoadLibraryA(argv[1]);CHECK(mod);
 Factory factory=(Factory)GetProcAddress(mod,"CreateInterface");CHECK(factory);
 auto role=(const char*(*)())GetProcAddress(mod,"AutorunLocalClientAdapterRole");CHECK(role&&!strcmp(role(),"autorun-local-offline-client-adapter-v1"));
 auto init=(bool(*)())GetProcAddress(mod,"SteamAPI_Init");CHECK(init);
 auto shutdown=(void(*)())GetProcAddress(mod,"SteamAPI_Shutdown");CHECK(shutdown);
 int code=-1;CHECK(!factory("SteamClient023",&code)&&code==1);
 CHECK(!init()); // all initialization exports require explicit opt-in
 FILE *bad=fopen("autorun-steam.ini","wb");CHECK(bad);fputs("[autorun]\napp_id=480\nsteam_id=1\nlocal_client_adapter=1\n",bad);fprintf(bad,"install_dir=%s\n",actual_install);fclose(bad);
 CHECK(!factory("SteamClient023",&code)&&code==1); // invalid SteamID
 CHECK(!init());
 bad=fopen("autorun-steam.ini","wb");CHECK(bad);fputs("[autorun]\napp_id=-1\nsteam_id=76561198000000000\nlocal_client_adapter=1\n",bad);fprintf(bad,"install_dir=%s\n",actual_install);fclose(bad);
 CHECK(!factory("SteamClient023",&code)&&code==1); // signed AppID
 CHECK(!init());
 bad=fopen("autorun-steam.ini","wb");CHECK(bad);fputs("[autorun]\napp_id=4294967296\nsteam_id=76561198000000000\nlocal_client_adapter=1\n",bad);fprintf(bad,"install_dir=%s\n",actual_install);fclose(bad);
 CHECK(!factory("SteamClient023",&code)&&code==1&&!init());
 bad=fopen("autorun-steam.ini","wb");CHECK(bad);fputs("[autorun]\napp_id=480\nsteam_id=76561198000000000\nlocal_client_adapter=1\ninstall_dir=relative-dir\n",bad);fclose(bad);
 CHECK(!factory("SteamClient023",&code)&&code==1&&!init());
 FILE *f=fopen("autorun-steam.ini","wb");CHECK(f);fputs("[autorun]\napp_id=480\nsteam_id=76561198000000000\n",f);fclose(f);
 CHECK(!factory("SteamClient023",&code)&&code==1); // mandatory opt-in
 CHECK(!init());
 f=fopen("autorun-steam.ini","ab");CHECK(f);fputs("local_client_adapter=1\n",f);fprintf(f,"install_dir=%s\n",actual_install);fclose(f);
 CHECK(!factory("SteamClient999",&code)&&code==1);
 auto*c=(ISteamClient*)factory("SteamClient023",&code);CHECK(c&&code==0);
 auto running=(bool(*)())GetProcAddress(mod,"SteamAPI_IsSteamRunning");CHECK(running&&!running());
 auto p=c->CreateSteamPipe();CHECK(p&& !c->CreateSteamPipe());
 CHECK(!c->ConnectToGlobalUser(p+1));auto u=c->ConnectToGlobalUser(p);CHECK(u);
 CHECK(!c->GetISteamUser(0,p,"SteamUser023"));
 CHECK(!c->GetISteamUser(u,p+1,"SteamUser023"));
 CHECK(!c->GetISteamUser(u,p,"SteamUtils010"));
 auto *user=c->GetISteamUser(u,p,"SteamUser023");CHECK(user&&!user->BLoggedOn()&&user->GetHSteamUser()==u);
 char ticket[16];memset(ticket,0x5a,sizeof(ticket));uint32 ticket_size=99;
 CHECK(user->GetAuthSessionTicket(ticket,sizeof(ticket),&ticket_size,nullptr)==k_HAuthTicketInvalid&&ticket_size==0);
 CHECK(ticket[0]==0x5a&&ticket[sizeof(ticket)-1]==0x5a);
 CHECK(user->BeginAuthSession(nullptr,0,CSteamID((uint64)1))==k_EBeginAuthSessionResultInvalidTicket);
 auto *utils=(ISteamUtils010*)c->GetISteamUtils(p,"SteamUtils010");CHECK(utils&&utils->GetAppID()==480);
 auto *apps=c->GetISteamApps(u,p,"STEAMAPPS_INTERFACE_VERSION009");CHECK(apps);
 char dir[MAX_PATH],tiny[2]={'x','x'};CHECK(apps->GetAppInstallDir(480,dir,sizeof(dir))>0);
 CHECK(!strcmp(dir,actual_install)); // actual install, not adapter component folder
 CHECK(GetFileAttributesA(dir)!=INVALID_FILE_ATTRIBUTES);
 CHECK(!apps->GetAppInstallDir(481,tiny,sizeof(tiny))&&!tiny[0]&&tiny[1]=='x');
 CHECK(!apps->GetAppInstallDir(480,tiny,sizeof(tiny))&&!tiny[0]&&tiny[1]=='x');
 CHECK(!apps->GetAppInstallDir(480,nullptr,1));
 c->ReleaseUser(p,u);CHECK(!c->GetISteamUser(u,p,"SteamUser023"));
 auto u2=c->ConnectToGlobalUser(p);CHECK(u2&&u2!=u);
 CHECK(!c->GetISteamUser(u,p,"SteamUser023"));
 CHECK(c->BReleaseSteamPipe(p)&&!c->BReleaseSteamPipe(p));
 auto p2=c->CreateSteamPipe();CHECK(p2&&p2!=p&&!c->ConnectToGlobalUser(p));
 CHECK(!c->GetISteamUtils(p,"SteamUtils010"));CHECK(c->BReleaseSteamPipe(p2));
 shutdown();CHECK(!running());
 c=(ISteamClient*)factory("SteamClient023",&code);CHECK(c&&code==0);
 auto p3=c->CreateSteamPipe();CHECK(p3&&p3!=p&&p3!=p2);
 auto u3=c->ConnectToGlobalUser(p3);CHECK(u3&&u3!=u&&u3!=u2);
 CHECK(!c->GetISteamUtils(p,"SteamUtils010"));
 CHECK(((ISteamUtils010*)c->GetISteamUtils(p3,"SteamUtils010"))->GetAppID()==480);
 c->ReleaseUser(p3,u3);CHECK(c->BReleaseSteamPipe(p3));shutdown();
 puts("PASS offline opt-in, exact factories, local handle lifecycle, stale handles, install-dir bounds; ownership/tickets/game=0");
 return 0;
}
