/* SPDX-License-Identifier: GPL-2.0-only
 * Open-drain software I2C: release=1 means input/high impedance, never drive high.
 * Timing/START/STOP/ACK sequence cross-checked with vendor i2c-algo-bit.c.
 * Single master only; no bus recovery pulses or retry after arbitration loss.
 */
#pragma once
#include <stdint.h>
struct v60_bb {
    void *ctx;
    void (*set)(void *,unsigned,int);
    int (*get)(void *,unsigned);
    void (*delay)(unsigned);
    unsigned phase;
};
enum { BB_SDA,BB_SCL };
enum { BB_BUSY=-10,BB_TIMEOUT=-11,BB_NACK=-12,BB_ARB=-13,BB_INVALID=-14 };
static inline void bb_set(struct v60_bb *b,unsigned line,int release) {
    b->set(b->ctx,line,release);b->delay(5);
}
static inline void bb_release(struct v60_bb *b) {
    b->set(b->ctx,BB_SDA,1);b->set(b->ctx,BB_SCL,1);
}
static int bb_high(struct v60_bb *b) {
    b->set(b->ctx,BB_SCL,1);
    for(unsigned i=0;i<1000;i++) {
        if(b->get(b->ctx,BB_SCL)){b->delay(5);return 0;}
        b->delay(10);
    }
    return BB_TIMEOUT;
}
static int bb_start(struct v60_bb *b,int repeat) {
    bb_set(b,BB_SDA,1);
    int e=bb_high(b);if(e)return e;
    if(!b->get(b->ctx,BB_SDA))return repeat?BB_ARB:BB_BUSY;
    bb_set(b,BB_SDA,0);bb_set(b,BB_SCL,0);return 0;
}
static int bb_stop(struct v60_bb *b) {
    bb_set(b,BB_SDA,0);
    int e=bb_high(b);if(e){bb_release(b);return e;}
    bb_set(b,BB_SDA,1);
    return b->get(b->ctx,BB_SDA)?0:BB_BUSY;
}
static int bb_write(struct v60_bb *b,uint8_t value) {
    for(unsigned i=0;i<8;i++) {
        int one=(value>>(7-i))&1;
        bb_set(b,BB_SDA,one);
        int e=bb_high(b);if(e)return e;
        if(one && !b->get(b->ctx,BB_SDA))return BB_ARB;
        bb_set(b,BB_SCL,0);
    }
    bb_set(b,BB_SDA,1);
    int e=bb_high(b);if(e)return e;
    int ack=!b->get(b->ctx,BB_SDA);
    bb_set(b,BB_SCL,0);return ack?0:BB_NACK;
}
static int bb_read(struct v60_bb *b,uint8_t *value,int last) {
    unsigned v=0;bb_set(b,BB_SDA,1);
    for(unsigned i=0;i<8;i++) {
        int e=bb_high(b);if(e)return e;
        v=(v<<1)|(unsigned)b->get(b->ctx,BB_SDA);
        bb_set(b,BB_SCL,0);
    }
    bb_set(b,BB_SDA,last); /* ACK except final byte: NACK */
    int e=bb_high(b);if(e)return e;
    bb_set(b,BB_SCL,0);bb_set(b,BB_SDA,1);
    *value=(uint8_t)v;return 0;
}
static int v60_bb_xfer(struct v60_bb *b,const uint8_t *tx,unsigned nt,uint8_t *rx,unsigned nr) {
    if(!tx || !nt || nt>6 || nr>156 || (nr && !rx))return BB_INVALID;
    b->phase=1;int e=bb_start(b,0);if(e){bb_release(b);return e;}
    b->phase=2;e=bb_write(b,0x50);if(e)goto fail;
    for(unsigned i=0;i<nt;i++){b->phase=0x100+i;e=bb_write(b,tx[i]);if(e)goto fail;}
    if(nr) {
        b->phase=3;e=bb_start(b,1);if(e)goto fail;
        b->phase=4;e=bb_write(b,0x51);if(e)goto fail;
        for(unsigned i=0;i<nr;i++){b->phase=0x200+i;e=bb_read(b,&rx[i],i+1==nr);if(e)goto fail;}
    }
    b->phase=5;e=bb_stop(b);bb_release(b);return e;
fail:
    /* NACK is ours to STOP. Busy/arbitration/timeout: only release both lines. */
    if(e==BB_NACK){int stop_error=bb_stop(b);if(stop_error)e=stop_error;}
    bb_release(b);return e;
}
