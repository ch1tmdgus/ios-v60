#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "clock_policy.h"
#include "engine_policy.h"
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
 puts("SW42902 packet decoder: 15 checks PASS");
 assert(v60_clock_ready(0x18008,0x80000002)); /* observed on V60 */
 assert(v60_clock_hwcg(0x18008,0x80000002));
 assert(v60_clock_ready(0x18008,2));
 assert(v60_clock_ready(0x18008,0));
 assert(!v60_clock_ready(0x18008,0x80000000));
 assert(!v60_clock_ready(0x18008,0x80000001));
 assert(!v60_clock_ready(0x18004,0x80000002));
 assert(!v60_clock_ready(0x23138,0x80000002));
 assert(!v60_clock_ready(0x23140,0x80000002));
 assert(!v60_clock_ready(0x185fc,0x80000002));
 assert(v60_clock_ready(0x185fc,0));
 puts("Clock gating policy: 11 checks PASS");
 const uint32_t tx=0x20102864,rx=0x20204800;
 assert(v60_fifo_width(tx)==32 && v60_fifo_depth(tx)==16);
 assert(v60_fifo_width(rx)==32 && v60_fifo_depth(rx)==32);
 assert(v60_engine_check(0x303,0,0,tx,rx)==V60_ENGINE_OK);
 assert(v60_engine_check(0x303,1,0,tx,rx)==V60_ENGINE_GSI);
 assert(v60_engine_check(0x303,1,1,tx,rx)==V60_ENGINE_GSI);
 assert(v60_engine_check(0x203,0,0,tx,rx)==V60_ENGINE_PROTOCOL);
 assert(v60_engine_check(0x303,0,0,0x10100000,rx)==V60_ENGINE_WIDTH);
 assert(v60_engine_check(0x303,0,0,tx,0x20010000)==V60_ENGINE_DEPTH);
 assert(v60_engine_check(0x303,0,1,tx,rx)==V60_ENGINE_BUSY);
 assert(v60_engine_check(0x303,0,0x1000,tx,rx)==V60_ENGINE_BUSY);
 assert(v60_engine_check(0x303,0,0x100,tx,rx)==V60_ENGINE_OK);
 puts("GENI capability policy: 11 checks PASS");return 0;
}
