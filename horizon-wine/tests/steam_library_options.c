#define _POSIX_C_SOURCE 200809L
#include "../source/steam/steam_family.h"
#include "../source/steam/steam_settings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned char reply[2048];static size_t reply_size;static int denied;
unsigned int steam_cm_error(const struct steam_cm *c){(void)c;return 6;}
int steam_cm_call(struct steam_cm *c,enum steam_service method,uint64_t id,uint32_t session,uint64_t job,const void *body,size_t length,struct steam_cm_message *m){
    struct steam_wire_reader r={body,length,0,0};struct steam_wire_field f;
    (void)c;(void)job;assert(id==99&&session==42);
    *m=(struct steam_cm_message){.type=147,.result=denied?15:1};
    if(method==STEAM_SERVICE_FAMILY_GROUP){
        static unsigned char group[]={8,123};m->body=group;m->body_size=sizeof(group);
        assert(steam_wire_next(&r,&f)==1&&f.number==1&&f.type==0&&f.integer==99);
    }else{
        assert(method==STEAM_SERVICE_FAMILY_LIBRARY);
        assert(steam_wire_next(&r,&f)==1&&f.number==1&&f.type==1&&f.integer==123);
        assert(steam_wire_next(&r,&f)==1&&f.number==2&&f.integer==0);
        assert(steam_wire_next(&r,&f)==1&&f.number==3&&f.integer==0);
        assert(steam_wire_next(&r,&f)==1&&f.number==5&&f.size==7&&!memcmp(f.data,"russian",7));
        m->body=reply;m->body_size=reply_size;
    }
    return 1;
}
static void add(struct steam_wire_writer *w,unsigned id,unsigned reason,int owner){
    unsigned char app[256],owners[16]={42};struct steam_wire_writer a={app,sizeof(app),0,0};
    steam_wire_uint(&a,1,id);steam_wire_bytes(&a,6,"Shared fixture",14);
    if(owner)steam_wire_bytes(&a,2,owners,sizeof(owners));
    steam_wire_uint(&a,10,reason);steam_wire_bytes(w,1,app,a.size);
}
static void family(void){
    struct steam_backend b={.account=99,.session=42,.language="russian"};
    struct steam_library l={.account=99,.count=1,.games=calloc(1,sizeof(struct steam_game))};
    struct steam_wire_writer w={reply,sizeof(reply),0,0};
    assert(l.games);l.games[0].app_id=123;strcpy(l.games[0].name,"Owned fixture");
    add(&w,123,0,1);add(&w,456,0,1);add(&w,789,3,1);add(&w,900,0,0);reply_size=w.size;
    assert(!steam_family_refresh(&b,&l)&&l.count==2);
    assert(l.games[0].app_id==123&&!l.games[0].shared);
    assert(l.games[1].app_id==456&&l.games[1].shared&&l.games[1].lender==42);
    denied=1;assert(steam_family_refresh(&b,&l)==1015&&l.count==2);denied=0;
    reply_size--;assert(steam_family_refresh(&b,&l)==3301&&l.count==2);reply_size++;
    add(&w,456,0,1);reply_size=w.size;
    assert(steam_family_refresh(&b,&l)==3301&&l.count==2);
    steam_library_clear(&l);
}
static void languages(void){
    const char *kv="\"appinfo\" { \"common\" { \"oslist\" \"windows\" } \"depots\" { "
       "\"101\" { \"manifests\" { \"public\" { \"gid\" \"900\" } } } "
       "\"102\" { \"config\" { \"language\" \"english\" } \"manifests\" { \"public\" { \"gid\" \"901\" } } } "
       "\"103\" { \"config\" { \"language\" \"russian\" } \"manifests\" { \"public\" { \"gid\" \"902\" } } } } }";
    struct steam_app app;char root[]="/tmp/steam-settings-XXXXXX",lang[32],path[768];
    assert(steam_app_parse_language(kv,strlen(kv),100,"russian",&app)&&app.count==2&&app.depots[1].id==103&&!app.language_missing);
    assert(steam_app_parse_language(kv,strlen(kv),100,"english",&app)&&app.count==2&&app.depots[1].id==102);
    assert(steam_app_parse_language(kv,strlen(kv),100,"german",&app)&&app.language_missing);
    assert(app.available_languages==(steam_language_mask("english,russian")|STEAM_LANGUAGE_BASE));
    assert(steam_app_parse_language(kv,strlen(kv),100,STEAM_LANGUAGE_BASE_ID,&app)&&!app.language_missing&&app.count==1&&app.depots[0].id==101);
    assert(steam_language_mask(" English , RUSSIAN,unknown,english ")==steam_language_mask("english,russian"));
    {
      const char *mixed="\"appinfo\" { \"common\" { \"oslist\" \"windows\" } \"depots\" { "
        "\"101\" { \"config\" { \"language\" \"  \" } \"manifests\" { \"public\" { \"gid\" \"900\" } } } "
        "\"102\" { \"config\" { \"language\" \" English , Russian \" } \"manifests\" { \"public\" { \"gid\" \"901\" } } } "
        "\"103\" { \"config\" { \"oslist\" \"linux\" \"language\" \"german\" } \"manifests\" { \"public\" { \"gid\" \"902\" } } } "
        "\"104\" { \"dlcappid\" \"200\" \"config\" { \"language\" \"french\" } \"manifests\" { \"public\" { \"gid\" \"903\" } } } "
        "\"105\" { \"config\" { \"language\" \"italian\" } \"manifests\" { \"beta\" { \"gid\" \"904\" } } } } }";
      assert(steam_app_parse_language(mixed,strlen(mixed),100,"english",&app)&&!app.language_missing&&app.count==2);
      assert(app.available_languages==(steam_language_mask("english,russian")|STEAM_LANGUAGE_BASE));
    }
    {
      const char *only="\"appinfo\" { \"common\" { \"oslist\" \"windows\" } \"depots\" { "
        "\"101\" { \"config\" { \"language\" \"russian\" } \"manifests\" { \"public\" { \"gid\" \"900\" } } } } }";
      assert(steam_app_parse_language(only,strlen(only),100,"english",&app)&&app.language_missing&&!app.count);
      assert(app.available_languages==steam_language_mask("russian"));
      assert(steam_app_parse_language(only,strlen(only),100,STEAM_LANGUAGE_BASE_ID,&app)&&app.language_missing&&!app.count);
      assert(steam_app_parse_language(only,strlen(only),100,"russian",&app)&&!app.language_missing&&app.count==1);
    }
    assert(mkdtemp(root));assert(!steam_settings_load(root,lang)&&!strcmp(lang,"english"));
    assert(steam_settings_save(root,"russian")&&steam_settings_load(root,lang)&&!strcmp(lang,"russian"));
    assert(steam_settings_concurrency(root)==16&&!steam_settings_save_concurrency(root,3)&&steam_settings_save_concurrency(root,16)&&steam_settings_concurrency(root)==16&&steam_settings_save_concurrency(root,4));
    assert(steam_settings_concurrency(root)==4&&steam_settings_load(root,lang)&&!strcmp(lang,"russian"));
    assert(steam_settings_save(root,"german")&&steam_settings_concurrency(root)==4);
    assert(!steam_settings_save_concurrency(root,99));
    assert(!steam_settings_save(root,"../invalid"));
    assert(!steam_settings_save(root,STEAM_LANGUAGE_BASE_ID));
    assert(steam_settings_save(root,"system"));
    assert(steam_settings_load(root,lang)==2&&!strcmp(lang,"english"));
    assert(steam_settings_save_concurrency(root,2));
    assert(steam_settings_load(root,lang)==2&&steam_settings_concurrency(root)==2);
    snprintf(path,sizeof(path),"%s/steam-settings.txt",root);assert(!unlink(path)&&!rmdir(root));
}
int main(void){
    struct steam_app app;const char *kv="\"appinfo\" { \"common\" { \"extended\" { \"installscript\" \"scripts/setup.vdf\" } } }";
    assert(steam_app_parse(kv,strlen(kv),123,&app)&&!strcmp(app.installscript,"scripts/setup.vdf"));
    family();languages();puts("Steam Families merge/exclusion/identity and persisted language/depot selection passed");return 0;}
