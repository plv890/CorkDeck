#include "../source/steam/steam_auth.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void wire_tests(void)
{
    unsigned char data[128];
    struct steam_wire_writer w = {data, sizeof(data), 0, 0};
    struct steam_wire_reader r;
    struct steam_wire_field f;
    const unsigned char expected[] = {0x08,0x96,0x01,0x11,8,7,6,5,4,3,2,1,0x1a,3,'a','b','c'};
    const unsigned char overflow[] = {8,255,255,255,255,255,255,255,255,255,2};
    const unsigned char length[] = {10,255,255,255,255,255,255,255,255,255,1};
    assert(steam_wire_uint(&w, 1, 150));
    assert(steam_wire_fixed64(&w, 2, UINT64_C(0x0102030405060708)));
    assert(steam_wire_bytes(&w, 3, "abc", 3));
    assert(w.size == sizeof(expected) && !memcmp(data, expected, w.size));
    r = (struct steam_wire_reader){data,w.size,0,0};
    assert(steam_wire_next(&r,&f)==1 && f.number==1 && f.integer==150);
    assert(steam_wire_next(&r,&f)==1 && f.type==1 && f.integer==UINT64_C(0x0102030405060708));
    assert(steam_wire_next(&r,&f)==1 && f.size==3 && !memcmp(f.data,"abc",3));
    assert(steam_wire_next(&r,&f)==0);
    r = (struct steam_wire_reader){overflow,sizeof(overflow),0,0};
    assert(steam_wire_next(&r,&f)==-1 && steam_wire_next(&r,&f)==-1);
    r = (struct steam_wire_reader){length,sizeof(length),0,0};
    assert(steam_wire_next(&r,&f)==-1);
    data[0]=0; r=(struct steam_wire_reader){data,1,0,0};
    assert(steam_wire_next(&r,&f)==-1);
    w=(struct steam_wire_writer){data,1,0,0};
    assert(!steam_wire_uint(&w,1,150) && w.failed);
    assert(!steam_wire_bytes(&w,2,"abc",3));
}
static void auth_tests(void)
{
    unsigned char packet[1024], response[1024];
    struct steam_auth s = {0};
    struct steam_cm_message message;
    struct steam_wire_writer w;
    size_t written, i;
    /* Independent literal fixture: client=42, URL, request bytes, float=5 sec. */
    const unsigned char begin[] = {
        8,42,18,21,'h','t','t','p','s',':','/','/','s','.','t','e','a','m','/','q','/','1','/','4','2',
        26,3,1,2,3,37,0,0,160,64
    };
    assert(steam_auth_begin_qr(&s,1,packet,sizeof(packet),&written));
    assert(steam_cm_message_read(packet,written,&message));
    assert(message.type==9804 && message.source_job==1 && message.result==2);
    assert(!steam_auth_poll(&s,2,packet,sizeof(packet),&written));
    message=(struct steam_cm_message){.type=147,.target_job=1,.result=1,.body=begin,.body_size=sizeof(begin)};
    assert(steam_auth_accept(&s,&message));
    assert(s.state==STEAM_AUTH_WAITING && s.poll_interval_ms==5000);
    assert(steam_auth_poll(&s,2,packet,sizeof(packet),&written));
    message.target_job=2; message.body=NULL; message.body_size=0;
    assert(steam_auth_accept(&s,&message) && s.state==STEAM_AUTH_WAITING);
    assert(steam_auth_poll(&s,3,packet,sizeof(packet),&written));
    w=(struct steam_wire_writer){response,sizeof(response),0,0};
    assert(steam_wire_bytes(&w,3,"synthetic-refresh",17));
    assert(steam_wire_bytes(&w,4,"synthetic-access",16));
    assert(steam_wire_bytes(&w,6,"synthetic-account",17));
    message.target_job=3; message.body=response; message.body_size=w.size;
    assert(steam_auth_accept(&s,&message) && s.state==STEAM_AUTH_TOKENS);
    assert(!s.request_size && !s.challenge[0]);
    steam_auth_clear(&s);
    for (i=0;i<sizeof(s);++i) assert(!((unsigned char *)&s)[i]);
    /* Truncation cannot produce a usable auth session. */
    for (i=0;i<sizeof(begin);++i) {
        assert(steam_auth_begin_qr(&s,4,packet,sizeof(packet),&written));
        message.target_job=4; message.body=begin; message.body_size=i;
        assert(!steam_auth_accept(&s,&message) && s.state==STEAM_AUTH_FAILED);
        assert(!s.request_size && !s.refresh_token[0]);
    }
    assert(steam_auth_begin_qr(&s,5,packet,sizeof(packet),&written));
    message.target_job=4; message.body_size=sizeof(begin);
    assert(!steam_auth_accept(&s,&message));
    assert(!steam_cm_message_read(packet,7,&message));
    memset(packet,255,8);
    assert(!steam_cm_message_read(packet,8,&message));
    steam_auth_clear(&s);
}
static void guard_tests(void)
{
    struct steam_auth s={0};
    unsigned char packet[2048];
    char encrypted[345];
    size_t size;
    const unsigned char response[]={8,42,18,3,1,2,3,29,0,0,160,64,34,2,8,3,40,123};
    struct steam_cm_message m={.type=147,.result=1,.target_job=7,.body=response,.body_size=sizeof(response)};
    memset(encrypted,'A',344); encrypted[344]=0;
    assert(!steam_auth_begin_credentials(&s,7,"test","plaintext",123,packet,sizeof(packet),&size));
    assert(steam_auth_begin_credentials(&s,7,"test",encrypted,123,packet,sizeof(packet),&size));
    assert(steam_auth_accept(&s,&m));
    assert(s.steam_id==123 && s.confirmations==(1u<<3) && s.poll_interval_ms==5000);
    assert(!steam_auth_guard(&s,8,2,"ABCDE",packet,sizeof(packet),&size));
    assert(steam_auth_guard(&s,8,3,"ABCDE",packet,sizeof(packet),&size));
    m.target_job=8; m.result=88; m.body=NULL; m.body_size=0;
    assert(steam_auth_accept(&s,&m) && s.state==STEAM_AUTH_WAITING && s.result==88);
    assert(steam_auth_guard(&s,9,3,"12345",packet,sizeof(packet),&size));
    m.target_job=9; m.result=1;
    assert(steam_auth_accept(&s,&m) && s.state==STEAM_AUTH_WAITING);
    assert(steam_auth_poll(&s,10,packet,sizeof(packet),&size));
    m.target_job=10; m.result=27;
    assert(!steam_auth_accept(&s,&m) && s.state==STEAM_AUTH_FAILED && !s.steam_id);
    steam_auth_clear(&s);
}
static void malformed_packets(void)
{
    unsigned char data[256];
    uint32_t random=12345;
    unsigned int n,i;
    for(n=0;n<10000;++n) {
        size_t length=n%sizeof(data);
        struct steam_wire_reader r;
        struct steam_wire_field f;
        struct steam_cm_message message;
        size_t previous=0;
        for(i=0;i<length;++i) { random=random*1664525u+1013904223u; data[i]=(unsigned char)(random>>24); }
        r=(struct steam_wire_reader){data,length,0,0};
        while(steam_wire_next(&r,&f)==1) {
            assert(r.offset>previous && r.offset<=length);
            previous=r.offset;
        }
        (void)steam_cm_message_read(data,length,&message);
    }
}
int main(void)
{
    wire_tests(); auth_tests(); guard_tests(); malformed_packets();
    puts("Steam wire/CM/auth fixture tests passed (no network or real credentials).");
    return 0;
}
