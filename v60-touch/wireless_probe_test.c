/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>
typedef unsigned long uptr;
static jmp_buf finish;
static uint32_t votes;
static unsigned blocked,uart_reads,writes,delays,terminated;
static int reject,map_fail;
static uptr kernel_io_map(uptr base,uptr length) {
    assert((base==0x100000&&length==0x60000)||(base==0xf900000&&length==0x20000)||(base==0xf97c000&&length==0x1000)||(base==0x998000&&length==0x4000));
    return map_fail?0:base;
}
static uint32_t rd(uptr base,unsigned offset) {
    if(base==0x998000){uart_reads++;assert(!blocked);return 0;}
    if(base==0x100000){
        if(offset==0x52008)return votes;
        if(offset==blocked)return 0x80000000;
        /* HWCG permits S_AHB status HALT while gated. */
        if(offset==0x17008)return 0x80000002;
        return 0;
    }
    assert(base==0xf900000||base==0xf97c000);return 0;
}
static void wr(uptr base,unsigned offset,uint32_t value) {
    assert(base==0x100000&&offset==0x52008);writes++;
    if(!reject)votes=value;
}
static int logmsg(const char *format,...) {(void)format;return 0;}
static void kernel_sleep(unsigned ms){assert(ms==90000);}
static void kernel_delay(unsigned us){assert(us==1);delays++;}
static uptr kernel_current_thread(void){return 1;}
static int kernel_thread_terminate(uptr t){assert(t==1);terminated++;longjmp(finish,1);return 0;}
#define V60_WIRELESS_PROBE 1
#include "wireless_probe.h"
static void run(unsigned halt,int rejected,int failed) {
    votes=0x0c3c0040;blocked=halt;reject=rejected;map_fail=failed;
    uart_reads=writes=delays=terminated=0;
    if(!setjmp(finish))wireless_worker(0,0);
    assert(terminated==1);
    assert((votes&0x0c3c0040)==0x0c3c0040);
    if(halt||rejected||failed)assert(!uart_reads);
    else assert(uart_reads==14);
    if(halt)assert(delays==5000);
    assert(writes==(failed?0u:1u));
}
int main(void) {
    assert(v60_wireless_clock_ready(0x17008,0x80000002));
    assert(!v60_wireless_clock_ready(0x17004,0x80000002));
    assert(!v60_wireless_clock_ready(0x1772c,0x80000002));
    run(0,0,0);run(0,1,0);run(0,0,1);
    const unsigned offsets[]={0x17004,0x17008,0x23000,0x23008,0x1772c};
    for(unsigned i=0;i<5;i++)run(offsets[i],0,0);
    puts("Wireless probe: success, HALT gates, map/vote failure, bounded timeout, writes limited to GCC vote: PASS");
}
