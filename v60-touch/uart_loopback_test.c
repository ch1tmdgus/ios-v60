/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
typedef unsigned long uptr;
static uint32_t regs[0x1000/4],initial[0x1000/4];
static unsigned writes,reads,transfers,waits,clears,cancels,aborts;
static int fault;
static char messages[4096];
enum {OK,TX_TIMEOUT,RX_TIMEOUT,BAD_ECHO,CANCEL_FAIL,ABORT_FAIL,RX_START_FAIL,BAD_COUNT,M_ERROR};
static uint32_t rd(uptr base,unsigned off){
    assert(base==0x998000&&off<0x1000&&!(off%4));reads++;
    if(off==0x780){uint32_t v=regs[off/4];regs[0x804/4]=0;return v;}
    return regs[off/4];
}
static void wr(uptr base,unsigned off,uint32_t value){
    assert(base==0x998000&&off<0x1000&&!(off%4));writes++;
    if(off==0x618||off==0x648){unsigned stat=off-8;assert((value&~regs[stat/4])==0);regs[stat/4]&=~value;clears++;return;}
    if(off==0x604||off==0x634){
        unsigned mask=off==0x604?1:0x1000;
        if(value==4){cancels++;if(fault==CANCEL_FAIL||fault==ABORT_FAIL)return;}
        else {assert(value==2);aborts++;if(fault==ABORT_FAIL)return;}
        regs[0x40/4]&=~mask;return;
    }
    regs[off/4]=value;
    if(off==0x630){assert(value==0x08000080);if(fault!=RX_START_FAIL)regs[0x40/4]|=0x1000;}
    if(off==0x600){assert(value==0x08000000);regs[0x40/4]|=1;}
    if(off==0x700){
        transfers++;
        assert(regs[0x22c/4]==3&&regs[0x25c/4]==2); /* internal + CTS masked */
        assert(regs[0x48/4]==0x11&&regs[0x4c/4]==0x11);
        assert(regs[0x268/4]==8&&regs[0x28c/4]==8&&regs[0x270/4]==4);
        assert(!regs[0xe1c/4]); /* no external interrupt delivery */
        assert(regs[0x260/4]==0x4380e&&regs[0x264/4]==0xc3e0e);
        assert(regs[0x284/4]==0x4380e&&regs[0x288/4]==0xc3e0e);
        if(fault==TX_TIMEOUT)return;
        regs[0x40/4]&=~1u;regs[0x610/4]|=1;
        if(fault==M_ERROR)regs[0x610/4]|=8;
        if(fault==RX_TIMEOUT)return;
        regs[0x780/4]=fault==BAD_ECHO?value^0x80000000:value;
        regs[0x804/4]=fault==BAD_COUNT?2:1;regs[0x640/4]|=1u<<26;
    }
}
static void kernel_delay(unsigned us){assert(us==10);waits++;assert(waits<=10000);}
static int logmsg(const char *fmt,...){
    va_list ap;va_start(ap,fmt);unsigned n=(unsigned)strlen(messages);
    int r=vsnprintf(messages+n,sizeof(messages)-n,fmt,ap);va_end(ap);return r;
}
#include "uart_loopback.h"
static void reset(int f){
    memset(regs,0,sizeof(regs));messages[0]=0;fault=f;writes=reads=transfers=waits=clears=cancels=aborts=0;
    regs[0x68/4]=regs[0x6c/4]=0x203;
    regs[0xe24/4]=0x20102864;regs[0xe28/4]=0x20204800;
    regs[0x48/4]=0x11;regs[0x24/4]=0x5f;regs[0x28/4]=0x18;
    memcpy(initial,regs,sizeof(regs));
}
static void check_restored(void){
    const unsigned exceptions[]={0x600,0x630,0x700,0x780,0x618,0x648};
    for(unsigned off=0;off<0x1000;off+=4){
        int excluded=0;for(unsigned i=0;i<sizeof(exceptions)/sizeof(exceptions[0]);i++)excluded|=off==exceptions[i];
        if(!excluded)assert(regs[off/4]==initial[off/4]);
    }
}
int main(void){
    reset(OK);uart_loopback(0x998000);assert(transfers==2);assert(strstr(messages,"passed=2/2 fail=0 stopped=1"));check_restored();
    for(int f=TX_TIMEOUT;f<=M_ERROR;f++){
        reset(f);uart_loopback(0x998000);assert(waits<=10000);
        if(f==CANCEL_FAIL){assert(aborts);check_restored();}
        else if(f==ABORT_FAIL){assert(strstr(messages,"restore skipped"));assert(!regs[0xe1c/4]);}
        else {assert(!strstr(messages,"passed=2/2 fail=0"));check_restored();}
    }
    const unsigned guard_off[]={0x68,0x6c,0x64,0xe24,0xe28,0x40,0x800,0x804,0x610,0x640,0x258,0xe18};
    const uint32_t guard_val[]={0x303,0x303,1,0x10002864,0x10004800,1,1,1,1,1,1,1};
    for(unsigned i=0;i<sizeof(guard_off)/sizeof(guard_off[0]);i++){
        reset(OK);regs[guard_off[i]/4]=guard_val[i];uart_loopback(0x998000);assert(!writes&&!transfers);
    }
    puts("UART loopback: two patterns, capability/ownership guards, TX/RX timeout, corrupt data, cancel/abort, register restore: PASS");
}
