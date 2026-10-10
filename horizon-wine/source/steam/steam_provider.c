#include "steam_provider.h"
#include "steam_auth.h"
#include "steam_cm.h"
#include "steam_crypto.h"
#include "steam_family.h"
#include "steam_session.h"
#include "steam_settings.h"
#include "steam_storage.h"
#include "steam_diagnostics.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>

struct steam_provider {
    SDL_Thread *thread; SDL_mutex *mutex; SDL_atomic_t cancel;
    struct steam_status_view view;
    char account[128],password[256],guard[17];
    char challenge[512];int qr;
    struct steam_library library;struct steam_library_view library_view;int refresh_library;
    char root[256];uint32_t install_app,auto_app;unsigned int auto_attempts;Uint32 auto_at;char auto_language[32];struct steam_download_view download;SDL_atomic_t download_cancel;
    /* Attempts in a row that failed and verified nothing new, and the download to begin anew once its files are gone. */
    uint32_t fail_app,restart_app;uint64_t fail_downloaded;unsigned int fail_count;char restart_language[32];
    char language[32], install_language[32];
    int language_inherited;
    unsigned int concurrency;
    uint32_t inspect_app, remove_app;
    struct steam_app_view app_view;
    struct steam_remove_view remove_view;
    char vault_pass[128];int restore,save_session;struct steam_session_view vault;
    struct steam_cm *active_cm;uint64_t active_id;uint32_t active_session;unsigned int heartbeat;Uint32 last_heartbeat;
};
/* Authentication rejection and protocol errors are never transport retries. */
static int retry_transport(unsigned int error,unsigned int transport)
{
    if(error>=1000)return 0;
    return (!error||error==2||error==6||error>=100)&&
        (transport==2||transport==6||(!transport&&error>=100));
}
static void state(struct steam_provider *p,enum steam_status status,unsigned int error,unsigned int guard,uint64_t id)
{
    SDL_LockMutex(p->mutex);p->view=(struct steam_status_view){.state=status,.error=error,.guard_type=guard,.steam_id=id};SDL_UnlockMutex(p->mutex);
}
static int cancelled(void *opaque) {return SDL_AtomicGet(&((struct steam_provider *)opaque)->cancel);}
static int wait_poll(struct steam_provider *p,struct steam_cm *cm,unsigned int ms)
{
    Uint32 started=SDL_GetTicks();
    do {if(cancelled(p)||!steam_cm_tick(cm))return 0;SDL_Delay(20);}while(SDL_GetTicks()-started<ms);
    return 1;
}
static int connection_tick(void *context)
{
    struct steam_provider *p=context;
    if(cancelled(p)||!steam_cm_tick(p->active_cm))return 0;
    if(SDL_GetTicks()-p->last_heartbeat>=p->heartbeat*1000){
        if(!steam_cm_send(p->active_cm,703,p->active_id,p->active_session,NULL,0))return 0;
        p->last_heartbeat=SDL_GetTicks();
    }
    return 1;
}
static int download_tick(void *context)
{struct steam_provider *p=context;return !SDL_AtomicGet(&p->download_cancel)&&connection_tick(context);}
static int download_discard_requested(void *context)
{return SDL_AtomicGet(&((struct steam_provider *)context)->download_cancel);}
static void download_progress(void *context,const struct steam_download_view *view)
{struct steam_provider *p=context;SDL_LockMutex(p->mutex);p->download=*view;SDL_UnlockMutex(p->mutex);}
/* Called only outside install_run or after joining its worker. Ordinary
 * disconnects retain staging; only a user's explicit Cancel reaches this path. */
static void finish_discard(struct steam_provider *p)
{
    struct steam_download_view v;
    struct steam_install_callbacks cb={.context=p,.progress=download_progress};
    SDL_LockMutex(p->mutex);
    v=p->download;
    if(!SDL_AtomicGet(&p->download_cancel)||!v.app_id||
       v.state==STEAM_DOWNLOAD_COMPLETE||v.state==STEAM_DOWNLOAD_DISCARDED||
       (v.state==STEAM_DOWNLOAD_ERROR&&v.error==3112)){
        SDL_UnlockMutex(p->mutex);return;
    }
    if(p->install_app==v.app_id)p->install_app=0;
    SDL_UnlockMutex(p->mutex);
    steam_install_discard(p->root,&v,&cb);
    v.diagnostic_error=steam_download_diagnostic(p->root,&v);
    download_progress(p,&v);
}
static int discard_worker(void *opaque)
{finish_discard(opaque);return 0;}
static int rsa_key(const struct steam_cm_message *m,char *mod,char *exp,uint64_t *stamp)
{
    struct steam_wire_reader r={m->body,m->body_size,0,0};struct steam_wire_field f;unsigned int seen=0;int n;
    if(m->type!=147 || m->result!=1)return 0;
    while((n=steam_wire_next(&r,&f))==1) {
        if(f.number<1||f.number>3)continue;
        if(seen&(1u<<f.number))return 0;
        seen|=1u<<f.number;
        if(f.number==3){if(f.type!=0||!f.integer)return 0;*stamp=f.integer;}
        else {char *out=f.number==1?mod:exp;size_t cap=f.number==1?1025:9;
            if(f.type!=2||!f.size||f.size>=cap||memchr(f.data,0,f.size))return 0;
            memcpy(out,f.data,f.size);out[f.size]=0;}
    }
    return n==0 && seen==14;
}
static int logon(struct steam_cm *cm,struct steam_auth *auth,uint32_t *session,uint32_t *cell,unsigned int *heartbeat,unsigned int *error)
{
    unsigned char body[4608];struct steam_wire_writer w={body,sizeof(body),0,0};struct steam_cm_message m;
    struct steam_wire_reader r;struct steam_wire_field f;int n;unsigned int seen=0,result=2,hb=30;uint32_t zone=0;
    steam_wire_uint(&w,1,65580);steam_wire_bytes(&w,6,"english",7);
    steam_wire_bytes(&w,50,auth->account_name,strlen(auth->account_name));
    /* Steam's logon field is called access_token but expects refresh_token. */
    steam_wire_bytes(&w,108,auth->refresh_token,strlen(auth->refresh_token));
    if(w.failed || !steam_cm_send(cm,5514,auth->steam_id,0,body,w.size)){steam_secret_clear(body,sizeof(body));return 0;}
    steam_secret_clear(body,sizeof(body));
    if(!steam_cm_exchange(cm,NULL,0,0,751,&m))return 0;
    r=(struct steam_wire_reader){m.body,m.body_size,0,0};
    while((n=steam_wire_next(&r,&f))==1){
        if(f.number==1){if((seen&1)||f.type!=0||f.integer>UINT32_MAX){*error=11;return 0;}seen|=1;result=(unsigned int)f.integer;}
        if(f.number==2){if((seen&2)||f.type!=0||f.integer>300){*error=11;return 0;}seen|=2;hb=(unsigned int)f.integer;}
        if(f.number==7){if((seen&4)||f.type!=0||f.integer>UINT32_MAX){*error=11;return 0;}seen|=4;zone=(uint32_t)f.integer;}
    }
    if(n<0||!(seen&1)){*error=11;return 0;}
    if(result!=1){*error=1000+result;return 0;}
    if(!m.steam_id||m.steam_id!=auth->steam_id){*error=12;return 0;}
    if(!m.session_id){*error=13;return 0;}
    *error=0;*session=m.session_id;*cell=zone;*heartbeat=hb?hb:30;return 1;
}
static int run(void *opaque)
{
    struct steam_provider *p=opaque;struct steam_auth *auth=calloc(1,sizeof(*auth));struct steam_cm *cm=NULL;
    unsigned char body[256],packet[4096];struct steam_wire_writer w={body,sizeof(body),0,0};
    struct steam_cm_message m;char mod[1025]={0},exp[9]={0},encrypted[1024]={0},code[17]={0};
    uint64_t stamp=0,job=1;size_t written=0;unsigned int error=0,guard_error=0,guard_type=0,hb=30,retries=0;uint32_t session=0,cell=0;Uint32 start,last;
    if(!auth){error=5;goto done;}
    if(p->restore){
        state(p,STEAM_AUTHENTICATING,0,0,0);
        error=p->restore==2?steam_session_load_automatic(p->root,auth):steam_session_load(p->root,p->vault_pass,auth);
        steam_secret_clear(p->vault_pass,sizeof(p->vault_pass));
        if(error)goto done;
    }
connect:
    error=0;session=0;cell=0;job=1;
    state(p,STEAM_CONNECTING,0,0,0);cm=steam_cm_connect(cancelled,p,&error);if(!cm)goto done;
    if(auth->state==STEAM_AUTH_TOKENS)goto authenticated;
    state(p,STEAM_AUTHENTICATING,0,0,0);
    if(p->qr){
        if(!steam_auth_begin_qr(auth,job,packet,sizeof(packet),&written)){error=3;goto done;}
    }else{
    steam_wire_bytes(&w,1,p->account,strlen(p->account));
    if(!steam_cm_auth_request(STEAM_AUTH_RSA,job,body,w.size,packet,sizeof(packet),&written) ||
       !steam_cm_exchange(cm,packet,written,job,0,&m))goto done;
    if(!rsa_key(&m,mod,exp,&stamp)){error=1000+m.result;goto done;}
    if(!steam_rsa_encrypt(mod,exp,p->password,encrypted,sizeof(encrypted))){error=9;goto done;}
    steam_secret_clear(p->password,sizeof(p->password));
    if(!steam_auth_begin_credentials(auth,++job,p->account,encrypted,stamp,packet,sizeof(packet),&written)){error=3;goto done;}
    steam_secret_clear(encrypted,sizeof(encrypted));
    }
    if(!steam_cm_exchange(cm,packet,written,job,0,&m))goto done;
    steam_secret_clear(packet,sizeof(packet));
    if(!steam_auth_accept(auth,&m)){error=1000+auth->result;goto done;}
    if(auth->confirmations & (1u<<3))guard_type=3;else if(auth->confirmations & (1u<<2))guard_type=2;
    if(!p->qr && !(auth->confirmations & ((1u<<1)|(1u<<2)|(1u<<3)|(1u<<4)))){error=10;goto done;}
    start=SDL_GetTicks();
    while(auth->state!=STEAM_AUTH_TOKENS && !cancelled(p)) {
        if(SDL_GetTicks()-start>180000){error=1027;goto done;}
        state(p,guard_type?STEAM_NEED_GUARD:STEAM_CONFIRM_DEVICE,guard_error,guard_type,0);
        SDL_LockMutex(p->mutex);strcpy(p->challenge,auth->challenge);SDL_UnlockMutex(p->mutex);
        SDL_LockMutex(p->mutex);memcpy(code,p->guard,sizeof(code));steam_secret_clear(p->guard,sizeof(p->guard));SDL_UnlockMutex(p->mutex);
        if(code[0] && guard_type){
            if(!steam_auth_guard(auth,++job,guard_type,code,packet,sizeof(packet),&written)){error=3;goto done;}
            steam_secret_clear(code,sizeof(code));
            if(!steam_cm_exchange(cm,packet,written,job,0,&m))goto done;
            steam_secret_clear(packet,sizeof(packet));
            if(!steam_auth_accept(auth,&m)){error=1000+auth->result;goto done;}
            if(auth->result==65||auth->result==88){guard_error=1000+auth->result;state(p,STEAM_NEED_GUARD,guard_error,guard_type,0);}
            else {guard_type=0;guard_error=0;}
        }
        if(!wait_poll(p,cm,auth->poll_interval_ms))goto done;
        if(!steam_auth_poll(auth,++job,packet,sizeof(packet),&written)){error=3;goto done;}
        if(!steam_cm_exchange(cm,packet,written,job,0,&m))goto done;
        steam_secret_clear(packet,sizeof(packet));
        if(!steam_auth_accept(auth,&m)){error=1000+auth->result;goto done;}
    }
    if(cancelled(p))goto done;
    SDL_LockMutex(p->mutex);steam_secret_clear(p->challenge,sizeof(p->challenge));SDL_UnlockMutex(p->mutex);
    if(p->qr){auth->steam_id=steam_token_subject(auth->refresh_token);if(!auth->steam_id||!auth->account_name[0]){error=3;goto done;}}
    if(!auth->account_name[0])strcpy(auth->account_name,p->account);
authenticated:
    state(p,STEAM_LOGGING_ON,0,0,0);
    if(!logon(cm,auth,&session,&cell,&hb,&error))goto done;
    state(p,STEAM_ONLINE,0,0,auth->steam_id);
    steam_secret_clear(auth->access_token,sizeof(auth->access_token));
    p->active_cm=cm;p->active_id=auth->steam_id;p->active_session=session;p->heartbeat=hb;p->last_heartbeat=SDL_GetTicks();
    SDL_LockMutex(p->mutex);p->library_view.state=STEAM_LIBRARY_IDLE;SDL_UnlockMutex(p->mutex);
    steam_provider_refresh_library(p);
    last=SDL_GetTicks();
    while(!cancelled(p)) {
      finish_discard(p);
      int refresh;
      uint32_t install_app, inspect_app, remove_app;
      char install_language[32];
      char vault_pass[128] = {0};
      int save;
      SDL_LockMutex(p->mutex);
      save = p->save_session;
      p->save_session = 0;
      if (save) {
        memcpy(vault_pass, p->vault_pass, sizeof(vault_pass));
        steam_secret_clear(p->vault_pass, sizeof(p->vault_pass));}
        SDL_UnlockMutex(p->mutex);
        if(save){unsigned int e=save==2?steam_session_save_automatic(p->root,auth):steam_session_save(p->root,vault_pass,auth);steam_secret_clear(vault_pass,sizeof(vault_pass));
            SDL_LockMutex(p->mutex);p->vault.error=e;p->vault.saving=0;p->vault.present=steam_session_present(p->root);p->vault.automatic=steam_session_automatic(p->root);SDL_UnlockMutex(p->mutex);
            if(!connection_tick(p))goto done;
        }
        SDL_LockMutex(p->mutex);
        refresh = p->refresh_library;
        p->refresh_library = 0;
        /* An installation the network interrupted goes on by itself. */
        if(!p->install_app&&p->auto_app&&!SDL_AtomicGet(&p->download_cancel)&&(Sint32)(SDL_GetTicks()-p->auto_at)>=0){
            p->install_app=p->auto_app;p->auto_app=0;strcpy(p->install_language,p->auto_language);
            p->download=(struct steam_download_view){.state=STEAM_DOWNLOAD_PLANNING,.app_id=p->install_app};
            strcpy(p->download.language,p->install_language);
        }
        install_app = p->install_app;
        strcpy(install_language, p->install_language);
        p->install_app = 0;
        inspect_app = p->inspect_app;
        p->inspect_app = 0;
        remove_app = p->remove_app;
        p->remove_app = 0;
        SDL_UnlockMutex(p->mutex);
        if(refresh){
            struct steam_library replacement={0};unsigned int lib_error=0,family_error=0;
            struct steam_backend family_backend={.cm=cm,.account=auth->steam_id,.session=session,.cell_id=cell,.job=job};
            SDL_LockMutex(p->mutex);strcpy(family_backend.language,p->language);SDL_UnlockMutex(p->mutex);
            w=(struct steam_wire_writer){body,sizeof(body),0,0};
            steam_wire_uint(&w,1,auth->steam_id);steam_wire_uint(&w,2,1);steam_wire_uint(&w,3,1);
            steam_wire_uint(&w,5,1);steam_wire_uint(&w,6,0);steam_wire_bytes(&w,7,family_backend.language,strlen(family_backend.language));
            if(!steam_cm_call(cm,STEAM_SERVICE_OWNED,auth->steam_id,session,++job,body,w.size,&m))lib_error=2000+steam_cm_error(cm);
            else if(m.result!=1)lib_error=1000+m.result;
            else {unsigned int parsed=(unsigned int)steam_library_read(&m,auth->steam_id,&replacement);lib_error=parsed?3200+parsed:0;}
            if(!lib_error){family_backend.job=job;family_error=steam_family_refresh(&family_backend,&replacement);job=family_backend.job;}
            if(!lib_error&&p->root[0]){size_t i;
              for (i = 0; i < replacement.count; ++i) {
                char directory[512];
                replacement.games[i].installed =
                    steam_install_present(p->root, replacement.games[i].app_id)
                        ? 1
                    : steam_storage_pending(
                          p->root, replacement.games[i].app_id, directory)
                        ? 2
                        : 0;
                if (i % 32 == 0 && !connection_tick(p)) {
                  lib_error = 2000 + steam_cm_error(cm);
                  break;
                }
              }
            }
            SDL_LockMutex(p->mutex);
            if(!lib_error){steam_library_clear(&p->library);p->library=replacement;p->library_view.count=p->library.count;p->library_view.generation++;}
            else steam_library_clear(&replacement);
            p->library_view.error=lib_error;p->library_view.family_error=family_error;p->library_view.state=lib_error?STEAM_LIBRARY_FAILED:STEAM_LIBRARY_READY;
            SDL_UnlockMutex(p->mutex);
        }
        if (inspect_app) {
          struct steam_app app;
          struct steam_backend backend = {.cm = cm,
                                          .account = auth->steam_id,
                                          .session = session,
                                          .cell_id = cell,
                                          .job = job};
          struct steam_app_view view = {.app_id = inspect_app, .state = 2};
          size_t i;
          SDL_LockMutex(p->mutex);
          strcpy(backend.language, p->language);
          SDL_UnlockMutex(p->mutex);
          if (!steam_backend_app(&backend, inspect_app, &app)) {
            view.state = 3;
            view.error = backend.error;
          } else if (app.windows && app.language_missing && app.english_available &&
                     /* What the installer will do (steam_install_run): a game
                      * with no files in the chosen language is installed in
                      * English, so that is what the page describes. */
                     (snprintf(backend.language, sizeof(backend.language), "english"),
                      view.english_fallback = 1,
                      !steam_backend_app(&backend, inspect_app, &app))) {
            view.state = 3;
            view.error = backend.error;
          } else {
            view.windows = app.windows;
            strcpy(view.installscript,app.installscript);
            view.language_missing = app.language_missing;
            view.english_available = app.english_available;
            view.available_languages = app.available_languages;
            for (i = 0; i < app.count; ++i) {
              struct steam_depot *depot = app.depots + i;
              if (!depot->manifest && depot->source_app != inspect_app) {
                struct steam_app source;
                size_t j;
                if (!steam_backend_app(&backend, depot->source_app, &source)) {
                  view.state = 3;
                  view.error = backend.error;
                  break;
                }
                for (j = 0; j < source.count; ++j)
                  if (source.depots[j].id == depot->id) {
                    depot->size = source.depots[j].size;
                    break;
                  }
              }
              if (UINT64_MAX - view.size < depot->size) {
                view.state = 3;
                view.error = 3002;
                break;
              }
              view.size += depot->size;
            }
          }
          job = backend.job;
          SDL_LockMutex(p->mutex);
          if (p->app_view.app_id == inspect_app)
            p->app_view = view;
          SDL_UnlockMutex(p->mutex);
        }
        if (remove_app) {
          unsigned int error =
              steam_storage_uninstall(p->root, remove_app, connection_tick, p);
          size_t i;
          SDL_LockMutex(p->mutex);
          p->remove_view.error = error;
          p->remove_view.busy = 0;
          if (!error) {
            for (i = 0; i < p->library.count; ++i)
              if (p->library.games[i].app_id == remove_app)
                p->library.games[i].installed = 0;
            p->library_view.generation++;
            if (p->download.app_id == remove_app)
              memset(&p->download, 0, sizeof(p->download));
          }
          SDL_UnlockMutex(p->mutex);
          if (!connection_tick(p))
            goto done;
        }
        if(install_app){
            struct steam_backend backend={.cm=cm,.account=auth->steam_id,.session=session,.cell_id=cell,.job=job};
            strcpy(backend.language,install_language);
            SDL_LockMutex(p->mutex);backend.concurrency=p->concurrency;SDL_UnlockMutex(p->mutex);
            struct steam_install_callbacks callbacks={.context=p,.tick=download_tick,.progress=download_progress,.discard_requested=download_discard_requested};
            int installed=steam_install_run(&backend,p->root,install_app,&callbacks);job=backend.job;
            if(installed){size_t i;SDL_LockMutex(p->mutex);
                for(i=0;i<p->library.count;++i)if(p->library.games[i].app_id==install_app)p->library.games[i].installed=1;
                p->auto_attempts=0;p->fail_app=0;p->fail_count=0;p->library_view.generation++;SDL_UnlockMutex(p->mutex);}
            else{
                /* Steam's CDN or the connection to Steam gave out: what is on
                 * the card is verified and kept, so try again shortly, and stop
                 * only after five attempts in a row that brought nothing. */
                SDL_LockMutex(p->mutex);
                if(!SDL_AtomicGet(&p->download_cancel)&&p->download.state==STEAM_DOWNLOAD_ERROR&&
                   (p->download.error==3106||(p->download.error>=2000&&p->download.error<3000))){
                    if(p->download.network_bytes>=8u*1024u*1024u)p->auto_attempts=0;
                    if(p->auto_attempts<5){
                        p->auto_app=install_app;strcpy(p->auto_language,install_language);
                        p->auto_at=SDL_GetTicks()+(3000u<<p->auto_attempts++);
                    }
                }
                /* A failure that comes again with nothing more verified is one
                 * Continue will not get past: the screen then offers to begin
                 * anew or to cancel. Every failure is written down with its cause. */
                if(!SDL_AtomicGet(&p->download_cancel)&&p->download.state==STEAM_DOWNLOAD_ERROR&&
                   p->download.error!=3111&&p->download.error!=3112){
                    struct steam_download_view failed;int again=p->auto_app!=0;
                    if(p->fail_app==install_app&&p->download.downloaded<=p->fail_downloaded)p->fail_count++;
                    else p->fail_count=1;
                    p->fail_app=install_app;p->fail_downloaded=p->download.downloaded;
                    p->download.attempts=p->fail_count;
                    p->download.stuck=p->fail_count>=2&&!again;
                    failed=p->download;
                    SDL_UnlockMutex(p->mutex);
                    steam_download_failure_log(p->root,&failed,failed.stuck?"cannot be continued":again?"will be tried again by itself":"can be continued");
                    SDL_LockMutex(p->mutex);
                }
                SDL_UnlockMutex(p->mutex);
            }
            last=p->last_heartbeat;
        }
        if(!steam_cm_tick(cm))goto done;
        if(SDL_GetTicks()-last>=hb*1000){
            if(!steam_cm_send(cm,703,auth->steam_id,session,NULL,0))goto done;
            last=SDL_GetTicks();
            p->last_heartbeat=last;
        }
        SDL_Delay(50);
    }
done:
    /* Only transport loss retries token logon. Server rejection/protocol faults
     * stop immediately. No password/Guard replay, no automatic install replay. */
    if(!cancelled(p)&&auth&&auth->state==STEAM_AUTH_TOKENS&&retries<3&&
       retry_transport(error,cm?steam_cm_error(cm):0)) {
        Uint32 delay=2000u<<retries++, waiting=SDL_GetTicks();
        state(p,STEAM_CONNECTING,0,0,0);
        p->active_cm=NULL;steam_cm_close(cm);cm=NULL;
        while(!cancelled(p)&&SDL_GetTicks()-waiting<delay)SDL_Delay(50);
        if(!cancelled(p))goto connect;
    }
    SDL_LockMutex(p->mutex);steam_secret_clear(p->challenge,sizeof(p->challenge));SDL_UnlockMutex(p->mutex);
    if(!error && cm)error=steam_cm_error(cm);
    if(cancelled(p))state(p,STEAM_OFFLINE,0,0,0);
    else {
        SDL_LockMutex(p->mutex);
        p->view.failed_stage=(unsigned int)p->view.state;p->view.cm_type=cm?steam_cm_last_type(cm):0;
        p->view.transport_error=cm?steam_cm_error(cm):steam_cm_connect_transport();
        p->view.state=STEAM_ERROR;p->view.error=error?error:3;p->view.steam_id=0;
        SDL_UnlockMutex(p->mutex);
    }
    /* Publish offline/error before the final cleanup check, so a Cancel racing
     * with CM shutdown either reaches this check or joins and starts a local job. */
    finish_discard(p);
    p->active_cm=NULL;p->active_id=0;p->active_session=0;
    steam_cm_close(cm);
    if(auth){steam_auth_clear(auth);free(auth);}
    steam_secret_clear(p->password,sizeof(p->password));steam_secret_clear(p->account,sizeof(p->account));
    steam_secret_clear(p->vault_pass,sizeof(p->vault_pass));
    SDL_LockMutex(p->mutex);p->vault.saving=0;p->save_session=0;SDL_UnlockMutex(p->mutex);
    steam_secret_clear(encrypted,sizeof(encrypted));steam_secret_clear(code,sizeof(code));steam_secret_clear(packet,sizeof(packet));
    return 0;
}
struct steam_provider *steam_provider_create(void)
{
    struct steam_provider *p=calloc(1,sizeof(*p));if(!p)return NULL;
    p->mutex=SDL_CreateMutex();if(!p->mutex){free(p);return NULL;}strcpy(p->language,"english");p->concurrency=STEAM_CONNECTIONS_DEFAULT;return p;
}
static void disconnect(struct steam_provider *p)
{
    if(!p)return;
    SDL_AtomicSet(&p->cancel,1);if(p->thread){SDL_WaitThread(p->thread,NULL);p->thread=NULL;}
    finish_discard(p);
    steam_secret_clear(p->password,sizeof(p->password));steam_secret_clear(p->guard,sizeof(p->guard));
    steam_secret_clear(p->vault_pass,sizeof(p->vault_pass));p->save_session=0;p->restore=0;
    SDL_LockMutex(p->mutex);steam_library_clear(&p->library);memset(&p->library_view,0,sizeof(p->library_view));p->refresh_library=0;SDL_UnlockMutex(p->mutex);
    SDL_LockMutex(p->mutex);
    p->install_app = p->auto_app = 0;
    p->fail_app = p->restart_app = 0;
    p->fail_count = 0;
    p->auto_attempts = 0;
    p->inspect_app = 0;
    p->remove_app = 0;
    memset(&p->app_view, 0, sizeof(p->app_view));
    memset(&p->remove_view, 0, sizeof(p->remove_view));
    memset(&p->download, 0, sizeof(p->download));
    SDL_UnlockMutex(p->mutex);
    SDL_AtomicSet(&p->download_cancel,0);
    state(p,STEAM_OFFLINE,0,0,0);
}
void steam_provider_logout(struct steam_provider *p)
{
    if(!p)return;
    disconnect(p);
    p->vault.error=p->root[0]?steam_session_forget(p->root):0;
    p->vault.present=p->root[0]?steam_session_present(p->root):0;
    p->vault.saving=0;p->vault.automatic=0;
}
void steam_provider_destroy(struct steam_provider *p)
{if(p){disconnect(p);SDL_DestroyMutex(p->mutex);steam_secret_clear(p,sizeof(*p));free(p);}}
int steam_provider_login(struct steam_provider *p,const char *account,const char *password)
{
    if(!account[0]||!password[0]||strlen(account)>=sizeof(p->account)||strlen(password)>=sizeof(p->password))return 0;
    steam_provider_logout(p);p->qr=0;strcpy(p->account,account);strcpy(p->password,password);SDL_AtomicSet(&p->cancel,0);
    state(p,STEAM_CONNECTING,0,0,0);p->thread=SDL_CreateThreadWithStackSize(run,"steam-session",256u*1024u,p);
    if(!p->thread){steam_secret_clear(p->password,sizeof(p->password));state(p,STEAM_ERROR,5,0,0);return 0;}return 1;
}
int steam_provider_guard(struct steam_provider *p,const char *code)
{
    size_t i,n=strlen(code);if(!n||n>=sizeof(p->guard))return 0;
    for(i=0;i<n;++i)if(!((code[i]>='A'&&code[i]<='Z')||(code[i]>='0'&&code[i]<='9')))return 0;
    SDL_LockMutex(p->mutex);strcpy(p->guard,code);SDL_UnlockMutex(p->mutex);return 1;
}
int steam_provider_login_qr(struct steam_provider *p)
{
    steam_provider_logout(p);p->qr=1;SDL_AtomicSet(&p->cancel,0);
    state(p,STEAM_CONNECTING,0,0,0);p->thread=SDL_CreateThreadWithStackSize(run,"steam-session",256u*1024u,p);
    if(!p->thread){state(p,STEAM_ERROR,5,0,0);return 0;}return 1;
}
void steam_provider_challenge(struct steam_provider *p,char out[512])
{SDL_LockMutex(p->mutex);memcpy(out,p->challenge,512);SDL_UnlockMutex(p->mutex);}
void steam_provider_status(struct steam_provider *p,
                           struct steam_status_view *out) {
  SDL_LockMutex(p->mutex);
  *out = p->view;
  SDL_UnlockMutex(p->mutex);
}
int steam_provider_refresh_library(struct steam_provider *p)
{
    int ok;SDL_LockMutex(p->mutex);ok=p->view.state==STEAM_ONLINE&&p->library_view.state!=STEAM_LIBRARY_LOADING;
    if (ok) {
      if (p->app_view.state == 3)
        memset(&p->app_view, 0, sizeof(p->app_view));
      p->refresh_library = 1;
      p->library_view.state = STEAM_LIBRARY_LOADING;
      p->library_view.error = 0;
    }
    SDL_UnlockMutex(p->mutex);return ok;
}
void steam_provider_library_status(struct steam_provider *p,struct steam_library_view *out)
{SDL_LockMutex(p->mutex);*out=p->library_view;SDL_UnlockMutex(p->mutex);}
size_t steam_provider_library_page(struct steam_provider *p,const char *search,size_t offset,struct steam_game *out,size_t capacity,size_t *total)
{size_t n;SDL_LockMutex(p->mutex);n=steam_library_page(&p->library,search,offset,out,capacity,total);SDL_UnlockMutex(p->mutex);return n;}
int steam_provider_set_root(struct steam_provider *p,const char *root)
{
    if(!root[0]||strlen(root)>=sizeof(p->root))return 0;
    SDL_LockMutex(p->mutex);
    if(!p->root[0]&&p->thread){SDL_UnlockMutex(p->mutex);return 0;}
    if(p->root[0]&&strcmp(p->root,root)){SDL_UnlockMutex(p->mutex);return 0;}
    int start=!p->root[0];
    if(start){strcpy(p->root,root);p->language_inherited=steam_settings_load(root,p->language)!=1;p->concurrency=steam_settings_concurrency(root);p->vault.present=steam_session_present(root);p->vault.automatic=steam_session_automatic(root);}
    SDL_UnlockMutex(p->mutex);
    if(start&&p->vault.automatic)steam_provider_resume_session(p);
    return 1;
}
unsigned int steam_provider_concurrency(struct steam_provider *p){unsigned int n;SDL_LockMutex(p->mutex);n=p->concurrency;SDL_UnlockMutex(p->mutex);return n;}
int steam_provider_set_concurrency(struct steam_provider *p,unsigned int n){
    int ok=0;if(!steam_connections_valid(n))return 0;SDL_LockMutex(p->mutex);
    if(p->root[0]&&!p->install_app&&!steam_download_active(p->download.state)&&
       steam_settings_save_concurrency(p->root,n)){p->concurrency=n;ok=1;}
    SDL_UnlockMutex(p->mutex);return ok;
}
size_t steam_provider_installed_page(struct steam_provider *p,const char *search,size_t offset,struct steam_game *out,size_t capacity,size_t *total)
{
    size_t i,n=0,found=0;SDL_LockMutex(p->mutex);
    for(i=0;i<p->library.count;++i)if(p->library.games[i].installed&&steam_library_matches(&p->library.games[i],search)){
        if(found++>=offset&&n<capacity)out[n++]=p->library.games[i];}
    *total=found;SDL_UnlockMutex(p->mutex);return n;
}
int steam_provider_install(struct steam_provider *p,uint32_t app)
{return steam_provider_install_language(p,app,NULL);}
int steam_provider_install_language(struct steam_provider *p,uint32_t app,const char *language)
{
    if(language && !steam_language_valid(language) && strcmp(language,STEAM_LANGUAGE_BASE_ID))return 0;
    size_t i;int ok=0;SDL_LockMutex(p->mutex);
    if (p->view.state == STEAM_ONLINE && p->root[0] && !p->install_app &&
        !p->remove_view.busy && p->download.error!=3112 &&
        !steam_download_active(p->download.state)) {
      for (i = 0; i < p->library.count; ++i)
        if (p->library.games[i].app_id == app &&
            !p->library.games[i].installed) {
          ok = 1;
          break;
        }
      if (ok) {
        p->install_app = app;
        p->auto_app = 0;
        p->auto_attempts = 0;
        strcpy(p->install_language,language?language:p->language);
        /* Retrying an interrupted transfer preserves its explicitly selected language. */
        if(!language && p->download.app_id==app && p->download.language[0] &&
           (p->download.state==STEAM_DOWNLOAD_CANCELLED || p->download.state==STEAM_DOWNLOAD_ERROR))
          strcpy(p->install_language,p->download.language);
        p->download = (struct steam_download_view){
            .state = STEAM_DOWNLOAD_PLANNING, .app_id = app};
        strcpy(p->download.language,p->install_language);
        SDL_AtomicSet(&p->download_cancel, 0);
      }
    }
    SDL_UnlockMutex(p->mutex);return ok;
}
void steam_provider_cancel_download(struct steam_provider *p)
{
    int offline=0;
    SDL_LockMutex(p->mutex);
    if(p->download.app_id &&
       ((p->download.state>STEAM_DOWNLOAD_IDLE&&p->download.state<STEAM_DOWNLOAD_COMMITTING)||
        p->download.state==STEAM_DOWNLOAD_CANCELLED||p->download.state==STEAM_DOWNLOAD_ERROR)){
        SDL_AtomicSet(&p->download_cancel,1);
        p->auto_app=0;p->fail_app=0;p->fail_count=0;p->download.stuck=0;
        if(!steam_download_active(p->download.state))p->download.state=STEAM_DOWNLOAD_CLEANING;
        offline=p->view.state!=STEAM_ONLINE;
    }
    SDL_UnlockMutex(p->mutex);
    if(offline){
        /* A failed CM worker may already have stopped. Cleanup needs no Steam
         * session; keep SD deletion off the rendering thread. */
        if(p->thread){SDL_WaitThread(p->thread,NULL);p->thread=NULL;}
        p->thread=SDL_CreateThreadWithStackSize(discard_worker,"steam-discard",128u*1024u,p);
        if(!p->thread){
            SDL_LockMutex(p->mutex);p->download.state=STEAM_DOWNLOAD_ERROR;
            p->download.error=5;p->download.failed_stage=STEAM_DOWNLOAD_CLEANING;SDL_UnlockMutex(p->mutex);
        }
    }
}
/* Removes what was downloaded and, once it is gone, starts the same download
 * from nothing, in the language it was started in. */
int steam_provider_restart_download(struct steam_provider *p)
{
    struct steam_download_view v;
    SDL_LockMutex(p->mutex);
    v=p->download;
    if(!v.app_id||(v.state!=STEAM_DOWNLOAD_ERROR&&v.state!=STEAM_DOWNLOAD_CANCELLED)||v.error==3112){SDL_UnlockMutex(p->mutex);return 0;}
    p->restart_app=v.app_id;strcpy(p->restart_language,v.language);
    SDL_UnlockMutex(p->mutex);
    steam_download_failure_log(p->root,&v,"the user chose to start over");
    steam_provider_cancel_download(p);
    return 1;
}
/* 1 when the download asked to start over has just been started, -1 when it could not be. */
int steam_provider_restart_poll(struct steam_provider *p)
{
    uint32_t app=0;char language[32]="";int failed=0;
    SDL_LockMutex(p->mutex);
    if(p->restart_app){
        if(p->download.app_id!=p->restart_app||(p->download.state==STEAM_DOWNLOAD_ERROR&&p->download.failed_stage==STEAM_DOWNLOAD_CLEANING)){p->restart_app=0;failed=1;}
        else if(p->download.state==STEAM_DOWNLOAD_DISCARDED){app=p->restart_app;strcpy(language,p->restart_language);p->restart_app=0;}
    }
    SDL_UnlockMutex(p->mutex);
    if(failed)return -1;
    if(!app)return 0;
    return steam_provider_install_language(p,app,language[0]?language:NULL)?1:-1;
}
void steam_provider_download_status(struct steam_provider *p,struct steam_download_view *out)
{SDL_LockMutex(p->mutex);*out=p->download;SDL_UnlockMutex(p->mutex);}
void steam_provider_language(struct steam_provider *p,char out[32])
{SDL_LockMutex(p->mutex);strcpy(out,p->language);SDL_UnlockMutex(p->mutex);}
void steam_provider_language_preference(struct steam_provider *p,char out[32])
{SDL_LockMutex(p->mutex);strcpy(out,p->language_inherited?"system":p->language);SDL_UnlockMutex(p->mutex);}
int steam_provider_set_language(struct steam_provider *p,const char *language)
{
    if((!steam_language_valid(language)&&strcmp(language,"system"))||!p->root[0]||!steam_settings_save(p->root,language))return 0;
    SDL_LockMutex(p->mutex);
    p->language_inherited=steam_settings_load(p->root,p->language)!=1;
    memset(&p->app_view, 0, sizeof(p->app_view));
    SDL_UnlockMutex(p->mutex);
    steam_provider_refresh_library(p);return 1;
}
void steam_provider_session_status(struct steam_provider *p,struct steam_session_view *out)
{SDL_LockMutex(p->mutex);*out=p->vault;SDL_UnlockMutex(p->mutex);}
int steam_provider_save_session(struct steam_provider *p,const char *pass)
{
    int ok;if(!steam_session_passphrase_valid(pass))return 0;
    SDL_LockMutex(p->mutex);ok=p->view.state==STEAM_ONLINE&&!p->vault.saving&&!p->save_session;
    if(ok){strcpy(p->vault_pass,pass);p->save_session=1;p->vault.saving=1;p->vault.error=0;}
    SDL_UnlockMutex(p->mutex);return ok;
}
int steam_provider_unlock_session(struct steam_provider *p,const char *pass)
{
    if(!steam_session_passphrase_valid(pass)||!p->root[0]||!steam_session_present(p->root))return 0;
    disconnect(p);p->restore=1;strcpy(p->vault_pass,pass);SDL_AtomicSet(&p->cancel,0);
    state(p,STEAM_CONNECTING,0,0,0);p->thread=SDL_CreateThreadWithStackSize(run,"steam-session",256u*1024u,p);
    if(!p->thread){steam_secret_clear(p->vault_pass,sizeof(p->vault_pass));state(p,STEAM_ERROR,5,0,0);return 0;}return 1;
}
int steam_provider_save_automatic_session(struct steam_provider *p)
{
    int ok;SDL_LockMutex(p->mutex);
    ok=p->view.state==STEAM_ONLINE&&p->root[0]&&!p->vault.saving&&!p->save_session;
    if(ok){p->save_session=2;p->vault.saving=1;p->vault.error=0;}
    SDL_UnlockMutex(p->mutex);return ok;
}
int steam_provider_resume_session(struct steam_provider *p)
{
    if(!p->root[0]||!steam_session_automatic(p->root))return 0;
    disconnect(p);p->restore=2;SDL_AtomicSet(&p->cancel,0);
    state(p,STEAM_CONNECTING,0,0,0);
    p->thread=SDL_CreateThreadWithStackSize(run,"steam-session",256u*1024u,p);
    if(!p->thread){state(p,STEAM_ERROR,5,0,0);return 0;}
    return 1;
}

int steam_provider_inspect(struct steam_provider *p, uint32_t app) {
  size_t i;
  int ok = 0;
  SDL_LockMutex(p->mutex);
  if (p->view.state == STEAM_ONLINE && p->app_view.app_id != app) {
    for (i = 0; i < p->library.count; ++i)
      if (p->library.games[i].app_id == app) {
        ok = 1;
        break;
      }
    if (ok) {
      p->inspect_app = app;
      p->app_view = (struct steam_app_view){.app_id = app, .state = 1};
    }
  }
  SDL_UnlockMutex(p->mutex);
  return ok;
}
void steam_provider_app_status(struct steam_provider *p,
                               struct steam_app_view *out) {
  SDL_LockMutex(p->mutex);
  *out = p->app_view;
  SDL_UnlockMutex(p->mutex);
}
int steam_provider_uninstall(struct steam_provider *p, uint32_t app) {
  size_t i;
  int ok = 0;
  char directory[512];
  if (!steam_storage_directory(p->root, app, directory) &&
      !steam_storage_pending(p->root, app, directory)) {
    SDL_LockMutex(p->mutex);
    if (p->remove_view.app_id != app || !p->remove_view.error) {
      SDL_UnlockMutex(p->mutex);
      return 0;
    }
    strcpy(directory, p->remove_view.directory);
    SDL_UnlockMutex(p->mutex);
  }
  SDL_LockMutex(p->mutex);
  if (p->view.state == STEAM_ONLINE && !p->remove_view.busy &&
      !p->install_app &&
      !steam_download_active(p->download.state)) {
    for (i = 0; i < p->library.count; ++i)
      if (p->library.games[i].app_id == app && p->library.games[i].installed) {
        ok = 1;
        break;
      }
    if (ok) {
      p->remove_app = app;
      p->remove_view = (struct steam_remove_view){
          .app_id = app,
          .busy = 1,
          .generation = p->remove_view.generation + 1};
      strcpy(p->remove_view.directory, directory);
    }
  }
  SDL_UnlockMutex(p->mutex);
  return ok;
}
/* The shell removed the game's files itself: the library stops listing it as
 * installed, and what is remembered of its download goes. Returns 0 while the
 * provider is itself working on that game. */
int steam_provider_forget_install(struct steam_provider *p, uint32_t app) {
  size_t i;
  int ok;
  SDL_LockMutex(p->mutex);
  ok = !p->remove_view.busy && p->install_app != app &&
       !(p->download.app_id == app && steam_download_active(p->download.state));
  if (ok) {
    for (i = 0; i < p->library.count; ++i)
      if (p->library.games[i].app_id == app)
        p->library.games[i].installed = 0;
    p->library_view.generation++;
    if (p->download.app_id == app)
      memset(&p->download, 0, sizeof(p->download));
  }
  SDL_UnlockMutex(p->mutex);
  return ok;
}
/* The shell found an installation the library did not know of. */
void steam_provider_note_install(struct steam_provider *p, uint32_t app) {
  size_t i;
  SDL_LockMutex(p->mutex);
  for (i = 0; i < p->library.count; ++i)
    if (p->library.games[i].app_id == app && !p->library.games[i].installed) {
      p->library.games[i].installed = 1;
      p->library_view.generation++;
    }
  SDL_UnlockMutex(p->mutex);
}
/* Whether the provider is downloading, installing or removing right now. */
int steam_provider_busy(struct steam_provider *p, uint32_t app) {
  int busy;
  SDL_LockMutex(p->mutex);
  busy = p->remove_view.busy || (app && p->install_app == app) ||
         (p->download.app_id == app && steam_download_active(p->download.state));
  SDL_UnlockMutex(p->mutex);
  return busy;
}
void steam_provider_remove_status(struct steam_provider *p,
                                  struct steam_remove_view *out) {
  SDL_LockMutex(p->mutex);
  *out = p->remove_view;
  SDL_UnlockMutex(p->mutex);
}
