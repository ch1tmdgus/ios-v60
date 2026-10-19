/* SPDX-License-Identifier: GPL-2.0-only
 * V60-only ACTIVE RPMh voltage/enable votes. DT: apps DRV2@18220000,
 * TCS+0xd00, ACTIVE2/SLEEP3/WAKE3; cmd-db@80860000 size20000.
 * Shared rails: smpa6=950mV (AON+DIG), smpa5=1900mV, smpc8=1350mV.
 * No mode/headroom vote, sleep/wake programming, direct PMIC write or retry.
 * Based on Rofik rpmh-rsc.c/cmd-db.c and existing sweet_rpmh.c port.
 */
#pragma once
struct bt_rpmh { uptr rsc,tcs,db;unsigned ncpt; };
static int bt_sync(uptr base,unsigned off,uint32_t value) {
    wr(base,off,value);
    for(unsigned i=0;i<1000;i++){if(rd(base,off)==value)return 0;kernel_delay(1);}
    return -1;
}
static unsigned bt_db16(uptr db,unsigned off) {
    return (rd(db,off&~3u)>>(8*(off&3)))&65535;
}
static int bt_db_range(unsigned off,unsigned size){return off<=0x20000&&size<=0x20000-off;}
static int bt_lookup(uptr db,const char *name,uint32_t *address) {
    uint32_t key[2]={0,0};unsigned matches=0;
    for(unsigned i=0;i<8&&name[i];i++)key[i/4]|=(uint32_t)(uint8_t)name[i]<<(8*(i%4));
    for(unsigned i=0;i<8;i++) {
        unsigned h=8+i*16,slave=bt_db16(db,h);
        if(!slave)break;
        unsigned e=144+bt_db16(db,h+2),aux=144+bt_db16(db,h+4),n=bt_db16(db,h+6);
        if(slave<3||slave>5||(e&3)||!bt_db_range(e,n*24)||!bt_db_range(aux,0))return -1;
        for(unsigned j=0;j<n;j++) {
            unsigned p=e+j*24;
            if(rd(db,p)!=key[0]||rd(db,p+4)!=key[1])continue;
            uint32_t a=rd(db,p+16);
            if(slave!=4||(a>>16)!=4||(a&15)||!bt_db_range(aux+bt_db16(db,p+22),bt_db16(db,p+20)))return -1;
            *address=a;matches++;
        }
    }
    return matches==1?0:-1;
}
static int bt_tcs_free(uptr tcs,unsigned t) {
    unsigned b=t*0x2a0;uint32_t idle=rd(tcs,b+0x18);
    return idle&&idle!=0xffffffffu&&!rd(tcs,b+0x14)&&!rd(tcs,b+0x1c)&&!rd(tcs,b+0x10)&&!(rd(tcs,4)&(1u<<t));
}
static int bt_vote(struct bt_rpmh *r,uint32_t addr,unsigned mv,const uint32_t addresses[3]) {
    int chosen=-1;uptr tcs=r->tcs;
    for(unsigned t=0;t<2;t++) {
        unsigned b=t*0x2a0;uint32_t enabled=rd(tcs,b+0x1c);
        if(enabled&~((1u<<r->ncpt)-1))return -1;
        for(unsigned c=0;c<r->ncpt;c++)if(enabled&(1u<<c)) {
            uint32_t a=rd(tcs,b+0x34+c*20)&~15u;
            for(unsigned j=0;j<3;j++)if(a==addresses[j])return -1;
        }
        if(chosen<0&&bt_tcs_free(tcs,t))chosen=(int)t;
    }
    if(chosen<0){logmsg("V60 WIRELESS: RPMh no free ACTIVE TCS\n");return -1;}
    unsigned t=(unsigned)chosen,b=t*0x2a0,bit=1u<<t;uint32_t irq=rd(tcs,0);
    if(irq==0xffffffffu||!bt_tcs_free(tcs,t)||bt_sync(tcs,0,irq&~bit))return -1;
    if(!bt_tcs_free(tcs,t)){(void)bt_sync(tcs,0,(rd(tcs,0)&~bit)|(irq&bit));return -1;}
    /* From first payload write, any uncertain operation ends ALL rail work.
     * Never free/reset/retry a possibly executing TCS on failure.
     */
    for(unsigned c=0;c<2;c++) {
        unsigned o=b+c*20;
        if(bt_sync(tcs,o+0x30,0x10108)||bt_sync(tcs,o+0x34,addr+c*4)||bt_sync(tcs,o+0x38,c?1:mv))return -1;
    }
    if(bt_sync(tcs,b+0x10,3)||bt_sync(tcs,b+0x1c,3)||bt_sync(tcs,b+0x14,0)||bt_sync(tcs,b+0x14,1u<<16))return -1;
    wr(tcs,b+0x14,(1u<<16)|(1u<<24));
    int done=0,progress=0;uint32_t status0=0,status1=0;
    for(unsigned us=0;us<100000;us++) {
        uint32_t idle=rd(tcs,b+0x18),pending=rd(tcs,4);
        status0=rd(tcs,b+0x3c);status1=rd(tcs,b+0x50);
        int complete=status0!=0xffffffffu&&status1!=0xffffffffu&&(status0&0x10100)==0x10100&&(status1&0x10100)==0x10100;
        progress|=!idle||!complete;
        if(idle&&idle!=0xffffffffu&&pending!=0xffffffffu&&((pending&bit)||progress)&&complete){done=1;break;}
        kernel_delay(1);
    }
    logmsg("V60 WIRELESS: RPMh addr=%x mv=%u tcs=%u done=%u status=%x/%x\n",addr,mv,t,done,status0,status1);
    if(!done)return -1;
    if(bt_sync(tcs,b+0x14,1u<<16)||bt_sync(tcs,b+0x14,0)||bt_sync(tcs,b+0x1c,0)||bt_sync(tcs,b+0x10,0))return -1;
    wr(tcs,8,bit);
    if(rd(tcs,4)&bit)return -1;
    return bt_sync(tcs,0,(rd(tcs,0)&~bit)|(irq&bit));
}
static int bt_power(void) {
    struct bt_rpmh r={kernel_io_map(0x18220000,0x100),kernel_io_map(0x18220d00,0x3000),kernel_io_map(0x80860000,0x20000),0};
    if(!r.rsc||!r.tcs||!r.db)return -1;
    uint32_t id=rd(r.rsc,0),config=rd(r.rsc,12),solver=rd(r.rsc,4);
    unsigned major=(id>>16)&255,count=(config>>12)&63;r.ncpt=(config>>27)&31;
    logmsg("V60 WIRELESS: RPMh id=%x config=%x solver=%x count=%u ncpt=%u\n",id,config,solver,count,r.ncpt);
    if((major!=1&&major!=2)||config==0xffffffffu||solver==0xffffffffu||count<8||count>18||r.ncpt<2||r.ncpt>16)return -1;
    logmsg("V60 WIRELESS: cmd-db version=%x magic=%x flags=%x\n",rd(r.db,0),rd(r.db,4),rd(r.db,140));
    if(rd(r.db,0)!=1||rd(r.db,4)!=0x0c0330db||(rd(r.db,140)&1))return -1;
    const char *names[]={"smpa6","smpa5","smpc8"};const unsigned mv[]={950,1900,1350};uint32_t addr[3]={0};
    for(unsigned i=0;i<3;i++) {
        if(bt_lookup(r.db,names[i],&addr[i]))return -1;
        for(unsigned j=0;j<i;j++)if(addr[i]==addr[j])return -1;
        logmsg("V60 WIRELESS: RPMh %s addr=%x mv=%u\n",names[i],addr[i],mv[i]);
    }
    for(unsigned i=0;i<3;i++)if(bt_vote(&r,addr[i],mv[i],addr)){logmsg("V60 WIRELESS: RPMh fail rail=%u\n",i);return -1;}
    kernel_sleep(50);return 0;
}
