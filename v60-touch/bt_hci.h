/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
/* Parse bounded H4 vendor EDL version event, without reading MAC/NVM data.
 * Rofik btqca.c: opcodeFC00/subcmd19 -> vendor event00/02 + rome_version.
 * 1=version,0=incomplete/other event,-1=malformed H4 stream.
 */
static int bt_hci_version(const uint8_t *b,unsigned n,uint32_t result[4]) {
    unsigned p=0;
    while(p<n) {
        if(b[p]!=4)return -1;
        if(n-p<3)return 0;
        unsigned len=b[p+2];
        if(len+3>n-p)return 0;
        if(b[p+1]==255&&len==14&&b[p+3]==0&&(b[p+4]==2||b[p+4]==0x19)) {
            const uint8_t *v=b+p+5;
            result[0]=(uint32_t)v[0]|((uint32_t)v[1]<<8)|((uint32_t)v[2]<<16)|((uint32_t)v[3]<<24);
            result[1]=v[4]|((uint32_t)v[5]<<8);
            result[2]=v[6]|((uint32_t)v[7]<<8);
            result[3]=(uint32_t)v[8]|((uint32_t)v[9]<<8)|((uint32_t)v[10]<<16)|((uint32_t)v[11]<<24);
            return 1;
        }
        p+=len+3;
    }
    return 0;
}
