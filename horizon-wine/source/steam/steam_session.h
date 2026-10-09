#ifndef AUTORUN_STEAM_SESSION_H
#define AUTORUN_STEAM_SESSION_H
#include "steam_auth.h"
/* v1: local passphrase, PBKDF2-HMAC-SHA256 600,000 iterations.
 * v2: explicit device-bound SPL opt-in. Both AES-256-GCM, random salt/nonce.
 * No plaintext/hardcoded/SD-stored encryption key or persisted passphrase. */
int steam_session_present(const char *root);
/* Version 2 opts into device-bound automatic unlock; v1 stays passphrase-only. */
int steam_session_automatic(const char *root);
unsigned int steam_session_save_automatic(const char *root,const struct steam_auth *);
unsigned int steam_session_load_automatic(const char *root,struct steam_auth *);
int steam_session_passphrase_valid(const char *);
unsigned int steam_session_save(const char *root,const char *passphrase,const struct steam_auth *);
unsigned int steam_session_load(const char *root,const char *passphrase,struct steam_auth *);
unsigned int steam_session_forget(const char *root);
#endif
