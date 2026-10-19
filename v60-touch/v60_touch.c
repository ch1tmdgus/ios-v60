/* SPDX-License-Identifier: GPL-2.0-only
 * LG V60 SW42902 polling driver experiment.
 * Protocol/power/clock facts from Rofik kernel 7eab1a7 and the supplied live DT.
 * No firmware upload, flash commands, RPMh votes, PMIC writes, or reset-low pulse.
 * Standalone fixed-ABI build for the exact supplied XNU image (see build.py).
 */
#include <stdint.h>
#include <stddef.h>
#include "protocol.h"
#include "clock_policy.h"
#include "gpio_i2c.h"
typedef unsigned long uptr;
extern uptr kernel_io_map(uptr, uptr);
extern int kernel_thread_start(void (*)(void *, int), void *, uptr *);
extern void kernel_thread_deallocate(uptr);
extern int kernel_thread_terminate(uptr);
extern uptr kernel_current_thread(void);
extern void kernel_sleep(unsigned);
extern void kernel_delay(unsigned);
extern int kernel_bootarg(const char *, void *, int);
extern void kernel_hid_touch(int, int, int, int);
/* Assembly bridge converts AAPCS64 register varargs to Apple's stack varargs. */
extern int logmsg(const char *, ...);

struct gpio_bus {
    uptr tile;
    uint32_t saved_cfg[2],saved_out[2],cfg[2];
    int owned;
};
struct state {
    uptr se,gcc,south,north;
    uint32_t active,last_phase;
    int dead;
    int x[10],y[10];
    struct gpio_bus gpio;
    struct v60_bb bus;
};
static uint32_t rd(uptr p,unsigned o) {
    uint32_t v=*(volatile uint32_t *)(p+o);
    __asm__ volatile("dmb oshld" ::: "memory"); return v;
}
static void wr(uptr p,unsigned o,uint32_t v) {
    __asm__ volatile("dmb oshst" ::: "memory");
    *(volatile uint32_t *)(p+o)=v;
    __asm__ volatile("dsb osh" ::: "memory");
}
static int clear_wait(uptr p,unsigned o,uint32_t mask) {
    for(unsigned i=0;i<5000;i++) {if(!(rd(p,o)&mask))return 0;kernel_delay(1);}return -1;
}
static void pin(uptr tile,unsigned id,unsigned mux,unsigned pull,int out) {
    unsigned o=id*0x1000;
    if(out)wr(tile,o+4,rd(tile,o+4)|2);
    uint32_t cfg=rd(tile,o)&~0x3ffu;
    wr(tile,o,cfg|(mux<<2)|pull|(out?0x200:0));
}
static int clocks(struct state *s) {
    /* gcc-kona: wrap1 core/core2x/AHB vote bits 18..21, S5 bit27, at 0x52008. */
    uint32_t votes=(15u<<18)|(1u<<27);
    wr(s->gcc,0x52008,rd(s->gcc,0x52008)|votes);
    if((rd(s->gcc,0x52008)&votes)!=votes) {
        logmsg("V60 TOUCH: clock vote not accepted; stop\n");return -1;
    }
    uint32_t dfs=rd(s->gcc,0x18614), cfg=rd(s->gcc,0x18604);
    logmsg("V60 TOUCH: GCC vote=%x cfg=%x dfs=%x\n",rd(s->gcc,0x52008),cfg,dfs);
    if(dfs&1) {
        cfg=rd(s->gcc,0x1861c);
        if((cfg&0x371f)!=1) {logmsg("V60 TOUCH: unsupported DFS0=%x; stop\n",cfg);return -1;}
    } else {
        wr(s->gcc,0x18604,(cfg&~(0x371fu|(1u<<20)))|1);
        wr(s->gcc,0x18600,rd(s->gcc,0x18600)|1);
        if(clear_wait(s->gcc,0x18600,1))return -1;
    }
    const unsigned branches[]={0x18004,0x18008,0x23140,0x23138,0x185fc};
    for(unsigned i=0;i<5;i++) {
        uint32_t value=rd(s->gcc,branches[i]);
        unsigned tries=0;
        while(!v60_clock_ready(branches[i],value) && tries++<5000) {
            kernel_delay(1);value=rd(s->gcc,branches[i]);
        }
        logmsg("V60 TOUCH: branch off=%x value=%x hwcg=%u ready=%u\n",
            branches[i],value,v60_clock_hwcg(branches[i],value),v60_clock_ready(branches[i],value));
        if(!v60_clock_ready(branches[i],value)) {
            logmsg("V60 TOUCH: clock halted off=%x value=%x\n",branches[i],value);return -1;
        }
    }
    return 0;
}
static void gpio_set(void *ctx,unsigned line,int release) {
    struct gpio_bus *g=ctx;
    /* OUT latch stays zero; OE=0 releases, OE=1 pulls low. */
    wr(g->tile,(36+line)*0x1000,g->cfg[line]|(release?0:0x200));
}
static int gpio_get(void *ctx,unsigned line) {
    struct gpio_bus *g=ctx;return rd(g->tile,(36+line)*0x1000+4)&1;
}
static void gpio_restore(struct gpio_bus *g) {
    if(!g->owned)return;
    for(unsigned i=0;i<2;i++) {
        unsigned o=(36+i)*0x1000;
        wr(g->tile,o,g->cfg[i]); /* release before restoring latch/mux */
        wr(g->tile,o+4,(rd(g->tile,o+4)&~2u)|g->saved_out[i]);
        wr(g->tile,o,g->saved_cfg[i]);
    }
    g->owned=0;
}
static int gpio_take(struct state *s) {
    struct gpio_bus *g=&s->gpio;g->tile=s->south;
    /* Never remux a running GENI master or slave. No GENI/GSI register writes. */
    uint32_t status=rd(s->se,0x40);
    logmsg("V60 TOUCH: GPIO transport SE13 status=%x FIFO_IF_DISABLE=%x\n",status,rd(s->se,0x64));
    if(status&0x1001){logmsg("V60 TOUCH: GENI busy; GPIO takeover refused\n");return -1;}
    for(unsigned i=0;i<2;i++) {
        unsigned o=(36+i)*0x1000;
        g->saved_cfg[i]=rd(g->tile,o);g->saved_out[i]=rd(g->tile,o+4)&2;
        g->cfg[i]=g->saved_cfg[i]&~0x3ffu; /* mux0, input, no pull, 2mA */
    }
    g->owned=1;
    for(unsigned i=0;i<2;i++) {
        unsigned o=(36+i)*0x1000;
        /* Release under existing mux first, then preload LOW and select GPIO. */
        wr(g->tile,o,g->saved_cfg[i]&~0x200u);
        wr(g->tile,o+4,rd(g->tile,o+4)&~2u);
        wr(g->tile,o,g->cfg[i]);
        if((rd(g->tile,o)&0x3ff)!=0){logmsg("V60 TOUCH: GPIO mux/OE readback failed pin=%u\n",36+i);return -1;}
    }
    s->bus=(struct v60_bb){g,gpio_set,gpio_get,kernel_delay,0};
    return 0;
}
static int regread(struct state *s,unsigned reg,uint8_t *buf,unsigned n) {
    uint8_t ptr[2]={(uint8_t)((n>4?0x20:0)|((reg>>8)&15)),(uint8_t)reg};
    int e=v60_bb_xfer(&s->bus,ptr,2,buf,n);s->last_phase=s->bus.phase;
    if(e==BB_TIMEOUT || e==BB_ARB || e==BB_BUSY)s->dead=1;
    return e;
}
static int regwrite(struct state *s,unsigned reg,uint32_t value) {
    uint8_t b[6]={(uint8_t)(0x40|((reg>>8)&15)),(uint8_t)reg,
                 (uint8_t)value,(uint8_t)(value>>8),(uint8_t)(value>>16),(uint8_t)(value>>24)};
    int e=v60_bb_xfer(&s->bus,b,6,0,0);s->last_phase=s->bus.phase;
    if(e==BB_TIMEOUT || e==BB_ARB || e==BB_BUSY)s->dead=1;
    return e;
}
static void report(struct state *s,const struct v60_frame *f) {
    for(unsigned i=0;i<10;i++) {
        uint32_t bit=1u<<i;
        if(f->mask&bit) {
            /* Fixed image presents 2436 logical rows, cropped from 2460 (-12 offset).
             * HID legacy 2400 clamps are corrected by the guarded builder. */
            int y=f->y[i]<12?12:(f->y[i]>2447?2447:f->y[i]);
            if(!(s->active&bit)||s->x[i]!=f->x[i]||s->y[i]!=y)kernel_hid_touch(i,1,f->x[i],y);
            s->x[i]=f->x[i];s->y[i]=y;
        } else if(s->active&bit)kernel_hid_touch(i,0,s->x[i],s->y[i]);
    }
    s->active=f->mask;
}
static void worker(void *arg,int result) {
    (void)arg;(void)result;
    unsigned delay_s=60;int mode=1;
    kernel_bootarg("v60touchdelay",&delay_s,sizeof(delay_s));
    kernel_bootarg("v60touch",&mode,sizeof(mode));
    if(delay_s>300)delay_s=300;
    logmsg("V60 TOUCH: touch4 GPIO-I2C SW42902 driver delay=%u mode=%d\n",delay_s,mode);
    kernel_sleep(delay_s*1000);
    struct state s={0};
    s.gcc=kernel_io_map(0x100000,0x60000);
    s.south=kernel_io_map(0xf500000,0x40000);
    s.north=kernel_io_map(0xf900000,0x50000);
    s.se=kernel_io_map(0xa94000,0x4000);
    if(!s.gcc||!s.south||!s.north||!s.se){logmsg("V60 TOUCH: mapping failed\n");goto out;}
    logmsg("V60 TOUCH: GPIO38=%x GPIO39=%x GPIO67=%x GPIO68=%x\n",
        rd(s.south,0x26004),rd(s.south,0x27004),rd(s.north,0x43004),rd(s.north,0x44004));
    if(clocks(&s)||gpio_take(&s))goto out;
    pin(s.south,39,0,3,0); /* active-low IRQ input, polling only */
    pin(s.north,67,0,0,1);kernel_sleep(1); /* VCL high: vendor POWER_ON order */
    pin(s.north,68,0,0,1);kernel_sleep(10); /* VDD high */
    pin(s.south,38,0,3,1);kernel_sleep(100); /* release reset, never pull low */
    logmsg("V60 TOUCH: power reset=%x irq=%x vcl=%x vdd=%x\n",
        rd(s.south,0x26004),rd(s.south,0x27004),rd(s.north,0x43004),rd(s.north,0x44004));
    logmsg("V60 TOUCH: GPIO-I2C SDA36=%u SCL37=%u half_delay=5us\n",
        gpio_get(&s.gpio,0),gpio_get(&s.gpio,1));
    uint8_t version[4]={0},pid[8]={0};
    int e=regread(&s,0x642,version,4);
    logmsg("V60 TOUCH: probe 0x28 rc=%d version=%x phase=%x\n",e,v60_le32(version),s.last_phase);
    if(e || version[2]!=10 || (version[3]&15)!=4){logmsg("V60 TOUCH: SW42902 identity not confirmed; stop\n");goto out;}
    e=regread(&s,0x644,pid,8);
    logmsg("V60 TOUCH: product rc=%d words=%x/%x\n",e,v60_le32(pid),v60_le32(pid+4));
    if(mode==2){logmsg("V60 TOUCH: probe complete; controller config unchanged\n");goto out;}
    /* Volatile operating settings from sw42902_init / tc_driving(U3). */
    if(regwrite(&s,0xfe4,0)||regwrite(&s,0xc00,1)||regwrite(&s,0xc01,1)||regwrite(&s,0xc03,4)) {
        logmsg("V60 TOUCH: device init failed phase=%x\n",s.last_phase);goto out;
    }
    kernel_sleep(30);
    if(regwrite(&s,0xc03,0x8303)){logmsg("V60 TOUCH: U3 mode failed\n");goto out;}
    kernel_sleep(100);
    logmsg("V60 TOUCH: polling 0x600, 10ms; finger events -> HID\n");
    unsigned errors=0,frames=0,previous=0,reads=0,polls=0;
    while(!s.dead) {
        if(!(rd(s.south,0x27004)&1)) {
            uint8_t b[V60_REPORT_SIZE]={0};struct v60_frame f={0};
            e=regread(&s,0x600,b,sizeof(b));
            if(!e) {
                if(reads++<3)logmsg("V60 TOUCH: report header ic=%x tc=%x info=%x\n",
                    v60_le32(b),v60_le32(b+4),v60_le32(b+8));
                int d=v60_decode(b,sizeof(b),&f);
                if(d<0)e=-5;
                else if(d==0 && ((v60_le32(b+4)>>16)&15)==5) {
                    report(&s,&f);frames++;
                    if(f.mask!=previous){logmsg("V60 TOUCH: contacts=%x frames=%u\n",f.mask,frames);previous=f.mask;}
                }
            }
            if(e) {
                if((errors++%100)==0)logmsg("V60 TOUCH: report rc=%d phase=%x\n",e,s.last_phase);
                if(errors>=10){struct v60_frame f={0};report(&s,&f);}
                if(errors>=100)break;
            } else errors=0;
        }
        if(++polls%3000==0)logmsg("V60 TOUCH: alive polls=%u reads=%u frames=%u irqpin=%u\n",
            polls,reads,frames,rd(s.south,0x27004)&1);
        kernel_sleep(10);
    }
    {struct v60_frame f={0};report(&s,&f);}
out:
    gpio_restore(&s.gpio);
    logmsg("V60 TOUCH: worker stopped\n");
    kernel_thread_terminate(kernel_current_thread());
    for(;;)kernel_sleep(1000);
}
__attribute__((section(".text.entry"))) void v60_touch_init(void) {
    int enable=1;uptr thread=0;
    kernel_bootarg("v60touch",&enable,sizeof(enable));
    if(!enable){logmsg("V60 TOUCH: disabled\n");return;}
    int e=kernel_thread_start(worker,0,&thread);
    if(!e)kernel_thread_deallocate(thread);
    else logmsg("V60 TOUCH: thread creation failed %d\n",e);
}
/* GCC may lower large automatic zero-initializers to memset even freestanding. */
void *memset(void *p,int c,size_t n) {
    volatile unsigned char *b=p;
    for(size_t i=0;i<n;i++)b[i]=(unsigned char)c;
    return p;
}
