#include "steam_device_key.h"
#include "steam_crypto.h"
#include "steam_wire.h"
#include <string.h>
#ifdef __SWITCH__
#include <switch.h>
int steam_device_key(const unsigned char salt[16], unsigned char out[32])
{
    /* Public domain separation, not a software encryption key. SPL generation
     * 4 selects the stable 4.0.0 device master key; option bit 0 is device-unique.
     * Regenerate sealed KEK each time: sealed handles are boot-specific. */
    static const unsigned char source[16] = {'A','u','t','o','r','u','n','S','t','e','a','m','K','e','y','2'};
    unsigned char sealed[16] = {0}, label[32] = {0};
    u32 slot = 0;
    int locked = 0, ok = 0;
    Result rc;
    memset(out, 0, 32);
    rc = splCryptoInitialize();
    if (R_FAILED(rc)) return 0;
    rc = splCryptoGenerateAesKek(salt, 4, 1, sealed);
    if (R_FAILED(rc)) goto done;
    rc = splCryptoLockAesEngine(&slot);
    if (R_FAILED(rc)) goto done;
    locked = 1;
    rc = splCryptoLoadAesKey(sealed, source, slot);
    if (R_FAILED(rc)) goto done;
    memcpy(label, "Autorun Steam vault v2", 21);
    label[31] = 1;
    rc = splCryptoComputeCmac(label, sizeof(label), slot, out);
    if (R_FAILED(rc)) goto done;
    label[31] = 2;
    rc = splCryptoComputeCmac(label, sizeof(label), slot, out + 16);
    ok = R_SUCCEEDED(rc);
done:
    if (locked && R_FAILED(splCryptoUnlockAesEngine(slot))) ok = 0;
    splCryptoExit();
    steam_secret_clear(sealed, sizeof(sealed));
    steam_secret_clear(label, sizeof(label));
    if (!ok) steam_secret_clear(out, 32);
    return ok;
}
#elif !defined(STEAM_DEVICE_KEY_FIXTURE)
int steam_device_key(const unsigned char salt[16], unsigned char out[32])
{ (void)salt; memset(out, 0, 32); return 0; }
#endif
