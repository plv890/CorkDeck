#include "../source/steam/steam_crypto.h"
#include <mbedtls/rsa.h>
#include <mbedtls/base64.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int random_bytes(void *ctx,unsigned char *out,size_t size)
{(void)ctx;return steam_random(out,size)?0:-1;}
int main(void)
{
    mbedtls_rsa_context rsa;char modulus[1025],encrypted[1024];
    unsigned char cipher[512],plain[512];size_t size,used;
    const char *password="synthetic-test-password";
    assert(steam_token_subject("e30.eyJzdWIiOiI3NjU2MTE5ODAwMDAwMDAwMCJ9.signature")==UINT64_C(76561198000000000));
    assert(!steam_token_subject("e30.e30.signature"));
    assert(!steam_token_subject("invalid"));
    mbedtls_rsa_init(&rsa,MBEDTLS_RSA_PKCS_V15,0);
    assert(!mbedtls_rsa_gen_key(&rsa,random_bytes,NULL,2048,65537));
    assert(!mbedtls_mpi_write_string(&rsa.N,16,modulus,sizeof(modulus),&size));
    assert(steam_rsa_encrypt(modulus,"010001",password,encrypted,sizeof(encrypted)));
    assert(!mbedtls_base64_decode(cipher,sizeof(cipher),&size,(unsigned char *)encrypted,strlen(encrypted)));
    assert(size==256);
    assert(!mbedtls_rsa_pkcs1_decrypt(&rsa,random_bytes,NULL,MBEDTLS_RSA_PRIVATE,&used,cipher,plain,sizeof(plain)));
    assert(used==strlen(password)&&!memcmp(plain,password,used));
    assert(!steam_rsa_encrypt(modulus,"03",password,encrypted,sizeof(encrypted)));
    assert(!steam_rsa_encrypt(modulus,"010001","",encrypted,sizeof(encrypted)));
    assert(!steam_rsa_encrypt("00","010001",password,encrypted,sizeof(encrypted)));
    assert(!steam_rsa_encrypt(modulus,"010001",password,encrypted,2));
    mbedtls_rsa_free(&rsa);puts("Steam RSA roundtrip and rejection tests passed");return 0;
}
