/* Synthetic CM fixture: no network, accounts or credentials. */
#include "../source/steam/steam_backend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char reply[2048];
static size_t reply_size;
static uint32_t expected_cell;
int steam_cm_call(struct steam_cm *cm, enum steam_service type, uint64_t account,
 uint32_t session, uint64_t job, const void *data, size_t size, struct steam_cm_message *out) {
 struct steam_wire_reader r={data,size,0,0};struct steam_wire_field f;unsigned int seen=0;
 (void)cm;(void)account;(void)session;(void)job;assert(type==STEAM_SERVICE_SERVERS);
 while(steam_wire_next(&r,&f)==1){assert(f.type==0);
  if(f.number==1){assert(!(seen&1)&&f.integer==expected_cell);seen|=1;}
  else {assert(f.number==2&&f.integer==20&&!(seen&2));seen|=2;}}
 assert(!r.failed&&seen==3);
 *out=(struct steam_cm_message){.type=147,.result=1,.body=reply,.body_size=reply_size};return 1;
}
int steam_cm_request(struct steam_cm *cm,uint32_t type,uint64_t account,uint32_t session,
 uint64_t job,const void *data,size_t size,struct steam_cm_message *out){
 (void)cm;(void)type;(void)account;(void)session;(void)job;(void)data;(void)size;(void)out;return 0;}
unsigned int steam_cm_error(const struct steam_cm *cm){(void)cm;return 6;}
int main(void){
 unsigned char entry[200];char hosts[8][128];size_t count;
 struct steam_backend b={0};struct steam_wire_writer w={entry,sizeof(entry),0,0},r={reply,sizeof(reply),0,0};
 steam_wire_bytes(&w,1,"CDN",3);steam_wire_bytes(&w,8,"cache1.steamcontent.com",strlen("cache1.steamcontent.com"));
 steam_wire_bytes(&w,12,"mandatory",9);steam_wire_bytes(&r,1,entry,w.size);reply_size=r.size;
 expected_cell=47;b.cell_id=47;assert(steam_backend_servers(&b,hosts,&count));
 assert(count==1&&!strcmp(hosts[0],"cache1.steamcontent.com"));
 expected_cell=0;b.cell_id=0;assert(steam_backend_servers(&b,hosts,&count));
 /* Many regions are given Valve's own servers only, which are "SteamCache",
  * and some a contracted network under another domain: both are taken. A
  * server for Steam China, a proxy, one kept for some applications, one
  * reached under another name, another kind and a name that is no DNS name
  * are passed over without spoiling the list. */
 {
  static const struct { const char *type,*host,*https,*vhost; int field; } servers[]={
   {"SteamCache","Cache2-FRA1.steamcontent.com","mandatory","cache2-fra1.steamcontent.com",0},
   {"CDN","steampipe.akamaized.net","optional",NULL,0},
   {"CDN","china.example.com","mandatory",NULL,7},
   {"CDN","proxy.example.com","mandatory",NULL,10},
   {"CDN","apps.example.com","mandatory",NULL,13},
   {"CDN","10.0.0.7","mandatory","lancache.example.com",0},
   {"OpenCache","open.example.com","mandatory",NULL,0},
   {"CDN","bad_name.example.com","mandatory",NULL,0},
   {"CDN","nodots","mandatory",NULL,0},
   {"SteamCache","plain.steamcontent.com","unavailable",NULL,0}};
  static unsigned char big[2048];struct steam_wire_writer all={big,sizeof(big),0,0};size_t i;
  for(i=0;i<sizeof(servers)/sizeof(*servers);++i){
   w=(struct steam_wire_writer){entry,sizeof(entry),0,0};
   steam_wire_bytes(&w,1,servers[i].type,strlen(servers[i].type));steam_wire_bytes(&w,8,servers[i].host,strlen(servers[i].host));
   if(servers[i].vhost)steam_wire_bytes(&w,9,servers[i].vhost,strlen(servers[i].vhost));
   if(servers[i].field)steam_wire_uint(&w,servers[i].field,1);
   steam_wire_bytes(&w,12,servers[i].https,strlen(servers[i].https));steam_wire_bytes(&all,1,entry,w.size);}
  assert(!all.failed&&all.size<=sizeof(reply));memcpy(reply,big,all.size);reply_size=all.size;
  assert(steam_backend_servers(&b,hosts,&count)&&count==3);
  assert(!strcmp(hosts[0],"cache2-fra1.steamcontent.com")&&!strcmp(hosts[1],"steampipe.akamaized.net")&&!strcmp(hosts[2],"plain.steamcontent.com"));
  assert(b.tls_hosts==3&&b.plain_hosts==6);
  /* Nothing usable is still the error it was. */
  w=(struct steam_wire_writer){entry,sizeof(entry),0,0};r=(struct steam_wire_writer){reply,sizeof(reply),0,0};
  steam_wire_bytes(&w,1,"OpenCache",9);steam_wire_bytes(&w,8,"open.example.com",16);steam_wire_bytes(&r,1,entry,w.size);reply_size=r.size;
  assert(!steam_backend_servers(&b,hosts,&count)&&b.error==3004&&!count);
 }
 puts("Authenticated CM cell routing reaches CDN discovery, with zero-cell fallback");return 0;
}
