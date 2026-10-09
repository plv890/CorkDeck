#ifndef AUTORUN_STEAM_CRYPTO_H
#define AUTORUN_STEAM_CRYPTO_H
#include <stddef.h>
#include <stdint.h>
/* Unverified routing hint; authenticated CM logon must confirm this identity. */
uint64_t steam_token_subject(const char *);
int steam_random(void *, size_t);
int steam_rsa_encrypt(const char *modulus, const char *exponent, const char *password, char *out, size_t capacity);
#endif
