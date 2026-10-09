#ifndef AUTORUN_STEAM_BACKEND_H
#define AUTORUN_STEAM_BACKEND_H
#include "steam_cm.h"
#include "steam_app.h"
struct steam_backend { struct steam_cm *cm;uint64_t account,job;uint32_t session,cell_id;unsigned int error,concurrency;char language[32];
    /* Bit per CDN host of the last server list: content may be requested
     * over plain HTTP / the host answers HTTPS at all. */
    unsigned int plain_hosts,tls_hosts; };
int steam_backend_app(struct steam_backend *,uint32_t,struct steam_app *);
int steam_backend_depot(struct steam_backend *,uint32_t app,const struct steam_depot *,unsigned char key[32],uint64_t *code);
int steam_backend_servers(struct steam_backend *,char hosts[8][128],size_t *count);
int steam_backend_cdn_auth(struct steam_backend *,uint32_t app,uint32_t depot,const char *host,char token[1024]);
#endif
