/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
typedef unsigned long uptr;
static uint32_t rsc[64],tcs[0x3000/4],db[0x20000/4];
static unsigned writes,transactions,delays,triggered;
static int fault;
static const uint32_t addresses[]={0x40100,0x40200,0x40300};
static const unsigned voltages[]={950,1900,1350};
enum {OK,NO_COMPLETION,STALE_COMPLETION,FAIL_SECOND,WRITE_FAIL,MAP_FAIL};
static uptr kernel_io_map(uptr base,uptr size){
    assert((base==0x18220000&&size==0x100)||(base==0x18220d00&&size==0x3000)||(base==0x80860000&&size==0x20000));
    return fault==MAP_FAIL?0:base;
}
static uint32_t rd(uptr base,unsigned off){
    assert(!(off%4));
    if(base==0x18220000){assert(off<sizeof(rsc));return rsc[off/4];}
    if(base==0x18220d00){assert(off<sizeof(tcs));return tcs[off/4];}
    assert(base==0x80860000&&off<sizeof(db));return db[off/4];
}
static void wr(uptr base,unsigned off,uint32_t value){
    assert(base==0x18220d00&&off<0x540&&!(off%4));writes++;
    /* This model always selects ACTIVE TCS0. No sleep/wake writes allowed. */
    assert(off<0x2a0);
    if(fault==WRITE_FAIL&&off==0x34)return;
    if(off==8){assert(value==1);tcs[1]&=~value;return;}
    tcs[off/4]=value;
    if(off==0x14&&value==0x1010000){
        unsigned i=transactions++;assert(i<3);
        assert(tcs[0x30/4]==0x10108&&tcs[0x44/4]==0x10108);
        assert(tcs[0x34/4]==addresses[i]&&tcs[0x48/4]==addresses[i]+4);
        assert(tcs[0x38/4]==voltages[i]&&tcs[0x4c/4]==1);
        assert(tcs[0x10/4]==3&&tcs[0x1c/4]==3);
        assert(!tcs[0x58/4]); /* never program mode/headroom/third command */
        assert(tcs[0]==(0xa5u&~1u));
        triggered=1;
        if(fault==STALE_COMPLETION){tcs[0x3c/4]=tcs[0x50/4]=0x10100;return;}
        tcs[0x18/4]=0;
    }
}
static void kernel_delay(unsigned us){
    assert(us==1);delays++;assert(delays<=101000);
    if(triggered&&fault!=NO_COMPLETION&&fault!=STALE_COMPLETION&&!(fault==FAIL_SECOND&&transactions==2)){
        tcs[0x18/4]=1;tcs[0x3c/4]=tcs[0x50/4]=0x10100;tcs[1]|=1;triggered=0;
    }
}
static void kernel_sleep(unsigned ms){assert(ms==50);}
static int logmsg(const char *format,...){(void)format;return 0;}
#include "bt_power.h"
static void setup(int f){
    memset(rsc,0,sizeof(rsc));memset(tcs,0,sizeof(tcs));memset(db,0,sizeof(db));
    fault=f;writes=transactions=delays=triggered=0;
    rsc[0]=0x20000;rsc[3]=(8u<<12)|(16u<<27);
    tcs[0]=0xa5;tcs[0x18/4]=tcs[(0x2a0+0x18)/4]=1;
    db[0]=1;db[1]=0x0c0330db;
    /* VRM header: entries offset0, aux offset72,3 entries. */
    uint8_t *bytes=(uint8_t*)db;bytes[8]=4;bytes[12]=72;bytes[14]=3;
    const char *names[]={"smpa6","smpa5","smpc8"};
    for(unsigned i=0;i<3;i++){unsigned off=144+i*24;memcpy(bytes+off,names[i],5);db[(off+16)/4]=addresses[i];}
}
int main(void){
    setup(OK);assert(!bt_power());assert(transactions==3&&tcs[0]==0xa5&&!tcs[1]&&!tcs[0x14/4]&&!tcs[0x1c/4]&&!tcs[0x10/4]);
    for(int f=NO_COMPLETION;f<=WRITE_FAIL;f++){
        setup(f);assert(bt_power());assert(transactions==(f==FAIL_SECOND?2u:f==WRITE_FAIL?0u:1u));
        assert(tcs[0]==(0xa5u&~1u)); /* quarantine, own IRQ remains masked */
    }
    setup(MAP_FAIL);assert(bt_power()&&!writes);
    setup(OK);db[1]=0;assert(bt_power()&&!writes);
    setup(OK);db[35]=1;assert(bt_power()&&!writes); /* standalone database */
    setup(OK);db[(144+24+16)/4]=addresses[0];assert(bt_power()&&!writes);
    setup(OK);((uint8_t*)db)[144]='x';assert(bt_power()&&!writes);
    setup(OK);((uint8_t*)db)[10]=0xff;((uint8_t*)db)[11]=0xff;assert(bt_power()&&!writes);
    setup(OK);rsc[0]=0x30000;assert(bt_power()&&!writes);
    setup(OK);tcs[0x14/4]=tcs[(0x2a0+0x14)/4]=1;assert(bt_power()&&!writes);
    setup(OK);tcs[0x1c/4]=1;tcs[0x34/4]=addresses[2]+4;assert(bt_power()&&!writes);
    puts("RPMh power: exact three votes, no mode writes, DB/geometry guards, busy/conflict, timeout quarantine, no retry: PASS");
}
