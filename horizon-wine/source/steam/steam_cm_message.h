#ifndef AUTORUN_STEAM_CM_MESSAGE_H
#define AUTORUN_STEAM_CM_MESSAGE_H
#include "steam_wire.h"
#define STEAM_CM_PACKET_MAX (1024u * 1024u)
#define STEAM_CM_HEADER_MAX 4096u
struct steam_cm_message {
    uint32_t type, session_id, result;
    uint64_t steam_id, source_job, target_job;
    const unsigned char *body;
    size_t body_size;
};
/* Handles one decompressed binary CM message. Multi decompression and transport
 * are separate bounded layers; never feed a WebSocket frame here directly. */
int steam_cm_message_read(const void *, size_t, struct steam_cm_message *);
/* Authentication unified call before logon; method is selected by enum only. */
enum steam_auth_method { STEAM_AUTH_RSA, STEAM_AUTH_CREDENTIALS, STEAM_AUTH_QR,
    STEAM_AUTH_GUARD, STEAM_AUTH_POLL, STEAM_AUTH_METHOD_COUNT };
int steam_cm_auth_request(enum steam_auth_method, uint64_t job, const void *body,
    size_t body_size, void *output, size_t capacity, size_t *written);
#endif
