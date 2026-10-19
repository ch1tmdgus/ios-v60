/* SPDX-License-Identifier: GPL-2.0-only
 * UART FIFO transport self-test, based on msm_geni_serial.c/qcom-geni-se.c.
 * Only SE6-internal registers; no pinmux, BT_EN, radio, PMIC, DMA or HCI.
 * TX/RX use the same inherited root clock. No external baud is asserted.
 * SE_IRQ_EN stays0; sequencer status enables are local, no GIC IRQ delivery.
 */
#pragma once
static int uart_wait(uptr se,unsigned offset,uint32_t mask,int set) {
    for(unsigned i=0;i<2000;i++) {
        if(!!(rd(se,offset)&mask)==set)return 0;
        kernel_delay(10);
    }
    return -1;
}
static int uart_stop(uptr se,unsigned status_mask,unsigned ctrl) {
    if(!(rd(se,0x40)&status_mask))return 0;
    wr(se,ctrl,4); /* cancel only the command owned by this test */
    if(!uart_wait(se,0x40,status_mask,0))return 0;
    wr(se,ctrl,2); /* bounded abort fallback */
    return uart_wait(se,0x40,status_mask,0);
}
static void uart_loopback(uptr se) {
    uint32_t fw=rd(se,0x68),sfw=rd(se,0x6c),tx=rd(se,0xe24),rx=rd(se,0xe28);
    if(((fw>>8)&255)!=2||((sfw>>8)&255)!=2||(rd(se,0x64)&1)||
       ((tx>>24)&63)!=32||((rx>>24)&63)!=32||!((tx>>16)&63)||!((rx>>16)&63)) {
        logmsg("V60 WIRELESS: loopback rejected: UART/FIFO capability\n");return;
    }
    if(rd(se,0x40)&0x1001||rd(se,0x800)||rd(se,0x804)||rd(se,0x610)||rd(se,0x640)||rd(se,0x258)||rd(se,0xe18)) {
        logmsg("V60 WIRELESS: loopback rejected: busy/dirty/DMA\n");return;
    }
    /* Save all configuration changed below; exclude command/FIFO/IRQ W1C.
     * Packing 8bit x4: [0x0e,0x10e,0x20e,0x30f] in10-bit slots.
     */
    static const unsigned offsets[]={0x20,0x24,0x28,0x48,0x4c,0x22c,
        0x25c,0x268,0x26c,0x270,0x280,0x28c,0x294,0x2a4,0x2a8,0x2ac,
        0x260,0x264,0x284,0x288,0x80c,0x810,0x814,0x614,0x644,0xe1c};
    uint32_t saved[sizeof(offsets)/sizeof(offsets[0])];
    for(unsigned i=0;i<sizeof(saved)/sizeof(saved[0]);i++)saved[i]=rd(se,offsets[i]);
    /* Mask SE output IRQs while polling. No IRQ-controller manipulation. */
    wr(se,0xe1c,0);wr(se,0x614,0x3000003f);wr(se,0x644,0x0f00003f);
    wr(se,0x22c,3); /* TX/RX + CTS/RFR INTERNAL loopback, pads stay GPIO. */
    wr(se,0x28,rd(se,0x28)|0x7f);wr(se,0x24,0x7f);wr(se,0x20,1);
    wr(se,0x48,0x11);wr(se,0x4c,0x11); /* equal enable/div1, shared clock */
    wr(se,0x25c,2);wr(se,0x280,0); /* mask external CTS, no parity/status */
    wr(se,0x268,8);wr(se,0x28c,8);wr(se,0x26c,0);
    wr(se,0x2a4,0);wr(se,0x2a8,0);wr(se,0x2ac,0);
    wr(se,0x294,160);wr(se,0x80c,1);wr(se,0x810,0);wr(se,0x814,1);
    wr(se,0x260,0x4380e);wr(se,0x264,0xc3e0e);
    wr(se,0x284,0x4380e);wr(se,0x288,0xc3e0e);
    /* Two distinct 4-byte transfers. A canned/stale FIFO value cannot pass. */
    unsigned passed=0;uint32_t received=0;int failure=0;
    wr(se,0x630,0x08000080); /* START_READ | RFR_OPEN */
    if(uart_wait(se,0x40,0x1000,1))failure=1;
    for(unsigned n=0;!failure&&n<2;n++) {
        uint32_t pattern=n?0xc3695a96u:0x963ca569u;
        wr(se,0x270,4);wr(se,0x600,0x08000000);wr(se,0x700,pattern);
        if(uart_wait(se,0x610,1,1)){failure=2;break;}
        if(uart_wait(se,0x804,0x1ffffff,1)){failure=3;break;}
        uint32_t fifo=rd(se,0x804),m=rd(se,0x610),s=rd(se,0x640);
        received=rd(se,0x780); /* exactly one word belonging to this test */
        logmsg("V60 WIRELESS: loopback n=%u tx=%x rx=%x fifo=%x irq=%x/%x\n",n,pattern,received,fifo,m,s);
        if((fifo&0x1ffffff)!=1||received!=pattern||(m&0x3000000e)||(s&0x300000e)){failure=4;break;}
        wr(se,0x618,m);wr(se,0x648,s);passed++;
    }
    int stuck=uart_stop(se,1,0x604)|uart_stop(se,0x1000,0x634);
    logmsg("V60 WIRELESS: loopback passed=%u/2 fail=%d stopped=%u status=%x\n",passed,failure,!stuck,rd(se,0x40));
    if(stuck){logmsg("V60 WIRELESS: restore skipped: sequencer still active\n");return;}
    /* Clear only test-produced statuses (entry required both clear). */
    wr(se,0x618,rd(se,0x610));wr(se,0x648,rd(se,0x640));
    /* Any unread data from our transfer is bounded by the hardware depth. */
    for(unsigned i=0;i<((rx>>16)&63)&&(rd(se,0x804)&0x1ffffff);i++)(void)rd(se,0x780);
    /* Restore IRQ enables last, after engine idle and other settings restored. */
    for(unsigned i=0;i<sizeof(saved)/sizeof(saved[0]);i++)wr(se,offsets[i],saved[i]);
    logmsg("V60 WIRELESS: loopback restored; no external HCI sent\n");
}
