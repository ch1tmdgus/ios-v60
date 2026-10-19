/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gpio_i2c.h"
/* Independent edge-driven slave model. All bus observations are wired-AND. */
struct sim {
 int release[2],slave_low,started,mode,nbits,ack_slot,address,next_read;
 unsigned byte,starts,stops,seen_n,read_n,master_acks,master_nacks;
 uint8_t seen[180],reply[156];
 int final_nack,nack_at,stuck_scl,stuck_sda,arb_at,stretch_remaining;
 unsigned scl_reads;
 int timeout_after_seen,fail_stop;
};
static int wire(struct sim *s,unsigned line) {
 if(line==BB_SCL)return s->release[line] && !s->stuck_scl;
 return s->release[line] && !s->slave_low && !s->stuck_sda;
}
static void edge_set(void *ctx,unsigned line,int release) {
 struct sim *s=ctx;assert(release==0 || release==1);
 int oldclk=wire(s,BB_SCL),olddata=wire(s,BB_SDA);
 s->release[line]=release;
 int clk=wire(s,BB_SCL),data=wire(s,BB_SDA);
 if(line==BB_SDA && oldclk && clk && olddata!=data) {
  if(!data){s->starts++;s->started=1;s->mode=1;s->nbits=0;s->ack_slot=0;s->address=1;s->byte=0;}
  else {s->stops++;s->started=0;s->slave_low=0;}
 }
 if(!s->started)return;
 if(!oldclk && clk) {
  if(s->mode==1 && s->nbits<8) {
   s->byte=(s->byte<<1)|(unsigned)data;s->nbits++;
   if(s->nbits==8){assert(s->seen_n<sizeof(s->seen));s->seen[s->seen_n++]=(uint8_t)s->byte;s->next_read=s->address && (s->byte&1);}
  } else if(s->mode==2) {
   if(s->nbits<8)s->nbits++;
   else {s->final_nack=data;if(data)s->master_nacks++;else s->master_acks++;}
  }
 }
 if(oldclk && !clk) {
  if(s->mode==1 && s->nbits==8) {
   if(!s->ack_slot){s->slave_low=(int)s->seen_n!=s->nack_at;s->ack_slot=1;}
   else {
    if(s->fail_stop && (int)s->seen_n==s->nack_at)s->stuck_scl=1;
    s->slave_low=0;s->ack_slot=0;s->nbits=0;s->byte=0;s->address=0;
    if(s->next_read){s->mode=2;s->slave_low=!(s->reply[s->read_n]&128);}
   }
  } else if(s->mode==2) {
   if(s->nbits<8)s->slave_low=!(s->reply[s->read_n]&(128>>s->nbits));
   else if(!s->ack_slot){s->slave_low=0;s->ack_slot=1;}
   else {s->read_n++;s->nbits=0;s->ack_slot=0;if(s->final_nack){s->mode=0;s->slave_low=0;}else s->slave_low=!(s->reply[s->read_n%156]&128);}
  }
 }
}
static int sense(void *ctx,unsigned line) {
 struct sim *s=ctx;
 if(line==BB_SCL){s->scl_reads++;if(s->timeout_after_seen>0 && (int)s->seen_n>=s->timeout_after_seen){s->stuck_scl=1;return 0;}if(s->stretch_remaining>0){s->stretch_remaining--;return 0;}}
 if(line==BB_SDA && s->arb_at>0 && (int)s->seen_n==s->arb_at && s->mode==1 && s->nbits<8)return 0;
 return wire(s,line);
}
static unsigned delays;
static void delay(unsigned us){assert(us==5 || us==10);delays++;}
static struct v60_bb init(struct sim *s) {
 memset(s,0,sizeof(*s));s->release[0]=s->release[1]=1;
 for(unsigned i=0;i<156;i++)s->reply[i]=(uint8_t)(i*37+0xa5);
 return (struct v60_bb){s,edge_set,sense,delay,0};
}
int main(void) {
 struct sim s;struct v60_bb b=init(&s);uint8_t rx[156]={0};uint8_t ptr[]={0x06,0x42};
 assert(v60_bb_xfer(&b,ptr,2,rx,4)==0);
 assert(s.starts==2 && s.stops==1 && s.seen_n==4);
 assert(!memcmp(s.seen,(uint8_t[]){0x50,0x06,0x42,0x51},4));
 assert(!memcmp(rx,s.reply,4) && s.master_acks==3 && s.master_nacks==1);
 b=init(&s);uint8_t report[]={0x26,0x00};assert(v60_bb_xfer(&b,report,2,rx,156)==0);
 assert(!memcmp(rx,s.reply,156) && s.master_acks==155 && s.master_nacks==1);
 b=init(&s);uint8_t wr[]={0x4c,0x03,0x03,0x83,0,0};assert(v60_bb_xfer(&b,wr,6,0,0)==0);
 assert(s.starts==1 && s.stops==1 && s.seen_n==7 && !memcmp(s.seen+1,wr,6));
 for(int n=1;n<=4;n++) {
  b=init(&s);s.nack_at=n;assert(v60_bb_xfer(&b,ptr,2,rx,4)==BB_NACK);
  assert(s.stops==1 && s.release[0] && s.release[1]);
  assert(b.phase==(n==1?2:n==2?0x100:n==3?0x101:4));
 }
 b=init(&s);s.stuck_scl=1;assert(v60_bb_xfer(&b,ptr,2,rx,4)==BB_TIMEOUT);
 assert(s.scl_reads==1000 && !s.starts && !s.stops && s.release[0] && s.release[1]);
 b=init(&s);s.timeout_after_seen=3;assert(v60_bb_xfer(&b,ptr,2,rx,4)==BB_TIMEOUT);
 assert(s.release[0] && s.release[1] && !s.stops);
 b=init(&s);s.nack_at=1;s.fail_stop=1;
 assert(v60_bb_xfer(&b,ptr,2,rx,4)==BB_TIMEOUT);
 assert(s.release[0] && s.release[1]);
 b=init(&s);s.stuck_sda=1;assert(v60_bb_xfer(&b,ptr,2,rx,4)==BB_BUSY);
 assert(!s.starts && !s.stops && s.release[0] && s.release[1]);
 b=init(&s);s.arb_at=1;assert(v60_bb_xfer(&b,ptr,2,rx,4)==BB_ARB);
 assert(s.release[0] && s.release[1] && !s.stops);
 b=init(&s);s.stretch_remaining=7;assert(v60_bb_xfer(&b,ptr,2,rx,4)==0);
 assert(!memcmp(rx,s.reply,4));
 b=init(&s);assert(v60_bb_xfer(&b,ptr,2,0,4)==BB_INVALID);assert(!s.starts);
 assert(v60_bb_xfer(&b,ptr,0,rx,4)==BB_INVALID);
 assert(v60_bb_xfer(&b,ptr,2,rx,157)==BB_INVALID);
 assert(v60_bb_xfer(&b,wr,7,0,0)==BB_INVALID);
 puts("GPIO I2C edge-model: combined read, 156-byte report, write, four NACK phases, stuck lines, arbitration, stretching, bounds PASS");
}
