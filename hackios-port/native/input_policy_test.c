#include <assert.h>
#include <stdio.h>
#include "input_policy.h"
int main(void)
{
    assert(v60_normalize(-1,1080)==0 && v60_normalize(0,1080)==0);
    assert(v60_normalize(1079,1080)==65535 && v60_normalize(1080,1080)==65535);
    assert(v60_normalize(2459,2460)==65535 && v60_normalize(2460,2460)==65535);
    uint32_t last=0;
    for(int y=0;y<2460;y++){uint32_t v=v60_normalize(y,2460);assert(v>=last && v<=65535);last=v;}
    struct v60_key_state s={0};
    for(int i=0;i<100;i++)assert(v60_key_sample(&s,1)==-1); /* held at boot */
    assert(v60_key_sample(&s,0)==-1);
    assert(v60_key_sample(&s,1)==-1); /* bounce */
    assert(v60_key_sample(&s,0)==-1);
    assert(v60_key_sample(&s,0)==-1);
    assert(v60_key_sample(&s,-1)==-1); /* no false release on bus error */
    assert(v60_key_sample(&s,1)==-1);
    assert(v60_key_sample(&s,1)==1);
    assert(v60_key_sample(&s,1)==-1);
    assert(v60_key_sample(&s,0)==-1);
    assert(v60_key_sample(&s,0)==0);
    puts("V60 native coordinates, startup-held suppression, debounce and bus-error handling: PASS");
}
