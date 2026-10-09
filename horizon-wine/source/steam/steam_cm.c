#define _POSIX_C_SOURCE 200809L
#include "steam_cm.h"
#include "steam_crypto.h"
#include <curl/curl.h>
#include <wslay/wslay.h>
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>
#include <zlib.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

struct steam_cm {
    CURL *curl;
    wslay_event_context_ptr ws;
    steam_cancel_fn cancel;
    void *context;
    unsigned int error;
    unsigned int last_type;
    uint64_t wanted_job;
    uint32_t wanted_type;
    unsigned char *reply;
    size_t reply_size;
};
static uint64_t now_ms(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static void pause_io(void)
{ struct timespec t={0,10000000}; nanosleep(&t,NULL); }
static int alive(struct steam_cm *c)
{ if (c->cancel && c->cancel(c->context)) c->error=1; return !c->error; }
unsigned int steam_cm_error(const struct steam_cm *c) { return c->error; }
unsigned int steam_cm_last_type(const struct steam_cm *c) { return c?c->last_type:0; }
static ssize_t ws_recv(wslay_event_context_ptr ctx,uint8_t *data,size_t size,int flags,void *opaque)
{
    struct steam_cm *c=opaque; size_t got=0; CURLcode r;
    (void)flags;
    r=curl_easy_recv(c->curl,data,size,&got);
    if (r==CURLE_AGAIN) { wslay_event_set_error(ctx,WSLAY_ERR_WOULDBLOCK); return -1; }
    if (r || !got) { c->error=2; wslay_event_set_error(ctx,WSLAY_ERR_CALLBACK_FAILURE); return -1; }
    return (ssize_t)got;
}
static ssize_t ws_send(wslay_event_context_ptr ctx,const uint8_t *data,size_t size,int flags,void *opaque)
{
    struct steam_cm *c=opaque; size_t sent=0; CURLcode r;
    (void)flags;
    r=curl_easy_send(c->curl,data,size,&sent);
    if (r==CURLE_AGAIN) { wslay_event_set_error(ctx,WSLAY_ERR_WOULDBLOCK); return -1; }
    if (r || !sent) { c->error=2; wslay_event_set_error(ctx,WSLAY_ERR_CALLBACK_FAILURE); return -1; }
    return (ssize_t)sent;
}
static int mask(wslay_event_context_ptr ctx,uint8_t *data,size_t size,void *opaque)
{ (void)ctx; (void)opaque; return steam_random(data,size)?0:-1; }
static uint32_t u32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(unsigned char *p,uint32_t n)
{ unsigned int i; for(i=0;i<4;i++) p[i]=(unsigned char)(n>>(8*i)); }
static void packet(struct steam_cm *c,const unsigned char *data,size_t size,unsigned int depth)
{
    struct steam_cm_message m;
    if(size<4 || size>STEAM_CM_PACKET_MAX || depth>2){c->error=3;return;}
    c->last_type=u32(data)&0x7fffffffu;
    if(!(u32(data)&0x80000000u)){
        /* Steam also sends legacy notifications (e.g. VAC/session-token).
         * They cannot satisfy authentication jobs or prove a successful logon. */
        if(size<20 || c->last_type==1 || c->last_type==751){c->error=3;return;}
        if(c->last_type==757)c->error=4;
        return;
    }
    if (!steam_cm_message_read(data,size,&m) || depth>2) { c->error=3; return; }
    if (m.type==757) { c->error=4; return; }
    if (m.type==1) {
        struct steam_wire_reader r={m.body,m.body_size,0,0};
        struct steam_wire_field f; int next; unsigned int seen=0;
        uint64_t expanded=0; const unsigned char *body=NULL; size_t length=0,offset=0;
        unsigned char *inflated=NULL;
        while((next=steam_wire_next(&r,&f))==1) {
            if(f.number==1) { if((seen&1)||f.type!=0) {c->error=3;return;} expanded=f.integer;seen|=1; }
            if(f.number==2) { if((seen&2)||f.type!=2) {c->error=3;return;} body=f.data;length=f.size;seen|=2; }
        }
        if(next<0 || !body || expanded>STEAM_CM_PACKET_MAX) {c->error=3;return;}
        if(expanded) {
            z_stream z={0}; int status;
            inflated=malloc((size_t)expanded);
            if(!inflated) {c->error=5;return;}
            z.next_in=(unsigned char *)body; z.avail_in=(uInt)length;
            z.next_out=inflated; z.avail_out=(uInt)expanded;
            if(inflateInit2(&z,31)!=Z_OK) {free(inflated);c->error=3;return;}
            status=inflate(&z,Z_FINISH);
            if(status!=Z_STREAM_END || z.total_out!=expanded || z.avail_in) c->error=3;
            inflateEnd(&z); body=inflated; length=(size_t)expanded;
        }
        while(!c->error && offset<length) {
            uint32_t n;
            if(length-offset<4) {c->error=3;break;}
            n=u32(body+offset);offset+=4;
            if(n<8 || n>length-offset) {c->error=3;break;}
            packet(c,body+offset,n,depth+1);offset+=n;
        }
        if(inflated) {steam_secret_clear(inflated,length);free(inflated);}
        return;
    }
    if ((c->wanted_job && m.target_job==c->wanted_job) || (c->wanted_type && m.type==c->wanted_type)) {
        if(c->reply_size || size>STEAM_CM_PACKET_MAX) {c->error=3;return;}
        c->reply=malloc(size);if(!c->reply){c->error=5;return;}
        memcpy(c->reply,data,size);c->reply_size=size;
    } else if(m.type==751) c->error=4;
}
static void message(wslay_event_context_ptr ctx,const struct wslay_event_on_msg_recv_arg *arg,void *opaque)
{
    struct steam_cm *c=opaque; (void)ctx;
    if(arg->opcode==WSLAY_BINARY_FRAME) packet(c,arg->msg,arg->msg_length,0);
    else if(arg->opcode==WSLAY_CONNECTION_CLOSE || arg->opcode==WSLAY_TEXT_FRAME) c->error=2;
}
int steam_cm_tick(struct steam_cm *c)
{
    if(!alive(c)) return 0;
    if(wslay_event_send(c->ws) || wslay_event_recv(c->ws)) {if(!c->error)c->error=2;}
    return alive(c);
}
static int flush(struct steam_cm *c)
{
    uint64_t deadline=now_ms()+15000;
    while(alive(c) && wslay_event_want_write(c->ws)) {
        if(now_ms()>deadline || wslay_event_send(c->ws)) {c->error=2;break;}
        if(wslay_event_want_write(c->ws)) pause_io();
    }
    return alive(c);
}
static int send_packet(struct steam_cm *c,const void *data,size_t size)
{
    struct wslay_event_msg m={WSLAY_BINARY_FRAME,data,size};
    if(size>STEAM_CM_PACKET_MAX || wslay_event_queue_msg(c->ws,&m)) {c->error=5;return 0;}
    return flush(c);
}
int steam_cm_send(struct steam_cm *c,uint32_t type,uint64_t steamid,uint32_t session,const void *body,size_t size)
{
    unsigned char data[8192],header[64];
    struct steam_wire_writer w={header,sizeof(header),0,0};
    int ok;
    steam_wire_fixed64(&w,1,steamid);steam_wire_uint(&w,2,session);
    if(w.failed || size>sizeof(data)-8-w.size) return 0;
    put32(data,type|0x80000000u);put32(data+4,(uint32_t)w.size);
    memcpy(data+8,header,w.size);if(size)memcpy(data+8+w.size,body,size);
    ok=send_packet(c,data,8+w.size+size);steam_secret_clear(data,sizeof(data));return ok;
}
int steam_cm_exchange(struct steam_cm *c,const void *data,size_t size,uint64_t job,uint32_t type,struct steam_cm_message *reply)
{
    uint64_t deadline=now_ms()+20000;
    steam_secret_clear(c->reply,c->reply_size);free(c->reply);c->reply=NULL;c->reply_size=0;c->wanted_job=job;c->wanted_type=type;
    if(size && !send_packet(c,data,size)) return 0;
    while(!c->reply_size && steam_cm_tick(c)) {
        if(now_ms()>deadline) {c->error=6;break;} pause_io();
    }
    c->wanted_job=0;c->wanted_type=0;
    return alive(c) && c->reply_size && steam_cm_message_read(c->reply,c->reply_size,reply);
}
void steam_cm_close(struct steam_cm *c)
{
    if(!c)return;
    if(c->ws)wslay_event_context_free(c->ws);
    if(c->curl)curl_easy_cleanup(c->curl);
    steam_secret_clear(c->reply,c->reply_size);free(c->reply);
    steam_secret_clear(c,sizeof(*c));free(c);
}
static int request(struct steam_cm *c,uint32_t type,const char *method,uint64_t id,uint32_t session,uint64_t job,const void *body,size_t size,struct steam_cm_message *reply)
{
    unsigned char header[256],data[8192];struct steam_wire_writer w={header,sizeof(header),0,0};int ok;
    if(!id||!session||!job||job==UINT64_MAX)return 0;
    steam_wire_fixed64(&w,1,id);steam_wire_uint(&w,2,session);steam_wire_fixed64(&w,10,job);
    if(method)steam_wire_bytes(&w,12,method,strlen(method));
    if(w.failed||size>sizeof(data)-8-w.size)return 0;
    put32(data,type|0x80000000u);put32(data+4,(uint32_t)w.size);memcpy(data+8,header,w.size);
    if(size)memcpy(data+8+w.size,body,size);
    ok=steam_cm_exchange(c,data,8+w.size+size,job,0,reply);steam_secret_clear(data,sizeof(data));return ok;
}
int steam_cm_call(struct steam_cm *c,enum steam_service method,uint64_t id,uint32_t session,uint64_t job,const void *body,size_t size,struct steam_cm_message *reply)
{
    static const char *const names[]={"Player.GetOwnedGames#1","ContentServerDirectory.GetManifestRequestCode#1","ContentServerDirectory.GetServersForSteamPipe#1","ContentServerDirectory.GetCDNAuthToken#1","FamilyGroups.GetFamilyGroupForUser#1","FamilyGroups.GetSharedLibraryApps#1"};
    if((unsigned int)method>=sizeof(names)/sizeof(names[0]))return 0;
    return request(c,151,names[method],id,session,job,body,size,reply);
}
int steam_cm_request(struct steam_cm *c,uint32_t type,uint64_t id,uint32_t session,uint64_t job,const void *body,size_t size,struct steam_cm_message *reply)
{
    if(type!=8903&&type!=8905&&type!=5438)return 0;
    return request(c,type,NULL,id,session,job,body,size,reply);
}
static int progress(void *p,curl_off_t a,curl_off_t b,curl_off_t d,curl_off_t e)
{(void)a;(void)b;(void)d;(void)e;return !alive(p);}
static int raw_send(struct steam_cm *c,const char *data,size_t size,uint64_t deadline)
{
    while(size && alive(c)) {
        size_t n=0;CURLcode r=curl_easy_send(c->curl,data,size,&n);
        if(now_ms()>deadline || (r && r!=CURLE_AGAIN)) return 0;
        data+=n;size-=n;if(!n)pause_io();
    }
    return alive(c);
}
static int upgrade(struct steam_cm *c,const char *endpoint)
{
    unsigned char random[16],hash[20];size_t n;
    char key[32],expected[40],joined[80],request[512],headers[8192];
    char *line,*end;size_t used=0;unsigned int found=0;
    uint64_t deadline=now_ms()+15000;
    if(!steam_random(random,sizeof(random)))return 0;
    if(mbedtls_base64_encode((unsigned char *)key,sizeof(key),&n,random,sizeof(random)))return 0;
    snprintf(joined,sizeof(joined),"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",key);
    if(mbedtls_sha1_ret((unsigned char *)joined,strlen(joined),hash))return 0;
    if(mbedtls_base64_encode((unsigned char *)expected,sizeof(expected),&n,hash,sizeof(hash)))return 0;
    snprintf(request,sizeof(request),"GET /cmsocket/ HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",endpoint,key);
    if(!raw_send(c,request,strlen(request),deadline))return 0;
    /* Read just headers; retain no unconsumed frame data in curl's caller buffer. */
    while(used<sizeof(headers)-1 && alive(c) && now_ms()<deadline) {
        CURLcode r=curl_easy_recv(c->curl,headers+used,1,&n);
        if(r==CURLE_AGAIN){pause_io();continue;}
        if(r || !n)return 0;
        used+=n;headers[used]=0;
        if(used>=4 && !memcmp(headers+used-4,"\r\n\r\n",4))break;
    }
    if(used<4 || strcmp(headers+used-4,"\r\n\r\n") || strncmp(headers,"HTTP/1.1 101 ",13))return 0;
    line=strstr(headers,"\r\n")+2;
    while((end=strstr(line,"\r\n")) && end!=line) {
        char *colon;*end=0;colon=strchr(line,':');if(!colon)return 0;*colon++=0;
        while(*colon==' '||*colon=='\t')++colon;
        if(!strcasecmp(line,"Sec-WebSocket-Accept")){if((found&1)||strcmp(colon,expected))return 0;found|=1;}
        if(!strcasecmp(line,"Upgrade")){if((found&2)||strcasecmp(colon,"websocket"))return 0;found|=2;}
        if(!strcasecmp(line,"Connection")){if((found&4)||strcasecmp(colon,"Upgrade"))return 0;found|=4;}
        if(!strcasecmp(line,"Sec-WebSocket-Extensions")||!strcasecmp(line,"Sec-WebSocket-Protocol"))return 0;
        line=end+2;
    }
    return found==7;
}
static unsigned int discover_transport;
unsigned int steam_cm_connect_transport(void){return discover_transport;}
struct steam_cm *steam_cm_connect(steam_cancel_fn cancel,void *context,unsigned int *error)
{
    struct steam_servers servers;struct steam_probe_result result;unsigned int i;
    const struct wslay_event_callbacks cb={.recv_callback=ws_recv,.send_callback=ws_send,.genmask_callback=mask,.on_msg_recv_callback=message};
    /* The console's network may come up a moment after the launcher does:
     * only an unreachable directory is asked again, twice, a little later. */
    for(i=0;;++i){
        uint64_t until;
        steam_discover(&result,&servers,cancel,context);
        discover_transport=result.transport_code;
        if(result.code!=STEAM_NETWORK||i==2)break;
        for(until=now_ms()+1500*(i+1);now_ms()<until&&!(cancel&&cancel(context));)pause_io();
    }
    if(result.code!=STEAM_OK){*error=100+(unsigned int)result.code;return NULL;}
    for(i=0;i<servers.count && i<3;++i) {
        struct steam_cm *c=calloc(1,sizeof(*c));char url[192];CURLcode rc;
        if(!c){*error=5;return NULL;}
        c->cancel=cancel;c->context=context;c->curl=curl_easy_init();
        if(!c->curl){steam_cm_close(c);*error=5;return NULL;}
        snprintf(url,sizeof(url),"https://%s/cmsocket/",servers.endpoint[i]);
#define OPT(k,v) do { if(curl_easy_setopt(c->curl,k,v)) {c->error=7;goto failed;} }while(0)
        OPT(CURLOPT_URL,url);OPT(CURLOPT_PROXY,"");OPT(CURLOPT_NETRC,(long)CURL_NETRC_IGNORED);
        OPT(CURLOPT_CONNECT_ONLY,1L);OPT(CURLOPT_SSL_VERIFYPEER,1L);OPT(CURLOPT_SSL_VERIFYHOST,2L);
        OPT(CURLOPT_PROTOCOLS,(long)CURLPROTO_HTTPS);OPT(CURLOPT_FOLLOWLOCATION,0L);
        OPT(CURLOPT_HTTP_VERSION,(long)CURL_HTTP_VERSION_1_1);
        OPT(CURLOPT_CONNECTTIMEOUT,10L);OPT(CURLOPT_TIMEOUT,15L);OPT(CURLOPT_NOSIGNAL,1L);
        OPT(CURLOPT_NOPROGRESS,0L);OPT(CURLOPT_XFERINFOFUNCTION,progress);OPT(CURLOPT_XFERINFODATA,c);
        rc=curl_easy_perform(c->curl);
        if(rc){c->error=200+(unsigned int)rc;goto failed;}
        if(!upgrade(c,servers.endpoint[i])){c->error=8;goto failed;}
        if(wslay_event_context_client_init(&c->ws,&cb,c)){c->error=5;goto failed;}
        wslay_event_config_set_max_recv_msg_length(c->ws,STEAM_CM_PACKET_MAX);
        {
            unsigned char body[8];struct steam_wire_writer w={body,sizeof(body),0,0};
            steam_wire_uint(&w,1,65580);
            if(!steam_cm_send(c,9805,0,0,body,w.size))goto failed;
        }
        *error=0;return c;
failed:
        *error=c->error;steam_cm_close(c);
        if(cancel && cancel(context)){*error=1;break;}
#undef OPT
    }
    return NULL;
}
