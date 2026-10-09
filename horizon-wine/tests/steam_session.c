#define _POSIX_C_SOURCE 200809L
#include "../source/steam/steam_session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/* Synthetic device adapter: never a production fallback. */
static unsigned char fixture_device=19;
static int fixture_available=1;
int steam_device_key(const unsigned char salt[16],unsigned char out[32]){
    size_t i;memset(out,0,32);if(!fixture_available)return 0;
    for(i=0;i<32;++i)out[i]=salt[i%16]^fixture_device^(unsigned char)i;
    return 1;
}
static int contains(const unsigned char *p,size_t size,const char *s){
    size_t n=strlen(s),i;for(i=0;i+n<=size;++i)if(!memcmp(p+i,s,n))return 1;return 0;
}
int main(void){
    char root[]="/tmp/steam-vault-XXXXXX",path[768];
    struct steam_auth a={.steam_id=UINT64_C(76561198000000000)},b={0};
    const char *pass="local synthetic vault passphrase";
    unsigned char first[4300],second[4300];size_t n,n2;FILE *f;
    strcpy(a.account_name,"synthetic-account");
    strcpy(a.refresh_token,"e30.eyJzdWIiOiI3NjU2MTE5ODAwMDAwMDAwMCJ9.signature");
    strcpy(a.access_token,"must-not-be-stored-access-token");
    assert(mkdtemp(root)&&!steam_session_present(root));
    assert(!steam_session_passphrase_valid("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"));
    assert(steam_session_save(root,"short",&a)==3405&&!steam_session_present(root));
    assert(!steam_session_save(root,pass,&a)&&steam_session_present(root));
    snprintf(path,sizeof(path),"%s/steam-session.vault",root);f=fopen(path,"rb");assert(f);n=fread(first,1,sizeof(first),f);fclose(f);
    assert(!contains(first,n,a.refresh_token)&&!contains(first,n,a.account_name)&&!contains(first,n,a.access_token)&&!contains(first,n,pass));
    assert(!steam_session_load(root,pass,&b)&&b.steam_id==a.steam_id&&!strcmp(b.refresh_token,a.refresh_token)&&!b.access_token[0]);
    assert(steam_session_load(root,"wrong local passphrase",&b)==3403&&!b.refresh_token[0]&&!b.steam_id);
    assert(!steam_session_save(root,pass,&a));f=fopen(path,"rb");assert(f);n2=fread(second,1,sizeof(second),f);fclose(f);
    assert(n==n2&&memcmp(first,second,n));
    f=fopen(path,"r+b");assert(f);assert(!fseek(f,48,SEEK_SET));int byte=fgetc(f);assert(byte!=EOF&&!fseek(f,48,SEEK_SET));assert(fputc(byte^1,f)!=EOF&&!fclose(f));
    assert(steam_session_load(root,pass,&b)==3403&&!b.refresh_token[0]);
    assert(!steam_session_forget(root)&&!steam_session_present(root));
    assert(!steam_session_save_automatic(root,&a)&&steam_session_automatic(root));
    assert(!steam_session_load_automatic(root,&b)&&b.steam_id==a.steam_id&&!b.access_token[0]);
    assert(steam_session_load(root,pass,&b)==3402&&!b.refresh_token[0]);
    fixture_device++;
    assert(steam_session_load_automatic(root,&b)==3403&&!b.refresh_token[0]);
    fixture_device--;fixture_available=0;
    assert(steam_session_load_automatic(root,&b)==3404&&!b.refresh_token[0]);
    assert(steam_session_save_automatic(root,&a)==3404&&steam_session_automatic(root));
    fixture_available=1;
    assert(!steam_session_load_automatic(root,&b));
    f=fopen(path,"rb");assert(f);n=fread(first,1,sizeof(first),f);fclose(f);
    assert(first[7]==2&&!contains(first,n,a.refresh_token)&&!contains(first,n,a.account_name)&&!contains(first,n,a.access_token));
    f=fopen(path,"r+b");assert(f);assert(!fseek(f,48,SEEK_SET));byte=fgetc(f);assert(byte!=EOF&&!fseek(f,48,SEEK_SET));assert(fputc(byte^1,f)!=EOF&&!fclose(f));
    assert(steam_session_load_automatic(root,&b)==3403&&!b.refresh_token[0]);
    assert(!steam_session_forget(root)&&!steam_session_present(root));
    /* Reject symlinks; never write a token over another file. */
    assert(!symlink("unrelated",path));assert(steam_session_save(root,pass,&a)==3401);assert(!unlink(path));
    assert(!rmdir(root));steam_auth_clear(&a);steam_auth_clear(&b);
    puts("Authenticated encrypted session roundtrip, randomization, wrong passphrase, tamper, no-plaintext and deletion passed");return 0;
}
