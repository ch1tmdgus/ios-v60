/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
typedef unsigned long uptr;
static uint32_t se[0x1000/4],gcc[0x60000/4],north[0x20000/4],saved_se[0x1000/4];
static unsigned power_calls,gpio_writes,uart_writes,root_writes,tx_words,rx_pos,rx_words,delays,sleeps;
static int fault;
static char messages[4096];
enum {OK,POWER_FAIL,ROOT_TIMEOUT,NO_REPLY,PARITY_ERROR,BAD_LAST,RX_START_FAIL,STOP_FAIL,BAD_PACKET};
static const uint8_t reply[]={4,255,14,0,2,16,0,0,0,0x99,0x38,0,2,0x34,0x12,0,0};
#define V60_WIRELESS_PROBE 3
#define V60_BT_POWER_EXTERNAL
static int bt_power(void){power_calls++;return fault==POWER_FAIL?-1:0;}
static uptr kernel_io_map(uptr base,uptr size){assert(base==0xf900000&&size==0x20000);return base;}
static uint32_t rd(uptr base,unsigned off){
    assert(!(off%4));
    if(base==0x100000){assert(off<sizeof(gcc));return gcc[off/4];}
    if(base==0xf900000){assert(off<sizeof(north));return north[off/4];}
    assert(base==0x998000&&off<sizeof(se));
    if(off==0x804&&rx_words)return 0x80000000u|((fault==BAD_LAST?7u:1u)<<28)|rx_words;
    if(off==0x780){
        assert(rx_words&&rx_pos<sizeof(reply));uint32_t word=0;
        for(unsigned i=0;i<4&&rx_pos<sizeof(reply);i++)word|=(uint32_t)reply[rx_pos++]<<(8*i);
        if(fault==BAD_PACKET&&rx_pos==4)word^=1;rx_words--;return word;
    }
    return se[off/4];
}
static void wr(uptr base,unsigned off,uint32_t value){
    assert(!(off%4));
    if(base==0x100000){
        assert(off>=0x17730&&off<=0x17740);root_writes++;gcc[off/4]=value;
        if(off==0x17730&&fault!=ROOT_TIMEOUT)gcc[off/4]&=~1u;
        return;
    }
    if(base==0xf900000){
        assert(power_calls&&fault!=POWER_FAIL);
        assert((off>=0x10000&&off<=0x13000&&!(off%0x1000))||off==0x15000||off==0x15004);
        gpio_writes++;north[off/4]=off==0x15004?(value&~1u)|((value>>1)&1):value;return;
    }
    assert(base==0x998000&&off<sizeof(se));uart_writes++;
    if(off==0x618||off==0x648){se[(off-8)/4]&=~value;return;}
    if(off==0x604||off==0x634){
        assert(value==4||value==2);if(fault!=STOP_FAIL)se[0x40/4]&=~(off==0x604?1u:0x1000u);return;
    }
    se[off/4]=value;
    if(off==0x630){assert(value==0x08000080);if(fault!=RX_START_FAIL)se[0x40/4]|=0x1000;}
    if(off==0x600){assert(value==0x08000000);se[0x40/4]|=1;}
    if(off==0x700){
        assert(se[0x48/4]==0x41&&se[0x4c/4]==0x41&&!se[0x22c/4]&&!se[0x25c/4]);
        assert(!se[0xe1c/4]&&se[0x270/4]==5&&(north[0x15004/4]&1));
        for(unsigned i=0;i<4;i++)assert((north[(16+i)*0x1000/4]&0x3c)==4);
        assert(gcc[0x17734/4]==0x102601&&gcc[0x17738/4]==384&&gcc[0x1773c/4]==((~(15625-384))&65535)&&gcc[0x17740/4]==((~15625u)&65535));
        assert(value==(tx_words?0x19u:0x01fc0001u));
        if(++tx_words==2){se[0x40/4]&=~1u;se[0x610/4]|=1;rx_words=fault==NO_REPLY?0:5;if(fault==PARITY_ERROR)se[0x640/4]|=1u<<9;}
    }
}
static void kernel_delay(unsigned us){assert(us==1||us==10);delays++;assert(delays<=5000);}
static void kernel_sleep(unsigned ms){assert(ms==1||ms==50||ms==100);sleeps++;assert(sleeps<=2004);}
static int logmsg(const char *fmt,...){va_list ap;va_start(ap,fmt);unsigned n=(unsigned)strlen(messages);int r=vsnprintf(messages+n,sizeof(messages)-n,fmt,ap);va_end(ap);return r;}
#include "bt_uart.h"
static void setup(int f){
    memset(se,0,sizeof(se));memset(gcc,0,sizeof(gcc));memset(north,0,sizeof(north));messages[0]=0;
    fault=f;power_calls=gpio_writes=uart_writes=root_writes=tx_words=rx_pos=rx_words=delays=sleeps=0;
    se[0x68/4]=se[0x6c/4]=0x203;se[0xe24/4]=0x20102864;se[0xe28/4]=0x20204800;se[0x48/4]=0x11;
    gcc[0]=0x80000000;gcc[1]=31;gcc[0x40/4]=0x4000;gcc[3]=0x100;gcc[0x17734/4]=0x100000;
    for(unsigned i=16;i<=21;i++)north[i*0x1000/4]=1;
    memcpy(saved_se,se,sizeof(se));
}
static void restored(void){
    assert(!north[0x15004/4]&&north[0x15000/4]==1);
    for(unsigned i=16;i<20;i++)assert(north[i*0x1000/4]==1);
    assert(gcc[0x17734/4]==0x100000&&!gcc[0x17738/4]&&!gcc[0x1773c/4]&&!gcc[0x17740/4]);
    const unsigned except[]={0x600,0x630,0x700,0x780};
    for(unsigned o=0;o<sizeof(se);o+=4){int skip=0;for(unsigned i=0;i<sizeof(except)/sizeof(except[0]);i++)skip|=o==except[i];if(!skip)assert(se[o/4]==saved_se[o/4]);}
}
int main(void){
    setup(OK);bt_uart(0x100000,0x998000);assert(strstr(messages,"HCI result=1 bytes=17"));assert(strstr(messages,"QCA product=10 patch=3899 ROM=200 SoC=1234"));restored();
    for(int f=POWER_FAIL;f<=BAD_PACKET;f++){
        setup(f);bt_uart(0x100000,0x998000);
        if(f==POWER_FAIL||f==ROOT_TIMEOUT)assert(!gpio_writes&&!uart_writes&&!tx_words);
        else if(f==STOP_FAIL){assert(strstr(messages,"quarantined"));assert(!north[0x15004/4]&&!se[0xe1c/4]);}
        else {assert(!strstr(messages,"HCI result=1"));restored();}
    }
    const unsigned invalid[]={0x68,0x64,0x40,0x800,0x804,0x610,0x640,0x258,0xe18};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){setup(OK);se[invalid[i]/4]=1;bt_uart(0x100000,0x998000);assert(!power_calls&&!gpio_writes&&!uart_writes);}
    setup(OK);gcc[1]=32;bt_uart(0x100000,0x998000);assert(!power_calls&&!gpio_writes);
    setup(OK);gcc[0x17744/4]=1;bt_uart(0x100000,0x998000);assert(!power_calls&&!gpio_writes);
    setup(OK);north[0x10000/4]=4;bt_uart(0x100000,0x998000);assert(!power_calls&&!gpio_writes);
    setup(OK);north[0x15004/4]=2;bt_uart(0x100000,0x998000);assert(!power_calls&&!gpio_writes);
    puts("External HCI: power-before-reset, exact baud/pins/EDL request, version, bounds/errors/timeouts, stop and restore: PASS");
}
