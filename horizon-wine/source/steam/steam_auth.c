#include "steam_auth.h"
#include <math.h>
#include <string.h>

void steam_auth_clear(struct steam_auth *s) { steam_secret_clear(s, sizeof(*s)); }
static int fail(struct steam_auth *s, unsigned int result)
{
    steam_auth_clear(s);
    s->state = STEAM_AUTH_FAILED; s->result = result;
    return 0;
}
static int text(const struct steam_wire_field *f, char *out, size_t capacity)
{
    if (f->type != 2 || f->size >= capacity || memchr(f->data, 0, f->size)) return 0;
    memcpy(out, f->data, f->size); out[f->size] = 0;
    return 1;
}
static int challenge_valid(const char *value)
{
    const char *p;
    /* QR confirmation challenges are sensitive. Do not open arbitrary URLs from
     * a response; only render Steam's expected mobile-confirmation origin. */
    if (strncmp(value, "https://s.team/q/", 17)) return 0;
    for (p = value+17; *p; ++p)
        if (!((*p >= '0' && *p <= '9') || *p == '/')) return 0;
    return p > value+17;
}
int steam_auth_begin_qr(struct steam_auth *s, uint64_t job, void *packet, size_t capacity, size_t *written)
{
    unsigned char device[128], body[192];
    struct steam_wire_writer d = {device, sizeof(device), 0, 0};
    struct steam_wire_writer b = {body, sizeof(body), 0, 0};
    *written = 0;
    if (s->state != STEAM_AUTH_IDLE && s->state != STEAM_AUTH_FAILED) return 0;
    steam_auth_clear(s);
    steam_wire_bytes(&d, 1, "Autorun on Nintendo Switch", 26);
    steam_wire_uint(&d, 2, 1); /* SteamClient platform. */
    steam_wire_bytes(&b, 1, "Autorun on Nintendo Switch", 26);
    steam_wire_uint(&b, 2, 1);
    steam_wire_bytes(&b, 3, device, d.size);
    steam_wire_bytes(&b, 4, "Client", 6);
    if (d.failed || b.failed || !steam_cm_auth_request(STEAM_AUTH_QR, job, body, b.size, packet, capacity, written)) return 0;
    s->pending_job = job; s->state = STEAM_AUTH_BEGIN_SENT;
    return 1;
}
int steam_auth_poll(struct steam_auth *s, uint64_t job, void *packet, size_t capacity, size_t *written)
{
    unsigned char body[288];
    struct steam_wire_writer b = {body, sizeof(body), 0, 0};
    int ok;
    *written = 0;
    if (s->state != STEAM_AUTH_WAITING) return 0;
    steam_wire_uint(&b, 1, s->client_id);
    steam_wire_bytes(&b, 2, s->request_id, s->request_size);
    ok = !b.failed && steam_cm_auth_request(STEAM_AUTH_POLL, job, body, b.size, packet, capacity, written);
    steam_secret_clear(body, sizeof(body));
    if (!ok) return 0;
    s->pending_job = job; s->state = STEAM_AUTH_POLL_SENT;
    return 1;
}
int steam_auth_begin_credentials(struct steam_auth *s, uint64_t job, const char *account,
    const char *encrypted, uint64_t timestamp, void *packet, size_t capacity, size_t *written)
{
    unsigned char body[1536], device[128];
    struct steam_wire_writer b = {body,sizeof(body),0,0}, d = {device,sizeof(device),0,0};
    size_t length, i;
    int ok;
    *written = 0;
    if (s->state != STEAM_AUTH_IDLE && s->state != STEAM_AUTH_FAILED) return 0;
    if (!account || !encrypted || !timestamp || !account[0] || strlen(account)>127) return 0;
    length = strlen(encrypted);
    if (length < 172 || length > 1024 || length % 4) return 0;
    for (i=0;i<length;++i) {
        char c=encrypted[i];
        if (!((c>='A'&&c<='Z') || (c>='a'&&c<='z') || (c>='0'&&c<='9') ||
              c=='+' || c=='/' || (c=='=' && i>=length-2))) return 0;
    }
    steam_auth_clear(s);
    steam_wire_bytes(&d,1,"Autorun on Nintendo Switch",26);
    steam_wire_uint(&d,2,1);
    steam_wire_bytes(&b,2,account,strlen(account));
    steam_wire_bytes(&b,3,encrypted,length);
    steam_wire_uint(&b,4,timestamp);
    steam_wire_uint(&b,5,1);
    steam_wire_uint(&b,6,1); /* SteamClient token audience, also present in device details. */
    steam_wire_uint(&b,7,1); /* Persistent token; saving locally is separate consent. */
    steam_wire_bytes(&b,8,"Client",6);
    steam_wire_bytes(&b,9,device,d.size);
    ok=!d.failed && !b.failed && steam_cm_auth_request(STEAM_AUTH_CREDENTIALS,job,body,b.size,packet,capacity,written);
    steam_secret_clear(body,sizeof(body));
    if (ok) { s->pending_job=job; s->credential_flow=1; s->state=STEAM_AUTH_BEGIN_SENT; }
    return ok;
}
int steam_auth_guard(struct steam_auth *s, uint64_t job, unsigned int type, const char *code,
    void *packet, size_t capacity, size_t *written)
{
    unsigned char body[128];
    struct steam_wire_writer b={body,sizeof(body),0,0};
    size_t i, length;
    int ok;
    *written=0;
    if (s->state!=STEAM_AUTH_WAITING || !s->credential_flow || !s->steam_id ||
        (type!=2 && type!=3) || !(s->confirmations & (1u<<type)) || !code) return 0;
    length=strlen(code);
    if (!length || length>16) return 0;
    for (i=0;i<length;++i) if (!((code[i]>='0'&&code[i]<='9') || (code[i]>='A'&&code[i]<='Z'))) return 0;
    steam_wire_uint(&b,1,s->client_id);
    steam_wire_fixed64(&b,2,s->steam_id);
    steam_wire_bytes(&b,3,code,length);
    steam_wire_uint(&b,4,type);
    ok=!b.failed && steam_cm_auth_request(STEAM_AUTH_GUARD,job,body,b.size,packet,capacity,written);
    steam_secret_clear(body,sizeof(body));
    if (ok) { s->pending_job=job; s->state=STEAM_AUTH_GUARD_SENT; }
    return ok;
}
static int confirmation(struct steam_auth *s, const struct steam_wire_field *f)
{
    struct steam_wire_reader r={f->data,f->size,0,0};
    struct steam_wire_field item;
    int next, seen=0;
    if (f->type!=2 || f->size>1024) return 0;
    while ((next=steam_wire_next(&r,&item))==1) {
        if (item.number!=1) continue;
        if (seen++ || item.type!=0 || item.integer>31) return 0;
        s->confirmations |= 1u << item.integer;
    }
    return next==0 && seen;
}
int steam_auth_accept(struct steam_auth *s, const struct steam_cm_message *m)
{
    struct steam_wire_reader r = {m->body, m->body_size, 0, 0};
    struct steam_wire_field f;
    uint32_t seen = 0;
    int next, begin = s->state == STEAM_AUTH_BEGIN_SENT;
    if (!begin && s->state != STEAM_AUTH_POLL_SENT && s->state != STEAM_AUTH_GUARD_SENT) return fail(s, 2);
    if (m->type != 147 || m->target_job != s->pending_job) return fail(s, 2);
    if (s->state == STEAM_AUTH_GUARD_SENT) {
        /* InvalidLoginAuthCode / TwoFactorCodeMismatch allow another user attempt.
         * DuplicateRequest can mean mobile confirmation already approved it. */
        if (m->result!=1 && m->result!=29 && m->result!=65 && m->result!=88) return fail(s,m->result);
        s->pending_job=0; s->result=m->result; s->state=STEAM_AUTH_WAITING;
        return 1;
    }
    if (m->result != 1) return fail(s, m->result);
    if (m->body_size > 16384) return fail(s, 2);
    while ((next = steam_wire_next(&r, &f)) == 1) {
        if (begin && s->credential_flow) {
            if (f.number==4) { if (!confirmation(s,&f)) return fail(s,2); continue; }
            if (f.number==5) {
                if (s->steam_id || f.type!=0 || !f.integer) return fail(s,2);
                s->steam_id=f.integer; continue;
            }
            /* Agreements need a separate supported flow, not silent acceptance. */
            if (f.number==7) { if (f.type!=2 || f.size) return fail(s,2); continue; }
            if (f.number==2) f.number=3;
            else if (f.number==3) f.number=4;
            else if (f.number!=1) continue;
        }
        if (f.number > (begin ? 4u : 6u)) continue;
        if (seen & (1u << f.number)) return fail(s, 2);
        seen |= 1u << f.number;
        if (begin) {
            switch (f.number) {
            case 1:
                if (f.type != 0 || !f.integer) return fail(s, 2);
                s->client_id = f.integer; break;
            case 2:
                if (!text(&f, s->challenge, sizeof(s->challenge)) || !challenge_valid(s->challenge)) return fail(s, 2);
                break;
            case 3:
                if (f.type != 2 || !f.size || f.size > sizeof(s->request_id)) return fail(s, 2);
                memcpy(s->request_id, f.data, f.size); s->request_size = f.size; break;
            case 4: {
                uint32_t bits = (uint32_t)f.integer;
                float interval;
                if (f.type != 5) return fail(s, 2);
                memcpy(&interval, &bits, sizeof(interval));
                if (!isfinite(interval) || interval <= 0 || interval > 120) return fail(s, 2);
                s->poll_interval_ms = (unsigned int)ceilf(interval * 1000);
                if (s->poll_interval_ms < 1000) s->poll_interval_ms = 1000;
                break;
            }
            }
        } else {
            switch (f.number) {
            case 1:
                if (f.type != 0) return fail(s, 2);
                if (f.integer) s->client_id = f.integer;
                break;
            case 2:
                if (!text(&f, s->challenge, sizeof(s->challenge)) || !challenge_valid(s->challenge)) return fail(s, 2);
                break;
            case 3: if (!text(&f, s->refresh_token, sizeof(s->refresh_token))) return fail(s, 2); break;
            case 4: if (!text(&f, s->access_token, sizeof(s->access_token))) return fail(s, 2); break;
            case 6: if (!text(&f, s->account_name, sizeof(s->account_name))) return fail(s, 2); break;
            }
        }
    }
    if (next < 0 || (begin && (seen & (s->credential_flow ? 26u : 30u)) != (s->credential_flow ? 26u : 30u))) return fail(s, 2);
    if (begin && s->credential_flow && (!s->steam_id || !s->confirmations)) return fail(s,2);
    s->pending_job = 0; s->result = 1;
    s->state = s->refresh_token[0] ? STEAM_AUTH_TOKENS : STEAM_AUTH_WAITING;
    if (s->state == STEAM_AUTH_TOKENS) {
        steam_secret_clear(s->request_id, sizeof(s->request_id));
        steam_secret_clear(s->challenge, sizeof(s->challenge));
        s->request_size = 0; s->client_id = 0;
    }
    return 1;
}
