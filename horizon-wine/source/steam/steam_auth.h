#ifndef AUTORUN_STEAM_AUTH_H
#define AUTORUN_STEAM_AUTH_H
#include "steam_cm_message.h"

/* Worker-only internal state. Never pass this object to UI or diagnostic code.
 * Token receipt is NOT CM logon or verified ownership. */
enum steam_auth_state { STEAM_AUTH_IDLE, STEAM_AUTH_BEGIN_SENT,
    STEAM_AUTH_WAITING, STEAM_AUTH_POLL_SENT, STEAM_AUTH_TOKENS, STEAM_AUTH_FAILED,
    STEAM_AUTH_GUARD_SENT };
struct steam_auth {
    enum steam_auth_state state;
    uint64_t client_id, pending_job;
    unsigned int result, poll_interval_ms;
    unsigned int credential_flow, confirmations;
    uint64_t steam_id;
    unsigned char request_id[256];
    size_t request_size;
    char challenge[512], refresh_token[4096], access_token[4096], account_name[128];
};
void steam_auth_clear(struct steam_auth *);
int steam_auth_begin_qr(struct steam_auth *, uint64_t job, void *packet, size_t capacity, size_t *written);
/* Password encryption is a separate crypto adapter. This function ONLY accepts
 * base64 RSA ciphertext, never a plaintext password. Caller wipes all inputs. */
int steam_auth_begin_credentials(struct steam_auth *, uint64_t job, const char *account,
    const char *encrypted_password, uint64_t rsa_timestamp, void *packet, size_t capacity, size_t *written);
int steam_auth_guard(struct steam_auth *, uint64_t job, unsigned int type, const char *code,
    void *packet, size_t capacity, size_t *written);
/* Scheduler must wait poll_interval_ms between polls, and impose session expiry.
 * No automatic network retries: callers must explicitly schedule each request. */
int steam_auth_poll(struct steam_auth *, uint64_t job, void *packet, size_t capacity, size_t *written);
/* Strict CM type/job correlation. Malformed responses erase all pending secrets.
 * Unrelated CM traffic must be dispatched elsewhere by the connection owner. */
int steam_auth_accept(struct steam_auth *, const struct steam_cm_message *);
#endif
