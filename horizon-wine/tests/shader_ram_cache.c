#include "../source/shader_ram_cache.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static void *stress(void *arg)
{
    unsigned i; unsigned char key[32]={0}, data[8]={0};
    for(i=0;i<2000;i++) {
        key[0]=i%32;data[0]=i%255;
        wine_nx_shader_ram_put(arg,key,32,data,8);
        size_t size;void *p=wine_nx_shader_ram_get(arg,key,32,&size);
        if(p) {assert(size==8);free(p);}
    }
    return NULL;
}
int main(void)
{
    char owner1,owner2;unsigned char a[32]={1},b[32]={2},c[32]={3};size_t size;
    wine_nx_shader_ram_configure(6);
    wine_nx_shader_ram_put(&owner1,a,32,"abcd",4);
    wine_nx_shader_ram_put(&owner1,b,32,"ef",2);
    char *p=wine_nx_shader_ram_get(&owner1,a,32,&size);assert(p && size==4 && !memcmp(p,"abcd",4));
    p[0]='!';free(p);
    wine_nx_shader_ram_put(&owner1,c,32,"gh",2);
    assert(!wine_nx_shader_ram_get(&owner1,b,32,&size));
    p=wine_nx_shader_ram_get(&owner1,a,32,NULL);assert(p && p[0]=='a');free(p);
    assert(!wine_nx_shader_ram_get(&owner2,a,32,&size));
    p=wine_nx_shader_ram_get(&owner1,a,32,&size);assert(p);
    assert(wine_nx_shader_ram_reclaim(1)==2); /* c is least recently used */
    assert(!wine_nx_shader_ram_get(&owner1,c,32,&size));
    assert(wine_nx_shader_ram_reclaim(1024)==4);
    assert(!memcmp(p,"abcd",4));free(p); /* callers' copies survive reclaim */
    assert(!wine_nx_shader_ram_reclaim(1));
    assert(wine_nx_shader_ram_stats().budget==6);
    wine_nx_shader_ram_forget(&owner1);assert(wine_nx_shader_ram_stats().bytes==0);
    assert(!wine_nx_shader_ram_get(&owner1,a,32,&size));
    wine_nx_shader_ram_put(&owner1,a,32,"oversize",8);assert(wine_nx_shader_ram_stats().bytes==0);
    wine_nx_shader_ram_configure(1024);
    pthread_t threads[4];unsigned i;
    for(i=0;i<4;i++) assert(!pthread_create(threads+i,NULL,stress,i&1 ? &owner1 : &owner2));
    for(i=0;i<4;i++) pthread_join(threads[i],NULL);
    assert(wine_nx_shader_ram_stats().bytes<=1024);
    wine_nx_shader_ram_configure(0);
    wine_nx_shader_ram_put(&owner1,a,32,"abcd",4);
    assert(!wine_nx_shader_ram_get(&owner1,a,32,&size));
    assert(wine_nx_shader_ram_stats().bytes==0);
    puts("shader RAM cache: ownership, namespaces, LRU, byte cap, destruction, concurrency and disable passed");
}
