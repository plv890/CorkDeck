#ifndef AUTORUN_STEAM_DEVICE_KEY_H
#define AUTORUN_STEAM_DEVICE_KEY_H
/* Device-bound derivation. Salt is public; output is ephemeral, never persisted.
 * Not app-isolated: privileged homebrew on this console can call the same SPL API. */
int steam_device_key(const unsigned char salt[16], unsigned char out[32]);
#endif
