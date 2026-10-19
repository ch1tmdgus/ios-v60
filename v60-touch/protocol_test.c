#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "protocol.h"
static void put(uint8_t *p,uint32_t v){for(int i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void point(uint8_t *b,int slot,unsigned id,unsigned ev,unsigned x,unsigned y){put(b+36+12*slot,id|(ev<<16)|(x<<18));put(b+40+12*slot,y);}
int main(void){
 uint8_t b[V60_REPORT_SIZE]={0};struct v60_frame f={0};
 assert(v60_decode(b,sizeof(b),&f)==0 && f.mask==0);
 put(b+8,1<<8);point(b,0,2,1,1079,2459);
 assert(v60_decode(b,sizeof(b),&f)==0 && f.mask==4 && f.x[2]==1079 && f.y[2]==2459);
 point(b,0,2,2,350,800);assert(v60_decode(b,sizeof(b),&f)==0 && f.x[2]==350 && f.y[2]==800);
 point(b,0,2,3,350,800);assert(v60_decode(b,sizeof(b),&f)==0 && f.mask==0);
 put(b+8,2<<8);point(b,0,0,1,0,0);point(b,1,9,2,100,200);
 assert(v60_decode(b,sizeof(b),&f)==0 && f.mask==513);
 point(b,1,0,2,100,200);assert(v60_decode(b,sizeof(b),&f)==-1);
 point(b,1,10,2,100,200);assert(v60_decode(b,sizeof(b),&f)==-1);
 put(b+8,1<<8);point(b,0,0,1,1081,0);assert(v60_decode(b,sizeof(b),&f)==-1);
 point(b,0,0,1,0,2461);assert(v60_decode(b,sizeof(b),&f)==-1);
 point(b,0,0,1,1080,2460);assert(v60_decode(b,sizeof(b),&f)==0 && f.x[0]==1079 && f.y[0]==2459);
 put(b+8,11<<8);assert(v60_decode(b,sizeof(b),&f)==-1);
 put(b+8,1);assert(v60_decode(b,sizeof(b),&f)==1);
 put(b+8,0x80000100);assert(v60_decode(b,sizeof(b),&f)==0 && f.mask==0);
 put(b+8,1<<8);point(b,0,15,1,0,0);assert(v60_decode(b,sizeof(b),&f)==0 && f.mask==0);
 assert(v60_decode(b,sizeof(b)-1,&f)==-1);
 puts("SW42902 packet decoder: 15 checks PASS");return 0;
}
