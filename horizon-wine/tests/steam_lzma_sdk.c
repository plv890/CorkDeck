/* Build with the Switch SDK headers but a native liblzma >=5.4.
 * Confirms the production LZMA1EXT path accepts Steam's markerless streams. */
#include "../source/steam/steam_content.c"
#include <assert.h>
static void put32(unsigned char *p,uint32_t n){unsigned i;for(i=0;i<4;++i)p[i]=(unsigned char)(n>>(8*i));}
int main(void){
    unsigned char source[1024],packed[2048],out[1024];size_t i,n;unsigned mode;
    lzma_options_lzma options;lzma_filter filters[2]={{LZMA_FILTER_LZMA1EXT,&options},{LZMA_VLI_UNKNOWN,NULL}};
    for(i=0;i<sizeof(source);++i)source[i]=(unsigned char)(i*7);
    for(mode=0;mode<2;++mode){
        assert(!lzma_lzma_preset(&options,1));options.ext_flags=mode?LZMA_LZMA1EXT_ALLOW_EOPM:0;
        memcpy(packed,"VZa",3);put32(packed+3,0);assert(lzma_properties_encode(filters,packed+7)==LZMA_OK);n=12;
        assert(lzma_raw_buffer_encode(filters,NULL,source,sizeof(source),packed,&n,sizeof(packed)-10)==LZMA_OK);
        put32(packed+n,(uint32_t)crc32(0,source,sizeof(source)));put32(packed+n+4,sizeof(source));memcpy(packed+n+8,"zv",2);n+=10;
        assert(unpack(packed,n,out,sizeof(out),1)&&!memcmp(out,source,sizeof(source)));
        packed[n-10]^=1;assert(!unpack(packed,n,out,sizeof(out),1));
    }
    return 0;
}
