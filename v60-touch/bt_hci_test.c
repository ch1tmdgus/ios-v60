/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bt_hci.h"
int main(void){
    const uint8_t event[]={4,255,14,0,2,16,0,0,0,0x99,0x38,0,2,0x34,0x12,0,0};uint32_t r[4]={0};
    for(unsigned n=0;n<sizeof(event);n++)assert(!bt_hci_version(event,n,r));
    assert(bt_hci_version(event,sizeof(event),r)==1);assert(r[0]==16&&r[1]==0x3899&&r[2]==0x200&&r[3]==0x1234);
    uint8_t stream[24]={4,14,4,1,0,252,0};memcpy(stream+7,event,sizeof(event));assert(bt_hci_version(stream,sizeof(stream),r)==1);
    stream[0]=1;assert(bt_hci_version(stream,sizeof(stream),r)==-1);
    uint8_t large[]={4,255,255,0};assert(!bt_hci_version(large,sizeof(large),r));
    for(unsigned i=0;i<4;i++)stream[i]=0;assert(bt_hci_version(stream,4,r)==-1);
    puts("HCI parser: partial frames, command-complete prefix, QCA version, malformed/oversized stream: PASS");
}
