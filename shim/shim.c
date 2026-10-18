/*
 * Milestone 2: one boot answers the questions that decide the XNU port.
 *   - CPU feature registers (16K granule, GIC sysreg interface)
 *   - PSCI/SMCCC version and which PSCI calls the Qualcomm TZ offers
 *   - SMP: PSCI CPU_ON for the 7 secondaries, each reports its MPIDR
 *   - GICv3 Group 1 IRQ from the virtual timer (INTID 19 on Qualcomm)
 * Any unexpected exception paints a blue panic screen.
 */
#include <stdint.h>
#include "console.h"
#include "mmu.h"
#include "loader.h"
#include "ufs.h"

#define IMEM_RESTART_REASON	0x146bf65cUL	/* V60 live DT: qcom,msm-imem@146bf000 + restart_reason@65c */
#define RESTART_BOOTLOADER	0x77665500U
#define PSHOLD			0x0c264000UL

#define GICD_BASE		0x17a00000UL
#define GICR_BASE		0x17a60000UL
#define GICR_SIZE		0x100000UL
#define GICR_PE_SIZE		0x20000UL
#define GICR_TYPER		0x08
#define GICR_WAKER		0x14
#define GICR_SGI		0x10000
#define GICR_IGROUPR0		(GICR_SGI + 0x080)
#define GICR_ISENABLER0		(GICR_SGI + 0x100)
#define GICR_ICENABLER0		(GICR_SGI + 0x180)
#define GICR_IPRIORITYR		(GICR_SGI + 0x400)
#define GICR_ISPENDR0		(GICR_SGI + 0x200)
#define GICR_ISACTIVER0		(GICR_SGI + 0x300)
#define GICR_ICFGR1		(GICR_SGI + 0xc04)
#define VTIMER_INTID		27	/* V60 DT: <GIC_PPI 11> (standard) */
#define PTIMER_INTID		30	/* V60 DT: <GIC_PPI 14> */
#define SGI_INTID		1

#define PSCI_VERSION		0x84000000UL
#define PSCI_CPU_SUSPEND	0xc4000001UL
#define PSCI_CPU_OFF		0x84000002UL
#define PSCI_CPU_ON		0xc4000003UL
#define PSCI_MIGRATE_INFO_TYPE	0x84000006UL
#define PSCI_SYSTEM_RESET	0x84000009UL
#define PSCI_FEATURES		0x8400000aUL
#define PSCI_SYSTEM_SUSPEND	0xc400000eUL
#define PSCI_SYSTEM_RESET2	0xc4000012UL
#define SMCCC_VERSION		0x80000000UL

#define NR_CPUS			8

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })
#define wsr(r, v) __asm__ volatile("msr " r ", %0" : : "r"((uint64_t)(v)))
#define isb() __asm__ volatile("isb" ::: "memory")

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline uint64_t rd64(uintptr_t a) { return *(volatile uint64_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline void wr8(uintptr_t a, uint8_t v) { *(volatile uint8_t *)a = v; }

extern char secondary_entry[];

static const char *stage;	/* set at runtime: no absolute pointers in data */
static volatile uint64_t sec_mpidr[NR_CPUS];
static volatile uint64_t sec_midr[NR_CPUS];
static volatile uint64_t sec_probe[4];	/* CPU1 redistributor write test */
static volatile uint32_t irq_count, irq_other, irq_last;
static volatile int irq_src;

/* ICC_SGI1R_EL1 value targeting the calling core only. */
static uint64_t sgi_self(unsigned id)
{
	uint64_t m = rsr("mpidr_el1");
	return ((m >> 16) & 0xff) << 32 |	/* Aff2 */
	       ((m >> 8) & 0xff) << 16 |	/* Aff1 */
	       (uint64_t)id << 24 |
	       (1U << (m & 0xf));		/* target list: Aff0 */
}

static uint64_t smc(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3)
{
	register uint64_t x0 __asm__("x0") = fn;
	register uint64_t x1 __asm__("x1") = a1;
	register uint64_t x2 __asm__("x2") = a2;
	register uint64_t x3 __asm__("x3") = a3;
	__asm__ volatile("smc #0"
			 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
			 :
			 : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
			   "x12", "x13", "x14", "x15", "x16", "x17", "memory");
	return x0;
}

static uint64_t now(void)
{
	isb();
	return rsr("cntpct_el0");
}

static void delay_ms(unsigned ms)
{
	uint64_t end = now() + rsr("cntfrq_el0") / 1000 * ms;
	while (now() < end)
		;
}

static void __attribute__((noreturn)) reset(unsigned after_s)
{
	con_skip();
	con_str("RESET", after_s > 20 ? "in 40s" : "in 20s");
	delay_ms(after_s * 1000);
	wr32(IMEM_RESTART_REASON, RESTART_BOOTLOADER);
	__asm__ volatile("dsb sy" ::: "memory");
	smc(PSCI_SYSTEM_RESET, 0, 0, 0);
	wr32(PSHOLD, 0);
	for (;;)
		__asm__ volatile("wfe");
}

/* ---- exceptions ---- */

static void irq_handler(void)
{
	uint64_t iar = rsr("icc_iar1_el1");
	uint32_t id = iar & 0xffffff;

	if (id >= 1020 && id <= 1023)
		return;	/* spurious */
	irq_last = id;
	if (id == (uint32_t)irq_src)
		irq_count++;
	else
		irq_other++;
	if (id == VTIMER_INTID)
		wsr("cntv_tval_el0", rsr("cntfrq_el0") / 100);	/* 10 ms */
	else if (id == PTIMER_INTID)
		wsr("cntp_tval_el0", rsr("cntfrq_el0") / 100);
	wsr("icc_eoir1_el1", iar);
	isb();
}

void exc_handler(uint64_t slot, uint64_t *regs)
{
	if (slot == 5) {	/* IRQ from current EL, SPx */
		irq_handler();
		return;
	}
	con_clear(BLUE);
	switch (slot & 3) {
	case 0: con_str("PANIC", "SYNC"); break;
	case 1: con_str("PANIC", "IRQ"); break;
	case 2: con_str("PANIC", "FIQ"); break;
	default: con_str("PANIC", "SERROR"); break;
	}
	con_hex2("SLOT", "n=", slot, "cpu=", rsr("mpidr_el1"));
	con_str("STAGE", stage);
	con_hex("ESR", rsr("esr_el1"));
	con_hex("ELR", rsr("elr_el1"));
	con_hex("FAR", rsr("far_el1"));
	con_hex("SPSR", rsr("spsr_el1"));
	con_hex("X0", regs[0]);
	con_hex("X1", regs[1]);
	con_hex("X30", regs[31]);	/* saved x29/x30 follow the 30-word frame */
#ifdef V60_DIAG
	con_str("PANIC", "halted; photograph this screen");
	for (;;) __asm__ volatile("wfe");
#else
	reset(40);
#endif
}

/* ---- SMP ---- */

static uintptr_t find_gicr(void);

void secondary_main(uint64_t idx)
{
	sec_midr[idx] = rsr("midr_el1");
	sec_mpidr[idx] = rsr("mpidr_el1") | (1ULL << 63);
	if (idx == 1) {
#ifndef V60_DIAG
		uintptr_t r = find_gicr();
		if (r) {
			sec_probe[0] = rd32(r + GICR_IGROUPR0);
			wr32(r + GICR_IGROUPR0, 0xffffffff);
			sec_probe[1] = rd32(r + GICR_IGROUPR0);
			wr8(r + GICR_IPRIORITYR + 19, 0xa0);
			sec_probe[2] = rd32(r + GICR_IPRIORITYR + 16);
		}
		sec_probe[3] = r ? 1 : 0xdead;
#endif
	}
	__asm__ volatile("dsb sy" ::: "memory");
	smc(PSCI_CPU_OFF, 0, 0, 0);
}

static void __attribute__((unused)) test_smp(void)
{
	stage = "smp";
	uint64_t self = rsr("mpidr_el1") & 0xffffff;
	int64_t ret[NR_CPUS];

	for (int i = 1; i < NR_CPUS; i++)
		ret[i] = smc(PSCI_CPU_ON, (uint64_t)i << 8, (uintptr_t)secondary_entry, i);
	delay_ms(500);
	for (int i = 1; i < NR_CPUS; i++) {
		char label[] = "CPU0";
		label[3] = '0' + i;
		if (((uint64_t)i << 8) == self)
			con_str(label, "is boot cpu");
		else
			con_hex2(label, "ret=", (uint32_t)ret[i], "mpidr=",
				 (uint32_t)sec_mpidr[i] | (sec_mpidr[i] ? 0 : 0xdead0000));
	}
}

/* ---- GIC + timer ---- */

static uintptr_t find_gicr(void)
{
	uint64_t aff = rsr("mpidr_el1");
	uint32_t want = (aff & 0xffffff) | ((aff >> 8) & 0xff000000);

	for (uintptr_t off = 0; off < GICR_SIZE; off += GICR_PE_SIZE) {
		uint64_t typer = rd64(GICR_BASE + off + GICR_TYPER);
		if ((uint32_t)(typer >> 32) == want)
			return GICR_BASE + off;
		if (typer & (1 << 4))	/* Last */
			break;
	}
	return 0;
}

static void gicd_wait_rwp(void)
{
	for (int i = 0; i < 1000000 && (rd32(GICD_BASE) & (1U << 31)); i++)
		;
}

/* Run one interrupt source for ~300 ms and report what the GIC saw. */
static void irq_window(const char *label, uintptr_t r, int src)
{
	uint64_t f = rsr("cntfrq_el0");
	uint32_t pend = 0, act = 0, isr = 0, hppi = 0x3ff, ist = 0;

	irq_count = irq_other = 0;
	irq_last = 0x3ff;
	irq_src = src;
	if (src == VTIMER_INTID) {
		wsr("cntv_tval_el0", f / 100);
		wsr("cntv_ctl_el0", 1);
	} else if (src == PTIMER_INTID) {
		wsr("cntp_tval_el0", f / 100);
		wsr("cntp_ctl_el0", 1);
	}
	isb();
	/* sample state with IRQs still masked, after the first period */
	uint64_t t = now() + f / 50;
	while (now() < t)
		;
	if (src == SGI_INTID)
		wsr("icc_sgi1r_el1", sgi_self(SGI_INTID));
	isb();
	ist = src == PTIMER_INTID ? rsr("cntp_ctl_el0") : rsr("cntv_ctl_el0");
	pend = rd32(r + GICR_ISPENDR0);
	act = rd32(r + GICR_ISACTIVER0);
	isr = rsr("isr_el1");
	hppi = rsr("icc_hppir1_el1");

	__asm__ volatile("msr daifclr, #2" ::: "memory");
	t = now() + f * 3 / 10;
	/* SGI: one already sent above, send 9 more 10 ms apart -> expect 10 */
	uint64_t next = now() + f / 100;
	for (int sent = 1; now() < t;)
		if (src == SGI_INTID && sent < 10 && now() >= next) {
			wsr("icc_sgi1r_el1", sgi_self(SGI_INTID));
			isb();
			sent++;
			next += f / 100;
		}
	__asm__ volatile("msr daifset, #2" ::: "memory");
	wsr("cntv_ctl_el0", 0);
	wsr("cntp_ctl_el0", 0);
	isb();

	con_hex2(label, "irqs=", irq_count, "other=", irq_other);
	con_hex2(" pre", "pend=", pend, "act=", act);
	con_hex2(" pre", "isr=", isr, "hppi=", hppi);
	con_hex2(" pre", "tctl=", ist, "last=", irq_last);
}

static int test_gic(void)
{
	stage = "gic sre";
	uint64_t sre = rsr("icc_sre_el1");
	if (!(sre & 1)) {
		wsr("icc_sre_el1", sre | 1);
		isb();
	}
	if (!(rsr("icc_sre_el1") & 1)) {
		con_str("GIC", "no sysreg iface, skip");
		return 0;
	}

	stage = "gic dist";
	uint32_t dctlr = rd32(GICD_BASE);
	/* Non-secure view (DS=0): bit0 EnableGrp1, bit1 EnableGrp1A, bit4 ARE_NS; Linux writes 0x13 */
	wr32(GICD_BASE, dctlr | 0x13);
	gicd_wait_rwp();
	con_hex2("GICD", "was=", dctlr, "now=", rd32(GICD_BASE));

	stage = "gic redist";
	uintptr_t r = find_gicr();
	if (!r) {
		con_str("GICR", "not found");
		return 0;
	}
	uint32_t waker = rd32(r + GICR_WAKER);
	if (waker & 2) {
		wr32(r + GICR_WAKER, waker & ~2U);
		for (int i = 0; i < 100000 && (rd32(r + GICR_WAKER) & 4); i++)
			;
	}
	con_hex2("GICR", "base=", r, "waker=", rd32(r + GICR_WAKER));

	stage = "gic config";
	uint32_t grp = rd32(r + GICR_IGROUPR0);
	wr32(r + GICR_IGROUPR0, 0xffffffff);	/* NS view: secure bits are WI */
	uint32_t en_before = rd32(r + GICR_ISENABLER0);
	uint32_t pri_before = rd32(r + GICR_IPRIORITYR + 16);	/* INTIDs 16..19 */
	for (int id = 0; id < 32; id++)
		wr8(r + GICR_IPRIORITYR + id, 0xa0);
	wr32(r + GICR_ISENABLER0, (1U << VTIMER_INTID) | (1U << PTIMER_INTID) | (1U << SGI_INTID));
	con_hex2("ENABLE", "was=", en_before, "now=", rd32(r + GICR_ISENABLER0));
	(void)grp;
	(void)pri_before;

	uint64_t ctlr = rsr("icc_ctlr_el1");
	wsr("icc_ctlr_el1", ctlr & ~2ULL);	/* EOImode 0: EOIR also deactivates */
	wsr("icc_pmr_el1", 0xf0);
	wsr("icc_bpr1_el1", 0);
	wsr("icc_igrpen1_el1", 1);
	isb();
	con_hex2("ICC", "ctlr=", ctlr, "grpen1=", rsr("icc_igrpen1_el1"));

	stage = "gic vtimer";
	irq_window("VTIMER", r, VTIMER_INTID);
	uint32_t virtual_irqs = irq_count;
	stage = "gic ptimer";
	irq_window("PTIMER", r, PTIMER_INTID);
	uint32_t physical_irqs = irq_count;
	stage = "gic sgi";
	irq_window("SGI1", r, SGI_INTID);
	uint32_t sgi_irqs = irq_count;
	wr32(r + GICR_ICENABLER0, 0xffffffff);
	isb();
	return virtual_irqs > 0 && physical_irqs > 0 && sgi_irqs == 10;
}

/* ---- main ---- */

static void __attribute__((unused)) psci_line(const char *label, uint64_t fn)
{
	con_hex2(label, "feat=", smc(PSCI_FEATURES, fn, 0, 0), 0, 0);
}

static uint64_t scratch[8192];	/* 64KB, lives in our (later cacheable) image */

static uint64_t ticks_fill(uint32_t c)
{
	uint64_t t = now();
	con_clear(c);
	return now() - t;
}

static uint64_t ticks_mem(void)
{
	uint64_t t = now();
	for (int pass = 0; pass < 16; pass++)
		for (int i = 0; i < 8192; i++)
			scratch[i] = scratch[i] * 3 + i;
	return now() - t;
}

static uint32_t to_us(uint64_t ticks)
{
	return ticks * 1000000 / rsr("cntfrq_el0");
}

#ifdef V60_DIAG
static void __attribute__((noreturn)) diag_halt(void)
{
	con_str("END", "halted; photograph this screen");
	for (;;) __asm__ volatile("wfe");
}

/* test2b: keep IRQ masked; isolate MMU stability from GIC register access. */
static void __attribute__((unused)) diag_step(const char *name)
{
	stage = name;
	con_str("STEP", name);
	/* Make the breadcrumb visible before attempting the next operation. */
	__asm__ volatile("dsb sy" ::: "memory");
	delay_ms(2000);
}

static void __attribute__((unused, noreturn)) v60_read_diag(void)
{
	con_clear(RED);
#if V60_DIAG == 4
	con_str("V60", "test2b MMU hold / IRQ masked");
#else
	con_str("V60", "test2b GIC reads / IRQ masked");
#endif
	con_hex("EL", rsr("CurrentEL"));
	con_hex("SCTLR0", rsr("sctlr_el1"));
	if ((((rsr("id_aa64mmfr0_el1") >> 20) & 15) == 15) ||
	    (rsr("sctlr_el1") & 5)) {
		con_str("STOP", "16K absent or MMU/cache already on");
		diag_halt();
	}
	struct mmu_info mi = {0};
	diag_step("enable 16K MMU");
	int rc = mmu_enable(&mi);
	con_hex2("MMU", "rc=", rc, "l3=", mi.n_l3);
	con_hex("SCTLR1", rsr("sctlr_el1"));
	if (rc) diag_halt();
#if V60_DIAG == 4
	con_str("INFO", "no GIC access; alive every 5 s");
	stage = "mmu hold";
	for (unsigned seconds = 0; seconds <= 120; seconds += 5) {
		con_hex2("ALIVE", "seconds=", seconds, 0, 0);
		__asm__ volatile("dsb sy" ::: "memory");
		if (seconds < 120) delay_ms(5000);
	}
	con_str("RESULT", "MMU hold 120 s complete");
#else
	diag_step("read ICC_SRE_EL1");
	con_hex("SRE", rsr("icc_sre_el1"));
	diag_step("read GICD_CTLR / TYPER / IIDR");
	con_hex2("GICD", "ctlr=", rd32(GICD_BASE), "typer=", rd32(GICD_BASE + 4));
	con_hex2("GICD", "iidr=", rd32(GICD_BASE + 8), 0, 0);
	uint64_t aff = rsr("mpidr_el1");
	uint32_t want = (aff & 0xffffff) | ((aff >> 8) & 0xff000000);
	uintptr_t r = 0;
	for (uintptr_t off = 0; off < GICR_SIZE; off += GICR_PE_SIZE) {
		stage = "read GICR_TYPER";
		/* One breadcrumb per redistributor, kept on the same screen. */
		con_hex2("READ R", "base=", GICR_BASE + off, 0, 0);
		__asm__ volatile("dsb sy" ::: "memory");
		delay_ms(1000);
		uint64_t typer = rd64(GICR_BASE + off + GICR_TYPER);
		con_hex("TYPER", typer);
		if ((uint32_t)(typer >> 32) == want) {
			r = GICR_BASE + off;
			break;
		}
		if (typer & (1 << 4)) break;
	}
	if (!r) {
		con_str("STOP", "boot CPU redistributor not found");
		diag_halt();
	}
	diag_step("read GICR_WAKER / CTLR");
	con_hex2("GICR", "waker=", rd32(r + GICR_WAKER), "ctlr=", rd32(r));
	diag_step("read PPI group / enable / pending");
	con_hex2("PPI", "group=", rd32(r + GICR_IGROUPR0), "enable=", rd32(r + GICR_ISENABLER0));
	con_hex2("PPI", "pending=", rd32(r + GICR_ISPENDR0), "active=", rd32(r + GICR_ISACTIVER0));
	con_str("RESULT", "GIC register reads complete");
	con_str("INFO", "no GIC writes / no IRQ delivery test");
#endif
	diag_halt();
}

static void __attribute__((noreturn)) v60_diag(uint64_t dtb)
{
#if V60_DIAG == 3 || V60_DIAG == 4
	(void)dtb;
	v60_read_diag();
#endif
	stage = "v60 cpu registers";
	con_clear(RED);
#if V60_DIAG == 2
	con_str("V60", "test2 SMP (MMU off)");
#else
	con_str("V60", "test2 CPU / MMU / IRQ");
#endif
	con_str("PAGE", "1: registers (15 seconds)");
	con_hex("DTB", dtb);
	con_hex("EL", rsr("CurrentEL"));
	con_hex("MPIDR", rsr("mpidr_el1"));
	con_hex("MIDR", rsr("midr_el1"));
	con_hex("MMFR0", rsr("id_aa64mmfr0_el1"));
	con_hex("PFR0", rsr("id_aa64pfr0_el1"));
	con_hex("CNTFRQ", rsr("cntfrq_el0"));
	con_hex("SCTLR", rsr("sctlr_el1"));
	con_hex2("PSCI", "ver=", smc(PSCI_VERSION, 0, 0, 0),
		"smccc=", smc(SMCCC_VERSION, 0, 0, 0));
	psci_line("CPU_ON", PSCI_CPU_ON);
	con_str("INFO", "no storage access / no payload");
	delay_ms(15000);
#if V60_DIAG == 2
	con_clear(RED);
	con_str("V60", "test2 SMP / CPU_ON + CPU_OFF");
	con_str("INFO", "CPU0..7 MPIDR = index << 8");
	stage = "v60 smp";
	uint64_t self = rsr("mpidr_el1") & 0xffffff;
	/* MMU/caches remain off for shared reports. No secondary GIC writes. */
	for (unsigned i = 0; i < NR_CPUS; i++) {
		char label[] = "CPU0";
		label[3] = '0' + i;
		if (((uint64_t)i << 8) == self) {
			con_str(label, "boot CPU: skipped");
			continue;
		}
		con_str(label, "calling CPU_ON");
		int64_t rc = smc(PSCI_CPU_ON, (uint64_t)i << 8,
			(uintptr_t)secondary_entry, i);
		uint64_t end = now() + rsr("cntfrq_el0") / 2;
		while (!sec_mpidr[i] && now() < end) ;
		con_hex2(label, "ret=", (uint32_t)rc, "midr=", sec_midr[i]);
		con_hex("MPIDR", sec_mpidr[i]);
	}
	diag_halt();
#else
	con_clear(RED);
	con_str("V60", "test2 MMU (16K granule)");
	con_str("PAGE", "2: MMU (15 seconds)");
	uint64_t tg16 = (rsr("id_aa64mmfr0_el1") >> 20) & 15;
	if (tg16 == 15 || (rsr("sctlr_el1") & 5)) {
		con_str("STOP", "16K absent or MMU/cache already on");
		diag_halt();
	}
	struct mmu_info mi = {0};
	stage = "v60 mmu enable";
	int rc = mmu_enable(&mi);
	con_hex2("MMU", "rc=", rc, "l3=", mi.n_l3);
	con_hex("IMG LO", mi.img_lo);
	con_hex("IMG HI", mi.img_hi);
	con_hex("TTBR0", mi.ttbr);
	con_hex("SCTLR0", mi.sctlr_before);
	con_hex("SCTLR1", mi.sctlr_after);
	con_hex("TCR", rsr("tcr_el1"));
	con_hex("PAR IMG", mmu_probe((uintptr_t)v60_diag));
	con_hex("PAR FB", mmu_probe(FB_BASE));
	con_hex("PAR GIC", mmu_probe(GICD_BASE));
	if (rc) diag_halt();
	delay_ms(15000);
	con_clear(RED);
	con_str("V60", "test2 GICv3 / timers");
	con_str("PAGE", "3: IRQ results (stays on screen)");
	con_hex2("INTID", "virt=", VTIMER_INTID, "phys=", PTIMER_INTID);
	int ok = test_gic();
	con_str("RESULT", ok ? "PASS: both timers + 10 SGIs" : "CHECK IRQ counters / panic");
	stage = "v60 diag done";
	diag_halt();
#endif
}
#endif

void shim_main(uint64_t dtb)
{
#ifdef V60_DIAG
	v60_diag(dtb);
#endif
	struct mmu_info mi;

	/*
	 * V60 bring-up test 1: does writing the cont_splash framebuffer change what
	 * the SW43103 command-mode panel shows? Three solid colours, 3 s each.
	 * If the screen never changes, the panel needs an MDP/DSI kick per frame.
	 */
	stage = "fbtest";
	con_clear(RED);
	delay_ms(3000);
	con_clear(GREEN);
	delay_ms(3000);
	con_clear(BLUE);
	delay_ms(3000);

	/* An xnu payload in the ramdisk means: boot it (MMU stays off). */
	stage = "loader";
	con_clear(RED);
	con_str("SHIM", "V60 test: fb + ufs probe");
	con_skip();
	const char *why = loader_boot(dtb);
	con_str("LOADER", why);
	con_skip();

	/* No payload: probe the internal UFS while the MMU is still off. */
	stage = "ufs";
	test_ufs();
	con_skip();
	/* Keep the probe results on screen (later tests clear it); reset after 3 min. */
	con_str("UFS", "probe done, reset in ~3 min");
	delay_ms(180000);
	reset(20);

	stage = "fill off";
	uint64_t fill_off = ticks_fill(RED);
	uint64_t mem_off = ticks_mem();

	stage = "mmu enable";
	int rc = mmu_enable(&mi);

	stage = "fill on";
	uint64_t fill_on = rc ? 0 : ticks_fill(RED);
	uint64_t mem_on = rc ? 0 : ticks_mem();
	if (rc)
		con_clear(RED);

	con_str("SHIM", "sweet milestone 3");
	con_skip();
	con_hex2("DTB", "at=", dtb, "magic=", dtb ? __builtin_bswap32(rd32(dtb)) : 0);
	con_hex2("IMAGE", "lo=", mi.img_lo, "hi=", mi.img_hi);
	con_hex2("MMU", "rc=", rc, "l3=", mi.n_l3);
	con_hex2("TTBR0", "pa=", mi.ttbr, "pa_hi=", mi.ttbr >> 32);
	con_hex("SCTLR0", mi.sctlr_before);
	con_hex("SCTLR1", mi.sctlr_after);
	con_hex("TCR", rsr("tcr_el1"));
	con_hex2("PARANGE", "mmfr0=", rsr("id_aa64mmfr0_el1") & 0xf, "tg16=", (rsr("id_aa64mmfr0_el1") >> 20) & 0xf);
	con_skip();
	con_hex("PAR img", mmu_probe((uintptr_t)shim_main));
	con_hex("PAR fb", mmu_probe(FB_PHYS + 0x1000));
	con_hex("PAR gic", mmu_probe(GICD_BASE));
	con_hex("PAR 12G", mmu_probe(0x300000000UL));
	con_skip();
	con_hex2("FILL us", "off=", to_us(fill_off), "on=", to_us(fill_on));
	con_hex2("MEM us", "off=", to_us(mem_off), "on=", to_us(mem_on));
	con_skip();

	stage = "psci";
	con_hex2("PSCI", "ver=", smc(PSCI_VERSION, 0, 0, 0), "smccc=", smc(SMCCC_VERSION, 0, 0, 0));
	con_skip();
	test_gic();

	stage = "done";
	reset(40);
}
