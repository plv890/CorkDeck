#ifndef AUTORUN_STEAM_CM_H
#define AUTORUN_STEAM_CM_H
#include "steam_probe.h"
#include "steam_cm_message.h"
struct steam_cm;
struct steam_cm *steam_cm_connect(steam_cancel_fn, void *, unsigned int *error);
/* The transfer library's code from the last server-directory request. */
unsigned int steam_cm_connect_transport(void);
void steam_cm_close(struct steam_cm *);
/* Single worker, one in-flight RPC. Raw response remains internal and is wiped on reuse. */
int steam_cm_exchange(struct steam_cm *, const void *, size_t, uint64_t job, uint32_t type, struct steam_cm_message *);
int steam_cm_send(struct steam_cm *, uint32_t type, uint64_t steamid, uint32_t session, const void *, size_t);
int steam_cm_tick(struct steam_cm *);
unsigned int steam_cm_error(const struct steam_cm *);
unsigned int steam_cm_last_type(const struct steam_cm *);
enum steam_service { STEAM_SERVICE_OWNED,STEAM_SERVICE_MANIFEST_CODE,STEAM_SERVICE_SERVERS,STEAM_SERVICE_CDN_AUTH,STEAM_SERVICE_FAMILY_GROUP,STEAM_SERVICE_FAMILY_LIBRARY };
int steam_cm_call(struct steam_cm *,enum steam_service,uint64_t steamid,uint32_t session,uint64_t job,const void *body,size_t,struct steam_cm_message *);
int steam_cm_request(struct steam_cm *,uint32_t type,uint64_t steamid,uint32_t session,uint64_t job,const void *body,size_t,struct steam_cm_message *);
#endif
