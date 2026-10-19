/* SPDX-License-Identifier: GPL-2.0-only
 * Bounded, one-shot QCA EDL ROM version query. No firmware download or stack.
 * Rofik msm_geni_serial.c: GPIO16CTS,17RTS,18TX,19RX qup6 NORTH;
 * baud115200,16x sampling (recovery config verified),7372800Hz/div4.
 */
#include "uart_loopback.h"
#include "bt_hci.h"
#ifndef V60_BT_POWER_EXTERNAL
#include "bt_power.h"
#endif
static int bt_root(uptr gcc,const uint32_t values[4]) {
    for(unsigned i=0;i<3;i++){unsigned o=0x17738+i*4;wr(gcc,o,(rd(gcc,o)&0xffff0000u)|(values[i+1]&65535));}
    wr(gcc,0x17734,values[0]);wr(gcc,0x17730,rd(gcc,0x17730)|1);
    for(unsigned i=0;i<500;i++){if(!(rd(gcc,0x17730)&1))return 0;kernel_delay(1);}
    return -1;
}
static void bt_uart(uptr gcc,uptr se) {
    if(((rd(se,0x68)>>8)&255)!=2||((rd(se,0x6c)>>8)&255)!=2||rd(se,0x64)&1||
       rd(se,0x40)&0x1001||rd(se,0x800)||rd(se,0x804)||rd(se,0x610)||rd(se,0x640)||rd(se,0x258)||rd(se,0xe18)) {
        logmsg("V60 WIRELESS: HCI UART busy\n");return;
    }
    uint32_t tx=rd(se,0xe24),rx=rd(se,0xe28);
    if(((tx>>24)&63)!=32||((rx>>24)&63)!=32||((tx>>16)&63)<2||((rx>>16)&63)<2)return;
    /* Check inherited GPLL0=600MHz and even output/div2 before using it.
     * Never reprogram PLL/voltage or disable DFS while owned by hardware.
     */
    uint32_t mode=rd(gcc,0),l=rd(gcc,4),alpha=rd(gcc,0x40),user=rd(gcc,12),dfs=rd(gcc,0x17744);
    logmsg("V60 WIRELESS: PLL0=%x/%x/%x/%x DFS=%x\n",mode,l,alpha,user,dfs);
    if(mode==0xffffffffu||!(mode&0x80000000u)||l!=31||(alpha&65535)!=0x4000||((user>>8)&15)!=1||(dfs&1)) {
        logmsg("V60 WIRELESS: HCI clock rejected\n");return;
    }
    uptr north=kernel_io_map(0xf900000,0x20000);
    if(!north)return;
    /* Refuse UART pins already owned by another function, or asserted BT_EN. */
    for(unsigned id=16;id<=19;id++)if(rd(north,id*0x1000)&0x23c)return;
    if((rd(north,0x15000)&0x3c)||(rd(north,0x15004)&3)){logmsg("V60 WIRELESS: BT_EN owned\n");return;}
    if(bt_power()){logmsg("V60 WIRELESS: power failed; no BT_EN/HCI\n");return;}
    static const unsigned offsets[]={0x20,0x24,0x28,0x48,0x4c,0x22c,0x25c,0x268,0x26c,0x270,0x280,0x28c,0x294,0x2a4,0x2a8,0x2ac,0x260,0x264,0x284,0x288,0x80c,0x810,0x814,0x614,0x644,0xe1c};
    uint32_t saved[sizeof(offsets)/sizeof(offsets[0])],pins[4],reset_cfg=rd(north,0x15000),reset_out=rd(north,0x15004)&2;
    uint32_t root[4]={rd(gcc,0x17734),rd(gcc,0x17738),rd(gcc,0x1773c),rd(gcc,0x17740)};
    for(unsigned i=0;i<sizeof(saved)/sizeof(saved[0]);i++)saved[i]=rd(se,offsets[i]);
    for(unsigned i=0;i<4;i++)pins[i]=rd(north,(16+i)*0x1000);
    uint32_t clock[4]={(root[0]&~0x371fu)|0x2601,384,(~(15625-384))&65535,(~15625u)&65535};
    if(bt_root(gcc,clock)){logmsg("V60 WIRELESS: RCGR update failed\n");return;}
    wr(se,0xe1c,0);wr(se,0x614,0x3000003f);wr(se,0x644,0x0f00003f);
    wr(se,0x22c,0);wr(se,0x28,rd(se,0x28)|0x7f);wr(se,0x24,0x7f);wr(se,0x20,1);
    wr(se,0x48,0x41);wr(se,0x4c,0x41);wr(se,0x25c,0);wr(se,0x280,0);
    wr(se,0x268,8);wr(se,0x28c,8);wr(se,0x26c,0);wr(se,0x2a4,0);wr(se,0x2a8,0);wr(se,0x2ac,0);
    wr(se,0x294,160);wr(se,0x80c,1);wr(se,0x810,0);wr(se,0x814,((rx>>16)&63)-2);
    wr(se,0x260,0x4380e);wr(se,0x264,0xc3e0e);wr(se,0x284,0x4380e);wr(se,0x288,0xc3e0e);
    for(unsigned i=0;i<4;i++)wr(north,(16+i)*0x1000,(pins[i]&~0x3ffu)|4|(i==1?1:i==2?3:0));
    /* Vendor power order: reset low50ms, then high50ms after supplies. */
    wr(north,0x15004,rd(north,0x15004)&~2u);wr(north,0x15000,(reset_cfg&~0x3ffu)|0x200);kernel_sleep(50);
    wr(north,0x15004,rd(north,0x15004)|2);kernel_sleep(50);
    logmsg("V60 WIRELESS: BT_EN=%x IOS=%x baud=115200 flow=CTS/RTS\n",rd(north,0x15004),rd(se,0x908));
    uint8_t data[64]={0};unsigned used=0;uint32_t version[4]={0};int result=-1;
    if(!(rd(north,0x15004)&1))goto cleanup;
    wr(se,0x630,0x08000080);
    if(uart_wait(se,0x40,0x1000,1))goto cleanup;
    /* H4 command [01,00,fc,01,19]: EDL_PATCH_VER_REQ; no MAC query. */
    wr(se,0x270,5);wr(se,0x600,0x08000000);wr(se,0x700,0x01fc0001);wr(se,0x700,0x19);
    for(unsigned ms=0;ms<2000;ms++) {
        if((rd(se,0x610)&0x3000000e)||(rd(se,0x640)&0x03001e0e)){result=-3;break;}
        uint32_t fifo=rd(se,0x804);unsigned words=fifo&0x1ffffff;
        if(words>((rx>>16)&63)){result=-2;break;}
        for(unsigned i=0;i<words;i++) {
            unsigned bytes=4;if(i==words-1&&(fifo>>31)&&((fifo>>28)&7))bytes=(fifo>>28)&7;
            if(bytes>4||used+bytes>sizeof(data)){result=-2;goto cleanup;}
            uint32_t word=rd(se,0x780);
            for(unsigned j=0;j<bytes;j++)data[used++]=(uint8_t)(word>>(8*j));
        }
        result=bt_hci_version(data,used,version);
        if(result)break;
        kernel_sleep(1);
    }
cleanup:
    logmsg("V60 WIRELESS: HCI result=%d bytes=%u M_IRQ=%x S_IRQ=%x IOS=%x\n",result,used,rd(se,0x610),rd(se,0x640),rd(se,0x908));
    if(used)logmsg("V60 WIRELESS: HCI head=%x\n",(unsigned)data[0]|((unsigned)data[1]<<8)|((unsigned)data[2]<<16)|((unsigned)data[3]<<24));
    if(result==1)logmsg("V60 WIRELESS: QCA product=%x patch=%x ROM=%x SoC=%x\n",version[0],version[1],version[2],version[3]);
    int stopped=!(uart_stop(se,1,0x604)|uart_stop(se,0x1000,0x634));
    /* One-shot query ends with BT reset low. Keep shared rail votes on. */
    wr(north,0x15004,rd(north,0x15004)&~2u);kernel_sleep(100);
    for(unsigned i=0;i<4;i++)wr(north,(16+i)*0x1000,pins[i]);
    wr(north,0x15004,(rd(north,0x15004)&~2u)|reset_out);wr(north,0x15000,reset_cfg);
    if(stopped) {
        for(unsigned i=0;i<((rx>>16)&63)&&(rd(se,0x804)&0x1ffffff);i++)(void)rd(se,0x780);
        wr(se,0x618,rd(se,0x610));wr(se,0x648,rd(se,0x640));
        for(unsigned i=0;i<sizeof(saved)/sizeof(saved[0]);i++)wr(se,offsets[i],saved[i]);
        int e=bt_root(gcc,root);logmsg("V60 WIRELESS: HCI stopped=1 root_restored=%u\n",!e);
    } else logmsg("V60 WIRELESS: HCI stop failed; quarantined\n");
}
