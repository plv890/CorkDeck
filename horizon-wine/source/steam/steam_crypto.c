#include "steam_crypto.h"
#include "steam_wire.h"
#include "../json_reader.h"
#include <mbedtls/rsa.h>
#include <mbedtls/base64.h>
#include <string.h>
uint64_t steam_token_subject(const char *token)
{
    char encoded[4096],key[64],sub[32];unsigned char decoded[3072];
    const char *first=strchr(token,'.'),*last;size_t n,i,used=0;uint64_t id=0;int seen=0,ok=0;
    struct parser p;
    if(!first || !(last=strchr(first+1,'.')))return 0;
    n=(size_t)(last-first-1);if(!n||n>sizeof(encoded)-5||n%4==1)return 0;
    for(i=0;i<n;++i){char c=first[1+i];encoded[i]=c=='-'?'+':c=='_'?'/':c;}
    while(n%4)encoded[n++]='=';
    if(mbedtls_base64_decode(decoded,sizeof(decoded),&used,(unsigned char *)encoded,n))goto done;
    p=(struct parser){decoded,decoded+used,0};
    if(!consume(&p,'{'))goto done;
    do {
        if(!json_string(&p,key,sizeof(key))||!consume(&p,':'))goto done;
        if(!strcmp(key,"sub")){
            if(seen++||!json_string(&p,sub,sizeof(sub))||strlen(sub)!=17)goto done;
            for(i=0;sub[i];++i){if(sub[i]<'0'||sub[i]>'9')goto done;id=id*10+(unsigned int)(sub[i]-'0');}
        }else if(!skip_value(&p))goto done;
        whitespace(&p);if(p.p<p.end&&*p.p=='}')break;
        if(!consume(&p,','))goto done;
    }while(1);
    if(!consume(&p,'}'))goto done;
    whitespace(&p);ok=p.p==p.end&&seen==1&&id!=0;
done:
    steam_secret_clear(encoded,sizeof(encoded));steam_secret_clear(decoded,sizeof(decoded));steam_secret_clear(sub,sizeof(sub));
    return ok?id:0;
}
#ifdef __SWITCH__
#include <switch.h>
#else
#include <sys/random.h>
#include <errno.h>
#endif
int steam_random(void *data, size_t size)
{
#ifdef __SWITCH__
    Result rc=csrngInitialize();
    if (R_FAILED(rc)) return 0;
    rc=csrngGetRandomBytes(data,size);
    csrngExit();
    return R_SUCCEEDED(rc);
#else
    unsigned char *p=data;
    while (size) {
        ssize_t n=getrandom(p,size,0);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) return 0;
        p+=n; size-=(size_t)n;
    }
    return 1;
#endif
}
static int rng(void *unused, unsigned char *out, size_t size)
{ (void)unused; return steam_random(out,size) ? 0 : -1; }
int steam_rsa_encrypt(const char *modulus, const char *exponent, const char *password, char *out, size_t capacity)
{
    mbedtls_rsa_context rsa;
    mbedtls_mpi n,e;
    unsigned char encrypted[512];
    size_t used=0, length=strlen(password);
    int ok=0;
    if (capacity) out[0]=0;
    if (strlen(modulus)!=512 && strlen(modulus)!=1024) return 0;
    if (!length || length>255 || strlen(exponent)>8) return 0;
    mbedtls_rsa_init(&rsa,MBEDTLS_RSA_PKCS_V15,0);
    mbedtls_mpi_init(&n); mbedtls_mpi_init(&e);
    if (mbedtls_mpi_read_string(&n,16,modulus) || mbedtls_mpi_read_string(&e,16,exponent) ||
        mbedtls_mpi_cmp_int(&e,65537) || mbedtls_rsa_import(&rsa,&n,NULL,NULL,NULL,&e) ||
        mbedtls_rsa_complete(&rsa) || mbedtls_rsa_check_pubkey(&rsa)) goto done;
    if (rsa.len>sizeof(encrypted) || length>rsa.len-11) goto done;
    if (mbedtls_rsa_pkcs1_encrypt(&rsa,rng,NULL,MBEDTLS_RSA_PUBLIC,length,(const unsigned char *)password,encrypted)) goto done;
    if (mbedtls_base64_encode((unsigned char *)out,capacity,&used,encrypted,rsa.len)) goto done;
    ok=1;
done:
    steam_secret_clear(encrypted,sizeof(encrypted));
    mbedtls_mpi_free(&n); mbedtls_mpi_free(&e); mbedtls_rsa_free(&rsa);
    return ok;
}
