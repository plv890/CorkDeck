/* UI-only fixture. Never authenticates; challenge below is synthetic. */
#include "../source/launcher_ui.h"
#include "../source/steam/steam_ui.h"
#include "../source/steam/steam_provider.h"
#include "../source/steam/steam_content.h"
#include "../source/steam/steam_settings.h"
#include "../source/steam/steam_library_ui.h"
#include "../source/steam/steam_prerequisites_ui.h"
#include "../source/steam/steam_diagnostics.h"
#include "../source/diagnostics/lan_log.h"
#include "../source/launcher_image.h"
#include <sys/stat.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void wine_nx_runtime_trace(const char *s){puts(s);}
static int mode,install_calls;
static char installed_language[32];
int steam_provider_inspect(struct steam_provider *p,uint32_t app){(void)p;(void)app;return 1;}
void steam_provider_app_status(struct steam_provider *p,struct steam_app_view *v){(void)p;*v=(struct steam_app_view){.app_id=123,.state=2,.windows=1,.size=250u*1048576,.language_missing=mode==18,.available_languages=steam_language_mask("russian")|STEAM_LANGUAGE_BASE};}
int steam_provider_uninstall(struct steam_provider *p,uint32_t app){(void)p;(void)app;return 1;}
void steam_provider_remove_status(struct steam_provider *p,struct steam_remove_view *v){(void)p;memset(v,0,sizeof(*v));}
int steam_storage_directory(const char *r,uint32_t app,char out[512]){return snprintf(out,512,"%s/drive_c/Steam/Synthetic %u",r,app)>0;}
static int dummy,frames;
static const char *output;
void steam_provider_language(struct steam_provider *p,char out[32]){(void)p;strcpy(out,mode>=15?"english":"russian");}
void steam_provider_language_preference(struct steam_provider *p,char out[32]){steam_provider_language(p,out);}
int steam_provider_set_language(struct steam_provider *p,const char *s){(void)p;(void)s;return 1;}
unsigned int steam_provider_concurrency(struct steam_provider *p){(void)p;return 2;}
int steam_provider_set_concurrency(struct steam_provider *p,unsigned int n){(void)p;(void)n;return 1;}
void steam_provider_session_status(struct steam_provider *p,struct steam_session_view *v){(void)p;*v=(struct steam_session_view){.present=mode==6};}
int steam_provider_save_session(struct steam_provider *p,const char *s){(void)p;(void)s;return 1;}
int steam_provider_save_automatic_session(struct steam_provider *p){(void)p;return 1;}
int steam_provider_resume_session(struct steam_provider *p){(void)p;return 1;}
int steam_provider_unlock_session(struct steam_provider *p,const char *s){(void)p;(void)s;return 1;}
int steam_provider_set_root(struct steam_provider *p,const char *r){(void)p;(void)r;return 1;}
int steam_provider_refresh_library(struct steam_provider *p){(void)p;return 1;}
int steam_provider_install_language(struct steam_provider *p,uint32_t a,const char *lang){(void)p;assert(a==123);assert(lang);++install_calls;snprintf(installed_language,sizeof(installed_language),"%s",lang);return 1;}
int steam_provider_install(struct steam_provider *p,uint32_t a){(void)p;(void)a;return 1;}
void steam_provider_cancel_download(struct steam_provider *p){(void)p;}
void steam_provider_download_status(struct steam_provider *p,struct steam_download_view *v){(void)p;*v=(struct steam_download_view){.state=mode==12?STEAM_DOWNLOAD_CLEANING:mode==13?STEAM_DOWNLOAD_DISCARDED:(mode==14||mode==19||mode==20)?STEAM_DOWNLOAD_ERROR:mode==4?STEAM_DOWNLOAD_FILES:mode==10?STEAM_DOWNLOAD_COMPLETE:STEAM_DOWNLOAD_IDLE,.app_id=123,.downloaded=60u*1048576,.total=100u*1048576,.network_bytes=20u*1048576,.bytes_per_second=2.5*1048576,.elapsed_ms=15000,.network_ms=8000,.batch_files=mode==4?64:0,.batch_peak_files=mode==4?3:0,.requests=24,.connections=2,.parallelism=2,.peak_requests=2};if(mode==12||mode==13)v->bytes_per_second=0;
 if(mode==13)v->downloaded=v->total=0;
 if(mode==14){v->error=3112;v->cleanup_errno=5;}
 if(mode==20){v->error=3103;v->integrity_detail=STEAM_INTEGRITY_FILE_SHA;v->chunk_format=2;v->integrity_retries=2;}
 if(mode==19){v->error=3111;v->available_languages=steam_language_mask("russian")|STEAM_LANGUAGE_BASE;}
 strcpy(v->file,"data/example.dat");}
void steam_provider_library_status(struct steam_provider *p,struct steam_library_view *v){(void)p;*v=(struct steam_library_view){.state=STEAM_LIBRARY_READY,.count=2};}
size_t steam_provider_library_page(struct steam_provider *p,const char *s,size_t offset,struct steam_game *g,size_t capacity,size_t *total){(void)p;(void)s;*total=2;if(offset||capacity<2)return 0;g[0]=(struct steam_game){.app_id=123,.shared=1,.lender=42};strcpy(g[0].name,"Synthetic Windows game");g[1]=(struct steam_game){.app_id=456,.installed=1};strcpy(g[1].name,"Synthetic installed game");return 2;}
size_t steam_provider_installed_page(struct steam_provider *p,const char *s,size_t offset,struct steam_game *g,size_t capacity,size_t *total){struct steam_game all[2];(void)capacity;steam_provider_library_page(p,s,0,all,2,total);*total=1;if(offset)return 0;*g=all[1];return 1;}
int steam_install_executable(const char *r,uint32_t a,char out[512]){(void)r;(void)a;out[0]=0;return 0;}
int steam_install_select_executable(const char *r,uint32_t a,const char *e){(void)r;(void)a;(void)e;return 1;}
struct steam_provider *steam_provider_create(void){return (struct steam_provider *)&dummy;}
void steam_provider_destroy(struct steam_provider *p){(void)p;}
void steam_provider_logout(struct steam_provider *p){(void)p;}
int steam_provider_login(struct steam_provider *p,const char *a,const char *b){(void)p;(void)a;(void)b;return 1;}
int steam_provider_login_qr(struct steam_provider *p){(void)p;return 1;}
int steam_provider_guard(struct steam_provider *p,const char *c){(void)p;(void)c;return 1;}
void steam_provider_status(struct steam_provider *p,struct steam_status_view *s)
{(void)p;*s=(struct steam_status_view){.state=mode==6?STEAM_OFFLINE:mode?STEAM_ONLINE:STEAM_CONFIRM_DEVICE,.steam_id=UINT64_C(76561198000000000)};}
void steam_provider_challenge(struct steam_provider *p,char out[512])
{(void)p;strcpy(out,mode?"":"https://s.team/q/1/12345");}
static void capture(SDL_Renderer *renderer)
{
    if(mode>=15&&mode<=19){
        SDL_Event e={.type=SDL_KEYDOWN};++frames;
        if((mode==16 || mode==18 || mode==19) && frames==1){e.key.keysym.sym=mode==16?SDLK_DOWN:SDLK_RETURN;SDL_PushEvent(&e);return;}
        if(frames>2){e.key.keysym.sym=SDLK_ESCAPE;SDL_PushEvent(&e);return;}
        SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,1280,720,32,SDL_PIXELFORMAT_ARGB8888);assert(surface);
        assert(!SDL_RenderReadPixels(renderer,NULL,surface->format->format,surface->pixels,surface->pitch));
        assert(!SDL_SaveBMP(surface,output));SDL_FreeSurface(surface);
        e.key.keysym.sym=mode==17?SDLK_ESCAPE:SDLK_RETURN;SDL_PushEvent(&e);return;
    }
    if(mode==11){
        SDL_Event event={.type=SDL_KEYDOWN};++frames;
        event.key.keysym.sym=frames<=6?SDLK_DOWN:frames<=8?SDLK_RETURN:SDLK_ESCAPE;
        if(frames!=9){SDL_PushEvent(&event);return;}
    }
    if(mode==10){
        SDL_Event event={.type=SDL_KEYDOWN};++frames;
        event.key.keysym.sym=frames<=5?SDLK_DOWN:frames==6?SDLK_RETURN:SDLK_ESCAPE;
        if(frames!=7){SDL_PushEvent(&event);return;}
    }
    if((mode==2||mode==3)&&++frames<8)return;
    if(mode==7){SDL_Event e={.type=SDL_KEYDOWN};if(frames++<29){e.key.keysym.sym=SDLK_DOWN;SDL_PushEvent(&e);return;}
        e.key.keysym.sym=SDLK_RETURN;SDL_PushEvent(&e);}
    if(mode==8&&++frames!=2){if(frames>2){SDL_Event e={.type=SDL_KEYDOWN};e.key.keysym.sym=SDLK_ESCAPE;SDL_PushEvent(&e);}return;}
    if((mode==4||mode>=12)&&++frames<2)return;
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,1280,720,32,SDL_PIXELFORMAT_ARGB8888);
    SDL_Event event={0};assert(surface);
    assert(!SDL_RenderReadPixels(renderer,NULL,surface->format->format,surface->pixels,surface->pitch));
    assert(!SDL_SaveBMP(surface,output));SDL_FreeSurface(surface);
    event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_ESCAPE;SDL_PushEvent(&event);
}
int main(int argc,char **argv)
{
    struct ui ui;FILE *font;long length;unsigned char *data;
    assert(argc==4||argc==5);output=argv[2];mode=atoi(argv[3]);
    if (mode == 9) {
        char path[768], saved[2048], after[2048]; FILE *f; size_t n, m;
        struct steam_download_view download = {.state=STEAM_DOWNLOAD_COMPLETE,.app_id=123,
            .network_bytes=100000,.first_byte_ms=400,.body_ms=600,.verify_ms=20,
            .writer_wait_ms=30,.resume_ms=10};
        assert(argc == 5);
        snprintf(path,sizeof(path),"%s/logs/steam-download.log",argv[4]);
        strcpy(download.file,"DO_NOT_LOG_FILE_OR_SECRET");
        assert(!steam_download_diagnostic(argv[4],&download));
        f=fopen(path,"rb");assert(f);n=fread(saved,1,sizeof(saved)-1,f);fclose(f);saved[n]=0;
        assert(strstr(saved,"first_byte_ms=400 body_ms=600 verify_ms=20 resume_ms=10 writer_wait_ms=30"));
        assert(!strstr(saved,"DO_NOT_LOG"));
        memset(&download,0,sizeof(download));
        assert(!steam_download_diagnostic(argv[4],&download));
        f=fopen(path,"rb");assert(f);m=fread(after,1,sizeof(after),f);fclose(f);
        assert(n==m&&!memcmp(saved,after,n));
        download.app_id=456;download.state=STEAM_DOWNLOAD_ERROR;download.error=3106;
        assert(!steam_download_diagnostic(argv[4],&download));
        f=fopen(path,"rb");assert(f);m=fread(after,1,sizeof(after)-1,f);fclose(f);after[m]=0;
        assert(strstr(after,"appid=456 state=8 error=3106"));
        puts("Download diagnostics survive idle provider restart and retain only allowlisted numeric fields");
        return 0;
    }
    font=fopen(argv[1],"rb");assert(font);assert(!fseek(font,0,SEEK_END));length=ftell(font);rewind(font);
    data=malloc((size_t)length);assert(data);assert(fread(data,1,(size_t)length,font)==(size_t)length);fclose(font);
    assert(ui_init(&ui,data,(size_t)length,0));ui_present_hook=capture;
    if((mode==2||mode==3||mode==8)&&argc==5){
      char path[768];int game,x,y;unsigned char pixels[256*256*4];
      snprintf(path,sizeof(path),"%s/steam",argv[4]);mkdir(path,0700);
      snprintf(path,sizeof(path),"%s/steam/artwork",argv[4]);mkdir(path,0700);
      for(game=0;game<2;++game){
        unsigned char *jpeg=NULL;size_t size=0;FILE *f;
        struct launcher_icon icon={LAUNCHER_ICON_RGBA,256,256,pixels,sizeof(pixels)};
        for(y=0;y<256;++y)for(x=0;x<256;++x){size_t at=(y*256+x)*4;
          int ring=(x-128)*(x-128)+(y-128)*(y-128)>5000&&(x-128)*(x-128)+(y-128)*(y-128)<6200;
          pixels[at]=ring?240:game?120+x/2:20+x/2;
          pixels[at+1]=ring?235:40+y/2;pixels[at+2]=ring?230:180-y/3;pixels[at+3]=255;
        }
        assert(launcher_image_jpeg(&icon,&jpeg,&size));
        snprintf(path,sizeof(path),"%s/steam/artwork/%u.jpg",argv[4],game?456:123);
        f=fopen(path,"wb");assert(f&&fwrite(jpeg,1,size,f)==size&&!fclose(f));free(jpeg);
      }
    }
    if(mode>=15 && mode<=17){char lang[32]="unchanged";int selected=steam_ui_install_language_picker(&ui,steam_language_mask("russian")|STEAM_LANGUAGE_BASE,lang);
        if(mode==17)assert(!selected&&!lang[0]);
        else assert(selected&&!strcmp(lang,mode==16?STEAM_LANGUAGE_BASE_ID:"russian"));}
    else if(mode==21){
      char path[512];struct steam_launcher_adapter a={0};assert(argc==5);
      snprintf(path,sizeof(path),"%s/drive_c",argv[4]);mkdir(path,0700);
      snprintf(path,sizeof(path),"%s/drive_c/Steam",argv[4]);mkdir(path,0700);
      snprintf(path,sizeof(path),"%s/drive_c/Steam/Synthetic 123",argv[4]);mkdir(path,0700);
      assert(!steam_prerequisites_ui(&ui,argv[4],123,NULL,&a));
    }
    else if(mode==7){assert(steam_ui_language_picker(&ui,"english")==30);}
    else if(mode==5||mode==10||mode==11)steam_ui_settings(&ui,argc==5?argv[4]:"/tmp");
    else if(mode==6)steam_ui_open(&ui,argc==5?argv[4]:"/tmp");
    else if(mode>=2){struct steam_launcher_adapter adapter={0};SDL_Event event={.type=SDL_KEYDOWN};event.key.keysym.sym=SDLK_r;
        if(mode==8||mode==18){event.key.keysym.sym=SDLK_RETURN;SDL_PushEvent(&event);}
        else if(mode>=3)SDL_PushEvent(&event);
        if(mode==4||(mode>=12&&mode!=18))SDL_PushEvent(&event);
        steam_library_ui_open(&ui,(struct steam_provider *)&dummy,argc==5?argv[4]:"/tmp",&adapter);}
    else steam_ui_open(&ui,argc==5?argv[4]:"/tmp");
    if(mode==10){char path[768],text[2048];FILE *f;size_t n;
        assert(argc==5);snprintf(path,sizeof(path),"%s/logs/steam-report.log",argv[4]);
        f=fopen(path,"rb");assert(f);n=fread(text,1,sizeof(text)-1,f);assert(!fclose(f));text[n]=0;
        assert(strstr(text,"steam_report_v1 build=2026100608"));
        assert(strstr(text,"download_snapshot available=1 appid=123"));
        assert(!strstr(text,"76561198000000000")&&!strstr(text,"data/example.dat"));
        puts("Steam settings Export diagnostics button saves a checked report without SteamID or paths");}
    if(mode==11){char url[128];assert(lan_log_url(url,sizeof(url)));puts("LAN settings enable button creates a browser URL");}
    if(mode==18 || mode==19){char language[32];steam_provider_language((struct steam_provider *)&dummy,language);
        assert(install_calls==1&&!strcmp(installed_language,"russian")&&!strcmp(language,"english"));
        puts("Per-game language choice reaches installation without changing global English");}
    lan_log_stop();steam_ui_shutdown();ui_quit(&ui);free(data);return 0;
}
