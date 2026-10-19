/* SPDX-License-Identifier: GPL-2.0-only
 * SW42902 wire format: LGE touch_sw42902.h/.c (Rofik 7eab1a7).
 * Decode explicitly instead of relying on compiler bitfield layout.
 */
#pragma once
#include <stdint.h>
#define V60_CONTACTS 10
#define V60_REPORT_SIZE 156 /* 12-byte header, 24-byte pen, 10 x 12-byte fingers */
struct v60_frame { uint32_t mask; int x[10], y[10]; };
static inline uint32_t v60_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
/* 0: absolute report; 1: non-absolute report; -1: malformed. */
static inline int v60_decode(const uint8_t *b, unsigned len, struct v60_frame *f) {
    if (len < V60_REPORT_SIZE) return -1;
    uint32_t h = v60_le32(b+8);
    if ((h & 255) != 0) return 1;
    unsigned n = (h >> 8) & 31;
    if (n > 10) return -1;
    f->mask = 0;
    uint32_t seen = 0;
    if (h >> 31) return 0; /* palm_bit: release fingers */
    for (unsigned i=0; i<n; i++) {
        const uint8_t *p = b+36+i*12;
        uint32_t a=v60_le32(p), c=v60_le32(p+4);
        unsigned id=a&31, ev=(a>>16)&3, x=a>>18, y=c&0x3fff;
        if (id == 15) { f->mask=0; return 0; } /* palm report */
        if (id>=10 || (seen & (1u<<id))) return -1;
        seen |= 1u<<id;
        if (ev==0 || ev==3) continue; /* idle/up */
        if (x>1080 || y>2460) return -1;
        f->mask |= 1u<<id; f->x[id]=(int)(x>1079?1079:x); f->y[id]=(int)(y>2459?2459:y);
    }
    return 0;
}
