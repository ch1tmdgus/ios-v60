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
#include "engine_policy.h"
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

#define STATUS 0x40
#define FW 0x68
#define CMD 0x600
#define CTRL 0x604
#define IRQ 0x610
#define CLR 0x618
#define TX 0x700
#define RX 0x780
#define TXCNT 0x800
#define RXCNT 0x804
#define DONE 1u
#define ERRORS (0x1eu|(1u<<9)|(1u<<10)|(1u<<12)|(1u<<13)|(3u<<24)|(3u<<28))
#define EVENTS (DONE|ERRORS|(1u<<26)|(1u<<27)|(1u<<30))
struct state {
    uptr se,gcc,south,north;
    uint32_t active, last_irq;
    unsigned txdepth, rxdepth;
    int dead;
    int x[10],y[10];
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
static int engine(struct state *s) {
    uint32_t fw=rd(s->se,FW),tx=rd(s->se,0xe24),rx=rd(s->se,0xe28);
    uint32_t iface=rd(s->se,0x64),status=rd(s->se,STATUS);
    logmsg("V60 TOUCH: SE13 fw=%x status=%x tx=%x rx=%x\n",fw,status,tx,rx);
    s->txdepth=v60_fifo_depth(tx);s->rxdepth=v60_fifo_depth(rx);
    logmsg("V60 TOUCH: FIFO_IF_DISABLE=%x tx_width=%u tx_depth=%u rx_width=%u rx_depth=%u\n",
        iface,v60_fifo_width(tx),s->txdepth,v60_fifo_width(rx),s->rxdepth);
    enum v60_engine_result reason=v60_engine_check(fw,iface,status,tx,rx);
    if(reason!=V60_ENGINE_OK) {
        logmsg("V60 TOUCH: engine blocked reason=%u (1=protocol 2=GSI 3=width 4=depth 5=busy)\n",reason);
        if(reason==V60_ENGINE_GSI)
            logmsg("V60 TOUCH: GSI-only interface; FIFO transport unavailable; stop before GPIO/chip commands\n");
        return -1;
    }
    if((rd(s->se,TXCNT)&0xfffffff)||(rd(s->se,RXCNT)&0x1ffffff)) {
        logmsg("V60 TOUCH: inherited FIFO data; stop\n");return -1;
    }
    wr(s->se,0xe18,0);wr(s->se,0xe1c,0);wr(s->se,0x644,0);
    wr(s->se,CLR,0xffffffff);wr(s->se,0x648,0xffffffff);wr(s->se,0x258,0);
    wr(s->se,0x28,rd(s->se,0x28)|0x7f);wr(s->se,0xe30,rd(s->se,0xe30)|15);
    wr(s->se,0x24,0x7f);wr(s->se,0x20,1);wr(s->se,0x7c,0);
    wr(s->se,0x48,(7u<<4)|1);wr(s->se,0x278,(10u<<20)|(11u<<10)|26);
    wr(s->se,0x254,0);
    wr(s->se,0x260,0x7f8fe);wr(s->se,0x264,0xffefe);
    wr(s->se,0x284,0x7f8fe);wr(s->se,0x288,0xffefe);
    wr(s->se,0x80c,1);wr(s->se,0x810,0);wr(s->se,0x814,s->rxdepth-1);
    wr(s->se,0x614,EVENTS);
    if(rd(s->se,0x260)!=0x7f8fe || rd(s->se,0x284)!=0x7f8fe)return -1;
    return 0;
}
static void abort_own(struct state *s) {
    wr(s->se,CTRL,2);
    int ok=0;
    for(unsigned i=0;i<5000;i++) {if(rd(s->se,IRQ)&(1u<<5)){ok=1;break;}kernel_delay(1);}
    if(!ok || clear_wait(s->se,STATUS,1))s->dead=1;
    for(unsigned i=0;i<s->rxdepth && (rd(s->se,RXCNT)&0x1ffffff);i++)(void)rd(s->se,RX);
    wr(s->se,CLR,0xffffffff);
    if(rd(s->se,TXCNT)&0xfffffff)s->dead=1;
}
static int msg(struct state *s,const uint8_t *tx,unsigned nt,uint8_t *rx,unsigned nr,int stretch) {
    if(s->dead || nt>6 || nr>V60_REPORT_SIZE || (nt&&nr) || (!nt&&!nr))return -1;
    if(clear_wait(s->se,STATUS,1)){s->dead=1;return -2;}
    wr(s->se,CLR,0xffffffff);wr(s->se,nr?0x270:0x26c,nr?nr:nt);
    wr(s->se,CMD,((nr?2u:1u)<<27)|(0x28u<<9)|(stretch?4:0));
    unsigned sent=0,got=0;
    for(unsigned i=0;i<20000;i++) {
        uint32_t irq=rd(s->se,IRQ);s->last_irq=irq;
        if(irq&ERRORS) {
            /* A completed NACK may leave an idle clean engine: no spurious abort. */
            if((irq&DONE) && !(rd(s->se,STATUS)&1) && !(rd(s->se,TXCNT)&0xfffffff) && !(rd(s->se,RXCNT)&0x1ffffff)) {
                wr(s->se,CLR,irq);return -3;
            }
            break;
        }
        if(nt && (irq&(1u<<30))) {
            unsigned count=rd(s->se,TXCNT)&0xfffffff;
            if(count>s->txdepth)break;
            for(unsigned free=s->txdepth-count;free && sent<nt;--free) {
                uint32_t word=0;for(unsigned j=0;j<4 && sent<nt;j++)word|=(uint32_t)tx[sent++]<<(j*8);
                wr(s->se,TX,word);
            }
        }
        unsigned count=rd(s->se,RXCNT)&0x1ffffff;
        if(count>s->rxdepth)break;
        while(count--) {
            uint32_t word=rd(s->se,RX);
            for(unsigned j=0;j<4 && got<nr;j++)rx[got++]=(uint8_t)(word>>(j*8));
        }
        if(irq&DONE) {wr(s->se,CLR,irq);if(sent==nt && got==nr)return 0;break;}
        wr(s->se,CLR,irq&((1u<<26)|(1u<<27)|(1u<<30)));kernel_delay(1);
    }
    abort_own(s);return -4;
}
static int regread(struct state *s,unsigned reg,uint8_t *buf,unsigned n) {
    uint8_t ptr[2]={(uint8_t)((n>4?0x20:0)|((reg>>8)&15)),(uint8_t)reg};
    int e=msg(s,ptr,2,0,0,1);if(e)return e;
    return msg(s,0,0,buf,n,0);
}
static int regwrite(struct state *s,unsigned reg,uint32_t value) {
    uint8_t b[6]={(uint8_t)(0x40|((reg>>8)&15)),(uint8_t)reg,
                 (uint8_t)value,(uint8_t)(value>>8),(uint8_t)(value>>16),(uint8_t)(value>>24)};
    return msg(s,b,6,0,0,0);
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
    logmsg("V60 TOUCH: touch3 SW42902 driver delay=%u mode=%d\n",delay_s,mode);
    kernel_sleep(delay_s*1000);
    struct state s={0};
    s.gcc=kernel_io_map(0x100000,0x60000);
    s.south=kernel_io_map(0xf500000,0x40000);
    s.north=kernel_io_map(0xf900000,0x50000);
    s.se=kernel_io_map(0xa94000,0x4000);
    if(!s.gcc||!s.south||!s.north||!s.se){logmsg("V60 TOUCH: mapping failed\n");goto out;}
    logmsg("V60 TOUCH: GPIO38=%x GPIO39=%x GPIO67=%x GPIO68=%x\n",
        rd(s.south,0x26004),rd(s.south,0x27004),rd(s.north,0x43004),rd(s.north,0x44004));
    if(clocks(&s)||engine(&s))goto out;
    pin(s.south,36,1,0,0);pin(s.south,37,1,0,0); /* GENI13 SDA/SCL */
    pin(s.south,39,0,3,0); /* active-low IRQ input, polling only */
    pin(s.north,68,0,0,1);kernel_sleep(10); /* VDD high */
    pin(s.north,67,0,0,1);kernel_sleep(10); /* VCL high */
    pin(s.south,38,0,3,1);kernel_sleep(100); /* release reset, never pull low */
    uint8_t version[4]={0},pid[8]={0};
    int e=regread(&s,0x642,version,4);
    logmsg("V60 TOUCH: probe 0x28 rc=%d version=%x irq=%x\n",e,v60_le32(version),s.last_irq);
    if(e || version[2]!=10 || (version[3]&15)!=4){logmsg("V60 TOUCH: SW42902 identity not confirmed; stop\n");goto out;}
    e=regread(&s,0x644,pid,8);
    logmsg("V60 TOUCH: product rc=%d words=%x/%x\n",e,v60_le32(pid),v60_le32(pid+4));
    if(mode==2){logmsg("V60 TOUCH: probe complete; controller config unchanged\n");goto out;}
    /* Volatile operating settings from sw42902_init / tc_driving(U3). */
    if(regwrite(&s,0xfe4,0)||regwrite(&s,0xc00,1)||regwrite(&s,0xc01,1)||regwrite(&s,0xc03,4)) {
        logmsg("V60 TOUCH: device init failed irq=%x\n",s.last_irq);goto out;
    }
    kernel_sleep(30);
    if(regwrite(&s,0xc03,0x8303)){logmsg("V60 TOUCH: U3 mode failed\n");goto out;}
    kernel_sleep(100);
    logmsg("V60 TOUCH: polling 0x600, 10ms; finger events -> HID\n");
    unsigned errors=0,frames=0,previous=0;
    while(!s.dead) {
        if(!(rd(s.south,0x27004)&1)) {
            uint8_t b[V60_REPORT_SIZE]={0};struct v60_frame f={0};
            e=regread(&s,0x600,b,sizeof(b));
            if(!e) {
                int d=v60_decode(b,sizeof(b),&f);
                if(d<0)e=-5;
                else if(d==0 && ((v60_le32(b+4)>>16)&15)==5) {
                    report(&s,&f);frames++;
                    if(f.mask!=previous){logmsg("V60 TOUCH: contacts=%x frames=%u\n",f.mask,frames);previous=f.mask;}
                }
            }
            if(e) {
                if((errors++%100)==0)logmsg("V60 TOUCH: report rc=%d irq=%x\n",e,s.last_irq);
                if(errors>=10){struct v60_frame f={0};report(&s,&f);}
                if(errors>=100)break;
            } else errors=0;
        }
        kernel_sleep(10);
    }
    {struct v60_frame f={0};report(&s,&f);}
out:
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
