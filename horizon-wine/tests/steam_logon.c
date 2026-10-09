/* Offline provider logon fixtures; no real credentials or connection. */
#define _POSIX_C_SOURCE 200809L
#include "../source/steam/steam_provider.c"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
int steam_backend_app(struct steam_backend *b,uint32_t app,struct steam_app *out){(void)b;(void)app;(void)out;return 0;}
int steam_storage_pending(const char *r,uint32_t app,char out[512]){(void)r;(void)app;(void)out;return 0;}
int steam_storage_directory(const char *r,uint32_t app,char out[512]){(void)r;(void)app;(void)out;return 0;}
unsigned int steam_storage_uninstall(const char *r,uint32_t app,int (*t)(void *),void *c){(void)r;(void)app;(void)t;(void)c;return 3104;}
static unsigned char response[64];static size_t response_size;static uint64_t identity;static uint32_t sid;
struct steam_cm *steam_cm_connect(steam_cancel_fn f,void *p,unsigned int *e){(void)f;(void)p;(void)e;return NULL;}
unsigned int steam_cm_connect_transport(void){return 0;}
void steam_cm_close(struct steam_cm *c){(void)c;}
unsigned int steam_cm_error(const struct steam_cm *c){(void)c;return 6;}
unsigned int steam_cm_last_type(const struct steam_cm *c){(void)c;return 782;}
int steam_cm_tick(struct steam_cm *c){(void)c;return 0;}
int steam_cm_call(struct steam_cm *c,enum steam_service t,uint64_t id,uint32_t s,uint64_t j,const void *b,size_t n,struct steam_cm_message *m)
{(void)c;(void)t;(void)id;(void)s;(void)j;(void)b;(void)n;(void)m;return 0;}
int steam_install_run(struct steam_backend *b,const char *r,uint32_t a,const struct steam_install_callbacks *c)
{(void)b;(void)r;(void)a;(void)c;return 0;}
static unsigned int discards;
void steam_install_discard(const char *r,struct steam_download_view *v,const struct steam_install_callbacks *c)
{(void)r;++discards;v->state=STEAM_DOWNLOAD_DISCARDED;v->error=v->cleanup_errno=0;if(c->progress)c->progress(c->context,v);}
unsigned int steam_download_diagnostic(const char *r,const struct steam_download_view *v)
{(void)r;(void)v;return 0;}
static unsigned int failures_logged;
void steam_download_failure_log(const char *r,const struct steam_download_view *v,const char *o)
{(void)r;(void)v;(void)o;++failures_logged;}
int steam_install_present(const char *r,uint32_t a){(void)r;(void)a;return 0;}
int steam_cm_send(struct steam_cm *c,uint32_t t,uint64_t id,uint32_t s,const void *b,size_t n)
{(void)c;(void)id;(void)s;(void)b;(void)n;assert(t==5514);return 1;}
int steam_cm_exchange(struct steam_cm *c,const void *b,size_t n,uint64_t j,uint32_t t,struct steam_cm_message *m)
{(void)c;(void)b;(void)n;(void)j;assert(t==751);*m=(struct steam_cm_message){.steam_id=identity,.session_id=sid,.body=response,.body_size=response_size};return 1;}
int main(void)
{
    assert(retry_transport(0,2)&&retry_transport(0,6)&&retry_transport(206,0));
    assert(!retry_transport(1005,2)&&!retry_transport(1065,6)&&!retry_transport(11,2));
    assert(!retry_transport(0,4)&&!retry_transport(0,3)&&!retry_transport(3403,0));
    struct steam_auth a={0};unsigned int error=999,hb=0;uint32_t session=0,cell=999;
    struct steam_wire_writer w={response,sizeof(response),0,0};
    a.steam_id=identity=UINT64_C(76561198000000000);sid=42;
    strcpy(a.refresh_token,"synthetic-token");strcpy(a.account_name,"synthetic-account");
    steam_wire_uint(&w,1,1);steam_wire_uint(&w,2,9);steam_wire_uint(&w,3,30);response_size=w.size;
    assert(logon(NULL,&a,&session,&cell,&hb,&error));assert(!error&&session==42&&hb==9&&cell==0);
    steam_wire_uint(&w,7,47);response_size=w.size;
    assert(logon(NULL,&a,&session,&cell,&hb,&error)&&cell==47);
    {size_t good=w.size;steam_wire_uint(&w,7,48);response_size=w.size;
     assert(!logon(NULL,&a,&session,&cell,&hb,&error)&&error==11);w.size=good;response_size=good;}
    identity++;assert(!logon(NULL,&a,&session,&cell,&hb,&error)&&error==12);identity--;
    sid=0;assert(!logon(NULL,&a,&session,&cell,&hb,&error)&&error==13);sid=42;
    response_size=0;assert(!logon(NULL,&a,&session,&cell,&hb,&error)&&error==11);
    w=(struct steam_wire_writer){response,sizeof(response),0,0};steam_wire_uint(&w,1,5);response_size=w.size;
    assert(!logon(NULL,&a,&session,&cell,&hb,&error)&&error==1005);
    {struct steam_provider *p=steam_provider_create();assert(p);assert(steam_provider_set_root(p,"/tmp"));assert(!steam_provider_install(p,123));
     state(p,STEAM_ONLINE,0,0,a.steam_id);assert(!steam_provider_install(p,123));
     p->library.games=calloc(1,sizeof(*p->library.games));assert(p->library.games);p->library.count=1;p->library.games[0].app_id=123;
     assert(!steam_provider_inspect(p,456));assert(steam_provider_inspect(p,123));assert(p->inspect_app==123&&p->app_view.state==1);assert(!steam_provider_inspect(p,123));
     assert(!steam_provider_install(p,456));assert(steam_provider_install(p,123));assert(!steam_provider_install(p,123));
     assert(!strcmp(p->install_language,p->language));
     p->install_app=0;p->download.state=STEAM_DOWNLOAD_ERROR;
     assert(!steam_provider_install_language(p,123,"../invalid"));
     assert(steam_provider_install_language(p,123,"russian"));
     assert(!strcmp(p->install_language,"russian")&&!strcmp(p->language,"english"));
     p->install_app=0;p->download.state=STEAM_DOWNLOAD_CANCELLED;
     assert(steam_provider_install(p,123)&&!strcmp(p->install_language,"russian"));
     p->install_app=0;p->download.state=STEAM_DOWNLOAD_ERROR;
     assert(steam_provider_install_language(p,123,STEAM_LANGUAGE_BASE_ID));
     assert(!strcmp(p->install_language,STEAM_LANGUAGE_BASE_ID)&&!strcmp(p->language,"english"));
     /* No Cancel: disconnect must retain resumable data. */
     steam_provider_logout(p);assert(!p->library.count&&!p->install_app&&!discards);steam_provider_destroy(p);}
    {struct steam_provider *p=steam_provider_create();assert(p&&steam_provider_set_root(p,"/tmp"));
     state(p,STEAM_ONLINE,0,0,a.steam_id);
     p->download=(struct steam_download_view){.app_id=123,.state=STEAM_DOWNLOAD_PLANNING};p->install_app=123;
     steam_provider_cancel_download(p);assert(SDL_AtomicGet(&p->download_cancel));
     steam_provider_logout(p);assert(discards==1&&!p->install_app);steam_provider_destroy(p);}
    {struct steam_provider *p=steam_provider_create();assert(p&&steam_provider_set_root(p,"/tmp"));
     p->download=(struct steam_download_view){.app_id=123,.state=STEAM_DOWNLOAD_ERROR,.error=3106};
     steam_provider_cancel_download(p);assert(p->thread);SDL_WaitThread(p->thread,NULL);p->thread=NULL;
     assert(p->download.state==STEAM_DOWNLOAD_DISCARDED&&discards==2);
     p->download=(struct steam_download_view){.app_id=123,.state=STEAM_DOWNLOAD_ERROR,.error=3112};
     steam_provider_cancel_download(p);assert(p->thread);SDL_WaitThread(p->thread,NULL);p->thread=NULL;
     assert(p->download.state==STEAM_DOWNLOAD_DISCARDED&&discards==3);
     p->download.state=STEAM_DOWNLOAD_COMMITTING;SDL_AtomicSet(&p->download_cancel,0);steam_provider_cancel_download(p);
     assert(!SDL_AtomicGet(&p->download_cancel));p->download.state=STEAM_DOWNLOAD_COMPLETE;steam_provider_cancel_download(p);
     assert(!SDL_AtomicGet(&p->download_cancel));steam_provider_destroy(p);assert(discards==3);}
    /* Starting over: the files go first, then the same download begins in the language it had. */
    {struct steam_provider *p=steam_provider_create();assert(p&&steam_provider_set_root(p,"/tmp"));
     state(p,STEAM_ONLINE,0,0,a.steam_id);
     p->library.games=calloc(1,sizeof(*p->library.games));assert(p->library.games);p->library.count=1;p->library.games[0].app_id=123;
     assert(!steam_provider_restart_download(p)&&!steam_provider_restart_poll(p));
     p->download=(struct steam_download_view){.app_id=123,.state=STEAM_DOWNLOAD_ERROR,.error=3110,.attempts=2,.stuck=1};
     strcpy(p->download.language,"russian");p->fail_app=123;p->fail_count=2;
     assert(steam_provider_restart_download(p)&&failures_logged==1);
     assert(p->download.state==STEAM_DOWNLOAD_CLEANING&&!p->download.stuck&&!p->fail_count&&p->restart_app==123);
     assert(!steam_provider_restart_poll(p)&&!p->install_app);
     finish_discard(p);assert(p->download.state==STEAM_DOWNLOAD_DISCARDED&&discards==4);
     assert(steam_provider_restart_poll(p)==1&&p->install_app==123&&!p->restart_app);
     assert(!strcmp(p->install_language,"russian")&&p->download.state==STEAM_DOWNLOAD_PLANNING);
     assert(!steam_provider_restart_poll(p));
     /* Cleaning failed: nothing is started. */
     p->install_app=0;p->download=(struct steam_download_view){.app_id=123,.state=STEAM_DOWNLOAD_ERROR,.error=3106};
     assert(steam_provider_restart_download(p));
     p->download.state=STEAM_DOWNLOAD_ERROR;p->download.error=3112;p->download.failed_stage=STEAM_DOWNLOAD_CLEANING;
     assert(steam_provider_restart_poll(p)==-1&&!p->install_app&&!p->restart_app);
     SDL_AtomicSet(&p->download_cancel,0);p->download=(struct steam_download_view){0};steam_provider_destroy(p);}
    {char root[]="/tmp/steam-provider-vault-XXXXXX";struct steam_provider *p;struct steam_session_view v;
     strcpy(a.refresh_token,"e30.eyJzdWIiOiI3NjU2MTE5ODAwMDAwMDAwMCJ9.signature");assert(mkdtemp(root));
     assert(!steam_session_save(root,"synthetic local vault passphrase",&a));p=steam_provider_create();assert(p&&steam_provider_set_root(p,root));
     steam_provider_session_status(p,&v);assert(v.present);steam_provider_destroy(p);assert(steam_session_present(root));
     p=steam_provider_create();assert(p&&steam_provider_set_root(p,root));steam_provider_logout(p);assert(!steam_session_present(root));steam_provider_destroy(p);assert(!rmdir(root));}
    {char root[]="/tmp/steam-auto-invalid-XXXXXX",path[768];struct steam_provider *p;struct steam_status_view status;FILE *f;
     assert(mkdtemp(root));snprintf(path,sizeof(path),"%s/steam-session.vault",root);
     f=fopen(path,"wb");assert(f&&fwrite("ARSTEAM\2",1,8,f)==8&&!fclose(f));
     p=steam_provider_create();assert(p&&steam_provider_set_root(p,root)&&p->thread);
     SDL_WaitThread(p->thread,NULL);p->thread=NULL;steam_provider_status(p,&status);
     assert(status.state==STEAM_ERROR&&status.error==3402&&!status.steam_id);
     steam_provider_destroy(p);assert(steam_session_present(root));
     p=steam_provider_create();assert(p&&steam_provider_set_root(p,root));steam_provider_logout(p);
     assert(!steam_session_present(root));steam_provider_destroy(p);assert(!rmdir(root));}
    steam_auth_clear(&a);puts("CM identity/session, heartbeat, retry gates, automatic startup failure and shutdown/logout vault lifetime tests passed");return 0;
}
