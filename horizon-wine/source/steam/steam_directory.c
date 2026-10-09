#include "steam_probe.h"
#include "../json_reader.h"

int steam_endpoint_valid(const char *s)
{
    const char *colon = strchr(s, ':');
    const char suffix[] = ".steamserver.net";
    const char *p;
    unsigned int port=0;
    size_t host;
    if (!colon || strlen(s)>=128) return 0;
    host=(size_t)(colon-s);
    if (host<=sizeof(suffix)-1 || memcmp(colon-(sizeof(suffix)-1),suffix,sizeof(suffix)-1)) return 0;
    for(p=s;p<colon;++p) if (!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='-')) return 0;
    if (!colon[1]) return 0;
    for(p=colon+1;*p;++p) {
        if (*p<'0'||*p>'9'||port>6553) return 0;
        port=port*10+(unsigned int)(*p-'0');
    }
    return port>0 && port<=65535;
}
static int servers_array(struct parser *p, struct steam_servers *servers)
{
    char key[64], endpoint[128], type[32], realm[32];
    if (!consume(p,'[')) return 0;
    whitespace(p);
    if (p->p<p->end && *p->p==']') { ++p->p; return 1; }
    do {
        unsigned int seen=0;
        endpoint[0]=type[0]=realm[0]=0;
        if (!consume(p,'{')) return 0;
        do {
            if (!json_string(p,key,sizeof(key))||!consume(p,':')) return 0;
            if (!strcmp(key,"endpoint")) { if ((seen&1)||!json_string(p,endpoint,sizeof(endpoint))) return 0; seen|=1; }
            else if (!strcmp(key,"type")) { if ((seen&2)||!json_string(p,type,sizeof(type))) return 0; seen|=2; }
            else if (!strcmp(key,"realm")) { if ((seen&4)||!json_string(p,realm,sizeof(realm))) return 0; seen|=4; }
            else if (!skip_value(p)) return 0;
            whitespace(p); if (p->p<p->end && *p->p=='}') break;
            if (!consume(p,',')) return 0;
        } while(1);
        if (!consume(p,'}')) return 0;
        if (seen==7 && !strcmp(type,"websockets") && !strcmp(realm,"steamglobal") &&
            steam_endpoint_valid(endpoint) && servers->count<STEAM_CM_SERVERS)
            strcpy(servers->endpoint[servers->count++],endpoint);
        whitespace(p); if (p->p<p->end && *p->p==']') break;
        if (!consume(p,',')) return 0;
    } while(1);
    return consume(p,']');
}
int steam_directory_servers(const unsigned char *data, size_t size, struct steam_servers *servers)
{
    struct parser p={data,data+size,0};
    char key[64];
    int seen=0;
    memset(servers,0,sizeof(*servers));
    if (!steam_directory_valid(data,size)||!consume(&p,'{')) return 0;
    do {
        if (!json_string(&p,key,sizeof(key))||!consume(&p,':')) return 0;
        if (!strcmp(key,"response")) {
            if (!consume(&p,'{')) return 0;
            do {
                if (!json_string(&p,key,sizeof(key))||!consume(&p,':')) return 0;
                if (!strcmp(key,"serverlist")) { if (seen++||!servers_array(&p,servers)) return 0; }
                else if (!skip_value(&p)) return 0;
                whitespace(&p); if (p.p<p.end && *p.p=='}') break;
                if (!consume(&p,',')) return 0;
            } while(1);
            if (!consume(&p,'}')) return 0;
        } else if (!skip_value(&p)) return 0;
        whitespace(&p); if (p.p<p.end && *p.p=='}') break;
        if (!consume(&p,',')) return 0;
    } while(1);
    return servers->count>0;
}

/* Parse the envelope, not a substring: duplicate success/response keys fail closed.
 * Directory contents are deliberately not retained by the connectivity probe. */
int steam_directory_valid(const unsigned char *data, size_t size)
{
    struct parser p = {data, data + size, 0};
    char key[64];
    int response = 0, success = 0, seen = 0;
    if (!size || size > 65536 || !consume(&p, '{')) return 0;
    do {
        if (!json_string(&p, key, sizeof(key)) || !consume(&p, ':')) return 0;
        if (!strcmp(key, "response")) {
            if (response++ || !consume(&p, '{')) return 0;
            do {
                if (!json_string(&p, key, sizeof(key)) || !consume(&p, ':')) return 0;
                if (!strcmp(key, "success")) {
                    if (seen++ || !json_bool(&p, &success)) return 0;
                } else if (!skip_value(&p)) return 0;
                whitespace(&p);
                if (p.p < p.end && *p.p == '}') break;
                if (!consume(&p, ',')) return 0;
            } while (1);
            if (!consume(&p, '}')) return 0;
        } else if (!skip_value(&p)) return 0;
        whitespace(&p);
        if (p.p < p.end && *p.p == '}') break;
        if (!consume(&p, ',')) return 0;
    } while (1);
    if (!consume(&p, '}')) return 0;
    whitespace(&p);
    return p.p == p.end && response == 1 && seen == 1 && success;
}
