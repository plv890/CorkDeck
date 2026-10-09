#include "../source/steam/steam_cm.h"
#include "../source/steam/steam_auth.h"
#include <stdio.h>
#include <string.h>
int main(void)
{
    unsigned int error=0; unsigned char body[128],packet[512];size_t written;
    struct steam_wire_writer w={body,sizeof(body),0,0};struct steam_cm_message response;
    struct steam_cm *cm=steam_cm_connect(NULL,NULL,&error);
    if(!cm){printf("CM connection error=%u\n",error);return 1;}
    steam_wire_bytes(&w,1,"autorun-integration-probe",25);
    if(!steam_cm_auth_request(STEAM_AUTH_RSA,1,body,w.size,packet,sizeof(packet),&written) ||
       !steam_cm_exchange(cm,packet,written,1,0,&response)) {
        printf("CM RPC error=%u\n",steam_cm_error(cm));steam_cm_close(cm);return 1;
    }
    printf("CM RSA RPC type=%u result=%u body_bytes=%zu\n",response.type,response.result,response.body_size);
    error=response.result!=1;
    if(!error){
        struct steam_auth auth={0};
        if(!steam_auth_begin_qr(&auth,2,packet,sizeof(packet),&written)||
           !steam_cm_exchange(cm,packet,written,2,0,&response)||!steam_auth_accept(&auth,&response))error=1;
        else printf("CM QR challenge received; polling interval=%u ms (challenge redacted)\n",auth.poll_interval_ms);
        steam_auth_clear(&auth);
    }
    steam_cm_close(cm);return error;
}
