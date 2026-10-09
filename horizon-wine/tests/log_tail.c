#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../source/diagnostics/log_tail.h"
int main(void)
{
    struct wine_nx_log_tail tail = {0};
    unsigned char input[WINE_NX_LOG_TAIL_CAPACITY + 100], output[WINE_NX_LOG_TAIL_CAPACITY];
    size_t i;
    assert(wine_nx_log_tail_copy(&tail, output, sizeof(output)) == 0);
    for (i=0; i<sizeof(input); i++) input[i]=(unsigned char)(i*17);
    wine_nx_log_tail_append(&tail, input, 100);
    wine_nx_log_tail_append(&tail, input+100, sizeof(input)-100);
    assert(wine_nx_log_tail_copy(&tail, output, sizeof(output)) == sizeof(output));
    assert(!memcmp(output, input+100, sizeof(output)));
    assert(wine_nx_log_tail_copy(&tail, output, 63) == 63);
    assert(!memcmp(output, input+sizeof(input)-63, 63));
    memset(&tail,0,sizeof(tail));
    wine_nx_log_tail_append(&tail, input, sizeof(input));
    assert(wine_nx_log_tail_copy(&tail, output, sizeof(output)) == sizeof(output));
    assert(!memcmp(output, input+100, sizeof(output)));
    assert(tail.sequence == 1);
    wine_nx_log_tail_append(&tail, input, 0);
    assert(tail.sequence == 1);
    return 0;
}
