/* SPDX-License-Identifier: GPL-2.0-only
 * Phase1: QUP0/SE6 clock enable + UART/pin inventory.
 * Phase2: same clock gate, then owned internal UART FIFO loopback.
 * Source: Rofik gcc-kona.c, pinctrl-kona.c, qcom-geni-se.h at7eab1a7.
 * Never access SE12 console, PMIC, PCIe, HCI or radio reset.
 * Phase1 does not read RX FIFO data; phase2 reads only its own test bytes.
 */
#include "wireless_policy.h"
#if V60_WIRELESS_PROBE == 2
#include "uart_loopback.h"
#endif
static void wireless_worker(void *arg,int result) {
    (void)arg;(void)result;
    kernel_sleep(90000); /* Touch worker initializes at60s; independent thread. */
#if V60_WIRELESS_PROBE == 1
    logmsg("V60 WIRELESS: wireless1 QCA6390 clock/UART probe begin\n");
#else
    logmsg("V60 WIRELESS: wireless2 UART loopback begin\n");
#endif
    uptr gcc=kernel_io_map(0x100000,0x60000);
#if V60_WIRELESS_PROBE == 1
    uptr north=kernel_io_map(0xf900000,0x20000);
    uptr swctrl=kernel_io_map(0xf97c000,0x1000);
    if(!gcc||!north||!swctrl){logmsg("V60 WIRELESS: map failed; stop\n");goto done;}
    for(unsigned id=16;id<=21;id++)logmsg("V60 WIRELESS: GPIO%u cfg=%x io=%x\n",id,rd(north,id*0x1000),rd(north,id*0x1000+4));
    logmsg("V60 WIRELESS: GPIO124 cfg=%x io=%x\n",rd(swctrl,0),rd(swctrl,4));
#else
    if(!gcc){logmsg("V60 WIRELESS: GCC map failed\n");goto done;}
#endif
    /* Enable only wrap0 AHB/core/core2x and SE6 branch votes. Preserve others.
     * No RCGR rate/source/reset writes: inherited source must already work.
     */
    uint32_t before=rd(gcc,0x52008),mask=(15u<<6)|(1u<<16);
    wr(gcc,0x52008,before|mask);
    uint32_t after=rd(gcc,0x52008);
    logmsg("V60 WIRELESS: GCC vote before=%x after=%x mask=%x\n",before,after,mask);
    if((after&mask)!=mask){logmsg("V60 WIRELESS: clock vote rejected; stop\n");goto done;}
    const unsigned branches[]={0x17004,0x17008,0x23000,0x23008,0x1772c};
    for(unsigned i=0;i<5;i++) {
        unsigned o=branches[i];uint32_t v=rd(gcc,o);unsigned tries=0;
        while(!v60_wireless_clock_ready(o,v) && tries++<5000){kernel_delay(1);v=rd(gcc,o);}
        logmsg("V60 WIRELESS: branch=%x value=%x ready=%u\n",o,v,v60_wireless_clock_ready(o,v));
        if(!v60_wireless_clock_ready(o,v)){logmsg("V60 WIRELESS: SE6 access skipped: clock halted\n");goto done;}
    }
#if V60_WIRELESS_PROBE == 1
    logmsg("V60 WIRELESS: SE6 RCGR=%x cfg=%x M=%x N=%x D=%x DFS=%x\n",
        rd(gcc,0x17730),rd(gcc,0x17734),rd(gcc,0x17738),rd(gcc,0x1773c),rd(gcc,0x17740),rd(gcc,0x17744));
#endif
    uptr se=kernel_io_map(0x998000,0x4000);
    if(!se){logmsg("V60 WIRELESS: SE6 mapping failed\n");goto done;}
#if V60_WIRELESS_PROBE == 1
    logmsg("V60 WIRELESS: SE6 fw=%x sfw=%x status=%x FIFO_DISABLE=%x\n",rd(se,0x68),rd(se,0x6c),rd(se,0x40),rd(se,0x64));
    logmsg("V60 WIRELESS: SE6 M_CLK=%x S_CLK=%x IOS=%x DMA=%x\n",rd(se,0x48),rd(se,0x4c),rd(se,0x908),rd(se,0x258));
    logmsg("V60 WIRELESS: SE6 TXparam=%x RXparam=%x TXstatus=%x RXstatus=%x\n",rd(se,0xe24),rd(se,0xe28),rd(se,0x800),rd(se,0x804));
    logmsg("V60 WIRELESS: SE6 M_IRQ=%x S_IRQ=%x; no HCI or firmware transfer\n",rd(se,0x610),rd(se,0x640));
#else
    uart_loopback(se);
#endif
done:
#if V60_WIRELESS_PROBE == 1
    logmsg("V60 WIRELESS: wireless1 probe complete; Wi-Fi/BT not enabled\n");
#else
    logmsg("V60 WIRELESS: wireless2 complete; Wi-Fi/BT not enabled\n");
#endif
    kernel_thread_terminate(kernel_current_thread());
    for(;;)kernel_sleep(1000);
}
