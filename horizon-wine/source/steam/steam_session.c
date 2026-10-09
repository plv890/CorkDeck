#define _POSIX_C_SOURCE 200809L
#include "steam_session.h"
#include "steam_crypto.h"
#include "steam_file.h"
#include "steam_device_key.h"
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
enum { VAULT_IO = 3401, VAULT_FORMAT, VAULT_UNLOCK, VAULT_CRYPTO, VAULT_PASSPHRASE };
#define HEADER 40u
#define CAPACITY 4300u
static const unsigned char magic[8] = {'A','R','S','T','E','A','M',1};
static int filename(const char *,char [768],int);
int steam_session_automatic(const char *root) {
    char path[768];unsigned char header[8];FILE *f;int automatic;
    if(!filename(root,path,0)||!steam_file_recover(path)||!(f=fopen(path,"rb")))return 0;
    automatic=fread(header,1,sizeof(header),f)==sizeof(header)&&
        !memcmp(header,magic,7)&&header[7]==2;
    fclose(f);return automatic;
}
static int filename(const char *root,char out[768],int temporary) {
    struct stat st;
    int n;
    if (!root[0]) return 0;
    n = snprintf(out,768,"%s/steam-session.vault%s",root,temporary==2?".previous":temporary?".tmp":"");
    if(n<0||n>=768)return 0;
    return lstat(out,&st)?errno==ENOENT:S_ISREG(st.st_mode)&&!S_ISLNK(st.st_mode);
}
int steam_session_present(const char *root) {
    char path[768];struct stat st;
    return filename(root,path,0)&&steam_file_recover(path)&&!lstat(path,&st);
}
static int key(const char *pass,const unsigned char salt[16],unsigned char out[32]) {
    mbedtls_md_context_t ctx;int ok=0;
    mbedtls_md_init(&ctx);
    if(!mbedtls_md_setup(&ctx,mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),1))
        ok=!mbedtls_pkcs5_pbkdf2_hmac(&ctx,(const unsigned char *)pass,strlen(pass),salt,16,600000,32,out);
    mbedtls_md_free(&ctx);return ok;
}
int steam_session_passphrase_valid(const char *pass) {
    size_t n=strlen(pass),i,count=0;
    if(n>=128)return 0;
    /* Native keyboard returns UTF-8. Count character starts, not UTF-8 bytes. */
    for(i=0;i<n;++i){unsigned char c=(unsigned char)pass[i];
        if(c<32||c==127)return 0;
        if((c&0xc0)!=0x80)++count;
    }
    return count>=12;
}
static unsigned int save(const char *root,const char *pass,const struct steam_auth *a,int automatic) {
    unsigned char *record=calloc(1,CAPACITY),*plain=calloc(1,CAPACITY),derived[32]={0};
    struct steam_wire_writer w={plain,CAPACITY-HEADER-16,0,0};
    mbedtls_gcm_context ctx;char path[768],tmp[768];FILE *f=NULL;
    unsigned int error=VAULT_CRYPTO;int fd=-1;size_t size=0;
    mbedtls_gcm_init(&ctx);
    if(!automatic&&!steam_session_passphrase_valid(pass)){error=VAULT_PASSPHRASE;goto done;}
    if(!record||!plain)goto done;
    if(!a->steam_id||!a->account_name[0]||!a->refresh_token[0]){error=VAULT_FORMAT;goto done;}
    steam_wire_fixed64(&w,1,a->steam_id);
    steam_wire_bytes(&w,2,a->account_name,strlen(a->account_name));
    steam_wire_bytes(&w,3,a->refresh_token,strlen(a->refresh_token));
    if(w.failed){error=VAULT_FORMAT;goto done;}
    memcpy(record,magic,8);
    if(automatic)record[7]=2;
    if(!steam_random(record+8,28))goto done;
    if(!(automatic?steam_device_key(record+8,derived):key(pass,record+8,derived)))goto done;
    record[36]=(unsigned char)w.size;record[37]=(unsigned char)(w.size>>8);
    record[38]=(unsigned char)(w.size>>16);record[39]=(unsigned char)(w.size>>24);
    if(mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,derived,256)||
       mbedtls_gcm_crypt_and_tag(&ctx,MBEDTLS_GCM_ENCRYPT,w.size,record+24,12,record,HEADER,plain,record+HEADER,16,record+HEADER+w.size))goto done;
    error=VAULT_IO;
    if(!filename(root,path,0)||!filename(root,tmp,1))goto done;
    {int flags=O_WRONLY|O_CREAT|O_TRUNC;
#ifdef O_NOFOLLOW
    flags|=O_NOFOLLOW;
#endif
    fd=open(tmp,flags,0600);}
    if(fd<0)goto done;
    f=fdopen(fd,"wb");if(!f){close(fd);goto done;}
    size=HEADER+w.size+16;
    if(fwrite(record,1,size,f)!=size||fflush(f)||fsync(fileno(f)))goto done;
    if(fclose(f)){f=NULL;goto done;}f=NULL;
    if(!steam_file_replace(tmp,path))goto done;
    error=0;
done:
    if(f)fclose(f);
    mbedtls_gcm_free(&ctx);steam_secret_clear(derived,sizeof(derived));
    if(record){steam_secret_clear(record,CAPACITY);free(record);}
    if(plain){steam_secret_clear(plain,CAPACITY);free(plain);}
    return error;
}
static unsigned int load(const char *root,const char *pass,struct steam_auth *a,int automatic) {
    unsigned char *record=calloc(1,CAPACITY),*plain=calloc(1,CAPACITY),derived[32]={0};
    mbedtls_gcm_context ctx;char path[768];FILE *f=NULL;
    struct steam_wire_reader r;struct steam_wire_field field;
    unsigned int error=VAULT_FORMAT,seen=0;size_t size,length;int n;
    mbedtls_gcm_init(&ctx);steam_auth_clear(a);
    if(!automatic&&!steam_session_passphrase_valid(pass)){error=VAULT_PASSPHRASE;goto done;}
    if(!record||!plain){error=VAULT_CRYPTO;goto done;}
    error=VAULT_IO;
    if(!filename(root,path,0)||!steam_file_recover(path)||!(f=fopen(path,"rb")))goto done;
    size=fread(record,1,CAPACITY,f);
    if(ferror(f))goto done;
    error=VAULT_FORMAT;
    if(size<HEADER+16||size==CAPACITY||memcmp(record,magic,7)||record[7]!=(automatic?2:1))goto done;
    length=(size_t)record[36]|((size_t)record[37]<<8)|((size_t)record[38]<<16)|((size_t)record[39]<<24);
    if(length!=size-HEADER-16)goto done;
    error=VAULT_CRYPTO;
    if(!(automatic?steam_device_key(record+8,derived):key(pass,record+8,derived))||mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,derived,256))goto done;
    error=VAULT_UNLOCK;
    if(mbedtls_gcm_auth_decrypt(&ctx,length,record+24,12,record,HEADER,record+HEADER+length,16,record+HEADER,plain))goto done;
    error=VAULT_FORMAT;r=(struct steam_wire_reader){plain,length,0,0};
    while((n=steam_wire_next(&r,&field))==1) {
        char *dst;size_t cap;
        if(field.number<1||field.number>3||(seen&(1u<<field.number)))goto done;
        seen|=1u<<field.number;
        if(field.number==1){if(field.type!=1||!field.integer)goto done;a->steam_id=field.integer;continue;}
        dst=field.number==2?a->account_name:a->refresh_token;
        cap=field.number==2?sizeof(a->account_name):sizeof(a->refresh_token);
        if(field.type!=2||!field.size||field.size>=cap||memchr(field.data,0,field.size))goto done;
        memcpy(dst,field.data,field.size);dst[field.size]=0;
    }
    if(n<0||seen!=14)goto done;
    /* Subject is a routing hint only. Provider still requires exact CM logon. */
    if(steam_token_subject(a->refresh_token)!=a->steam_id)goto done;
    a->state=STEAM_AUTH_TOKENS;error=0;
done:
    if(f)fclose(f);
    mbedtls_gcm_free(&ctx);steam_secret_clear(derived,sizeof(derived));
    if(record){steam_secret_clear(record,CAPACITY);free(record);}
    if(plain){steam_secret_clear(plain,CAPACITY);free(plain);}
    if(error)steam_auth_clear(a);
    return error;
}
unsigned int steam_session_save(const char *root,const char *pass,const struct steam_auth *a)
{return save(root,pass,a,0);}
unsigned int steam_session_load(const char *root,const char *pass,struct steam_auth *a)
{return load(root,pass,a,0);}
unsigned int steam_session_save_automatic(const char *root,const struct steam_auth *a)
{return save(root,NULL,a,1);}
unsigned int steam_session_load_automatic(const char *root,struct steam_auth *a)
{return load(root,NULL,a,1);}
unsigned int steam_session_forget(const char *root) {
    char path[768];int i;
    for(i=0;i<3;++i) {
        if(!filename(root,path,i))return VAULT_IO;
        if(unlink(path)&&errno!=ENOENT)return VAULT_IO;
    }
    return 0;
}
