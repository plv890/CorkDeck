#include "steam_cm_message.h"
#include <string.h>
static uint32_t get32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void put32(unsigned char *p, uint32_t n)
{
    unsigned int i;
    for (i = 0; i < 4; ++i) p[i] = (unsigned char)(n >> (8*i));
}
int steam_cm_message_read(const void *data, size_t size, struct steam_cm_message *out)
{
    const unsigned char *p = data;
    struct steam_cm_message m = {0};
    struct steam_wire_reader r;
    struct steam_wire_field f;
    uint32_t header, seen = 0;
    int next;
    memset(out, 0, sizeof(*out));
    if (!data || size < 8 || size > STEAM_CM_PACKET_MAX || !(get32(p) & 0x80000000u)) return 0;
    header = get32(p+4);
    if (header > STEAM_CM_HEADER_MAX || header > size - 8) return 0;
    m.type = get32(p) & 0x7fffffffu;
    m.result = 2; /* Steam's default EResult.Fail, never implicit OK. */
    m.source_job = m.target_job = UINT64_MAX;
    r = (struct steam_wire_reader){p+8, header, 0, 0};
    while ((next = steam_wire_next(&r, &f)) == 1) {
        unsigned int bit;
        if (f.number != 1 && f.number != 2 && f.number != 10 && f.number != 11 && f.number != 13) continue;
        bit = 1u << f.number;
        if (seen & bit) return 0;
        seen |= bit;
        if (f.number == 2 || f.number == 13) {
            if (f.type != 0 || (f.number == 13 && f.integer > UINT32_MAX)) return 0;
            /* int32 session IDs may be sign-extended on the wire. */
            if (f.number == 2) m.session_id = (uint32_t)f.integer;
            else m.result = (uint32_t)f.integer;
        } else {
            if (f.type != 1) return 0;
            if (f.number == 1) m.steam_id = f.integer;
            else if (f.number == 10) m.source_job = f.integer;
            else m.target_job = f.integer;
        }
    }
    if (next < 0) return 0;
    m.body = p + 8 + header; m.body_size = size - 8 - header;
    *out = m;
    return 1;
}
int steam_cm_auth_request(enum steam_auth_method method, uint64_t job, const void *body,
    size_t body_size, void *output, size_t capacity, size_t *written)
{
    static const char *const methods[] = {
        "Authentication.GetPasswordRSAPublicKey#1",
        "Authentication.BeginAuthSessionViaCredentials#1",
        "Authentication.BeginAuthSessionViaQR#1",
        "Authentication.UpdateAuthSessionWithSteamGuardCode#1",
        "Authentication.PollAuthSessionStatus#1"
    };
    unsigned char header[256], *p = output;
    struct steam_wire_writer w = {header, sizeof(header), 0, 0};
    *written = 0;
    if ((unsigned int)method >= STEAM_AUTH_METHOD_COUNT || !job || job == UINT64_MAX ||
        !output || (!body && body_size) || body_size > STEAM_CM_PACKET_MAX) return 0;
    if (!steam_wire_fixed64(&w, 10, job) ||
        !steam_wire_bytes(&w, 12, methods[method], strlen(methods[method]))) return 0;
    if (capacity < 8+w.size || body_size > capacity-8-w.size || body_size > STEAM_CM_PACKET_MAX-8-w.size) return 0;
    put32(p, 9804u | 0x80000000u); /* ServiceMethodCallFromClientNonAuthed */
    put32(p+4, (uint32_t)w.size);
    memcpy(p+8, header, w.size);
    if (body_size) memcpy(p+8+w.size, body, body_size);
    *written = 8+w.size+body_size;
    return 1;
}
