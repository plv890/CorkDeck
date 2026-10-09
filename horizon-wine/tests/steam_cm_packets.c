/* Offline transport dispatch fixtures; include to exercise private packet parser. */
#include "../source/steam/steam_cm.c"
#include <assert.h>
int main(void)
{
    struct steam_cm c={0};unsigned char legacy[36]={0},reply[64]={0},children[128],multi[512],gzip[256],body[384];
    struct steam_wire_writer h={reply+8,sizeof(reply)-8,0,0},w;z_stream z={0};size_t length;
    put32(legacy,782);packet(&c,legacy,sizeof(legacy),0);assert(!c.error&&c.last_type==782);
    packet(&c,legacy,8,0);assert(c.error==3);c.error=0;
    put32(legacy,757);packet(&c,legacy,sizeof(legacy),0);assert(c.error==4);c.error=0;
    put32(legacy,751);packet(&c,legacy,sizeof(legacy),0);assert(c.error==3);c.error=0;
    put32(legacy,782);
    steam_wire_fixed64(&h,1,UINT64_C(76561198000000000));steam_wire_uint(&h,2,42);
    put32(reply,751u|0x80000000u);put32(reply+4,(uint32_t)h.size);length=8+h.size;
    put32(children,(uint32_t)length);memcpy(children+4,reply,length);
    put32(children+4+length,sizeof(legacy));memcpy(children+8+length,legacy,sizeof(legacy));
    length+=8+sizeof(legacy);
    assert(deflateInit2(&z,Z_DEFAULT_COMPRESSION,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)==Z_OK);
    z.next_in=children;z.avail_in=(uInt)length;z.next_out=gzip;z.avail_out=sizeof(gzip);
    assert(deflate(&z,Z_FINISH)==Z_STREAM_END);
    w=(struct steam_wire_writer){body,sizeof(body),0,0};
    steam_wire_uint(&w,1,length);steam_wire_bytes(&w,2,gzip,z.total_out);deflateEnd(&z);
    put32(multi,1u|0x80000000u);put32(multi+4,0);memcpy(multi+8,body,w.size);
    c.wanted_type=751;packet(&c,multi,8+w.size,0);
    assert(!c.error&&c.reply_size==8+h.size&&c.last_type==782);
    free(c.reply);c=(struct steam_cm){0};put32(multi,1u|0x80000000u);put32(multi+4,0);
    w=(struct steam_wire_writer){multi+8,sizeof(multi)-8,0,0};
    steam_wire_uint(&w,1,STEAM_CM_PACKET_MAX+1);steam_wire_bytes(&w,2,gzip,1);
    packet(&c,multi,8+w.size,0);assert(c.error==3);
    puts("CM legacy notifications and compressed post-logon Multi tests passed");return 0;
}
