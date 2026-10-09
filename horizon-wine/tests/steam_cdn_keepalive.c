/* Local verified-HTTPS fixture only. CA is supplied by the test harness;
 * production TLS settings and official host discovery remain unchanged. */
#include <assert.h>
#include <curl/curl.h>
#include <stdlib.h>
static void reset_with_fixture_ca(CURL *curl){
    curl_easy_reset(curl);
    assert(curl_easy_setopt(curl,CURLOPT_CAINFO,getenv("STEAM_TEST_CA"))==CURLE_OK);
}
#define curl_easy_reset reset_with_fixture_ca
#include "../source/steam/steam_install.c"
int steam_backend_cdn_auth(struct steam_backend *b,uint32_t app,uint32_t depot,const char *host,char token[1024]){
    (void)b;(void)app;(void)depot;(void)host;(void)token;assert(!"Unexpected CDN authorization request");return 0;
}
int main(int argc,char **argv){
    char hosts[8][128]={{0}};unsigned char data[65536];
    struct steam_download_view view={0};struct cdn_session cdn={0};
    struct steam_install_callbacks cb={0};
    struct transfer t={.cdn=&cdn,.cb=&cb,.view=&view,.memory=data,.capacity=sizeof(data)};
    unsigned int i;assert(argc==2&&getenv("STEAM_TEST_CA"));
    assert(snprintf(hosts[0],sizeof(hosts[0]),"localhost:%s",argv[1])<(int)sizeof(hosts[0]));
    assert(!curl_global_init(CURL_GLOBAL_DEFAULT));
    cdn.started=cdn.rate_tick=millis();
    for(i=0;i<8;++i){assert(fetch(hosts,1,"fixture",&t)&&t.used==sizeof(data));assert(data[0]=='x'&&data[65535]=='x');}
    assert(view.requests==8&&view.connections==1&&view.network_bytes==8*sizeof(data));
    assert(view.first_byte_ms<=view.network_ms&&view.body_ms<=view.network_ms);
    curl_easy_cleanup(cdn.curl);steam_secret_clear(&cdn,sizeof(cdn));curl_global_cleanup();
    puts("Verified local HTTPS: 8 requests share 1 connection through production fetch logic");return 0;
}
