/*
 * Milestone 5 probe: can we talk to the internal UFS that ABL booted from?
 *
 * Runs with the MMU off (all memory Device, so DMA is coherent with no cache
 * maintenance). Dumps the UFSHCI registers ABL left behind and, if the host
 * is enabled and the link is up, points the transfer request list at our own
 * buffers and issues READ-ONLY SCSI commands on LUN 0. Nothing is ever
 * written to the device: there is no SCSI WRITE in this file.
 *
 * Register layout and descriptor formats: JEDEC UFSHCI (JESD223).
 */
#include <stdint.h>
#include "console.h"
#include "ufs.h"
#ifdef V60_EXTERNAL_ROOT
#include "external_root.h"
#endif

#define UFS_BASE	0x01d84000UL	/* ufshc@1d84000, sweet live FDT */

#define REG_CAP		0x00
#define REG_VER		0x08
#define REG_IS		0x20
#define REG_HCS		0x30
#define REG_HCE		0x34
#define REG_UTRLBA	0x50
#define REG_UTRLBAU	0x54
#define REG_UTRLDBR	0x58
#define REG_UTRLCLR	0x5c
#define REG_UTRLRSR	0x60

#define HCS_DP		(1u << 0)
#define HCS_UTRLRDY	(1u << 1)
#define HCS_UCRDY	(1u << 3)

#define BLOCK		4096u

static inline uint32_t ufs_rd(uint32_t off) { return *(volatile uint32_t *)(UFS_BASE + off); }
static inline void ufs_wr(uint32_t off, uint32_t v) { *(volatile uint32_t *)(UFS_BASE + off) = v; }

/* UTP transfer request descriptor (32 bytes). */
struct utrd {
	uint32_t dw0;		/* [31:28] CT, [26:25] DD, [24] I */
	uint32_t dw1;
	uint32_t dw2;		/* [7:0] OCS */
	uint32_t dw3;
	uint32_t ucdba;		/* command descriptor base, 128-byte aligned */
	uint32_t ucdbau;
	uint32_t resp;		/* [31:16] response UPIU offset, [15:0] length (DWORDs) */
	uint32_t prdt;		/* [31:16] PRDT offset, [15:0] entries (DWORDs/entries) */
};

/* UTP command descriptor: command UPIU, response UPIU, PRDT. */
struct ucd {
	uint8_t cmd[512];
	uint8_t resp[512];
	struct {
		uint32_t addr_lo, addr_hi, rsvd, dbc;	/* dbc = byte count - 1 */
	} prdt[1];
};

static struct utrd utrl[32] __attribute__((aligned(1024)));
static struct ucd ucd __attribute__((aligned(128)));
static uint8_t data[2 * BLOCK] __attribute__((aligned(4096)));

static uint64_t ticks(void)
{
	uint64_t t;
	__asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(t));
	return t;
}

static void zero(void *p, unsigned n)
{
	volatile uint8_t *b = p;
	for (unsigned i = 0; i < n; i++)
		b[i] = 0;
}

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/*
 * Issue one SCSI command (read direction or no data) in slot 0.
 * Returns 0 on success, otherwise a code describing the failure stage.
 */
static uint32_t scsi(uint8_t lun, const uint8_t *cdb, unsigned cdb_len, void *buf, uint32_t len,
    uint32_t *ocs_out, uint32_t *status_out)
{
	static uint8_t tag;
	zero(&utrl[0], sizeof(utrl[0]));
	zero(&ucd, sizeof(ucd));

	/* Command UPIU: header (12 bytes), expected length, CDB. */
	ucd.cmd[0] = 0x01;			/* transaction type: COMMAND */
	ucd.cmd[1] = len ? 0x40 : 0x00;		/* flags: R (data in) */
	ucd.cmd[2] = lun;
	ucd.cmd[3] = ++tag;
	ucd.cmd[12] = len >> 24;
	ucd.cmd[13] = len >> 16;
	ucd.cmd[14] = len >> 8;
	ucd.cmd[15] = len;
	for (unsigned i = 0; i < cdb_len; i++)
		ucd.cmd[16 + i] = cdb[i];

	uintptr_t ucd_pa = (uintptr_t)&ucd;
	utrl[0].dw0 = (1u << 28) | ((len ? 2u : 0u) << 25) | (1u << 24);	/* UFS storage, dev->host */
	utrl[0].dw2 = 0x0f;			/* OCS: invalid until the host updates it */
	utrl[0].ucdba = (uint32_t)ucd_pa;
	utrl[0].ucdbau = (uint32_t)(ucd_pa >> 32);
	utrl[0].resp = (512u / 4) << 16 | (512u / 4);
	if (len) {
		uintptr_t buf_pa = (uintptr_t)buf;
		ucd.prdt[0].addr_lo = (uint32_t)buf_pa;
		ucd.prdt[0].addr_hi = (uint32_t)(buf_pa >> 32);
		ucd.prdt[0].dbc = len - 1;
		utrl[0].prdt = (1024u / 4) << 16 | 1;
	}
	__asm__ volatile("dsb sy" ::: "memory");

	ufs_wr(REG_UTRLDBR, 1);
	uint64_t start = ticks();
	while (ufs_rd(REG_UTRLDBR) & 1) {
		if (ticks() - start > 19200000) {	/* 1 s */
			ufs_wr(REG_UTRLCLR, ~1u);	/* clear slot 0 (write 0 to the bit) */
			return 1;			/* timeout */
		}
	}
	__asm__ volatile("dsb sy" ::: "memory");
	*ocs_out = utrl[0].dw2 & 0xff;
	*status_out = ucd.resp[7];		/* SCSI status in the response UPIU */
	if (*ocs_out)
		return 2;
	if (ucd.resp[0] != 0x21)		/* RESPONSE UPIU */
		return 3;
	return *status_out ? 4 : 0;
}

static uint32_t read_blocks(uint32_t lba, uint32_t count, void *buf)
{
	uint8_t cdb[10] = { 0x28, 0, lba >> 24, lba >> 16, lba >> 8, lba, 0, count >> 8, count, 0 };
	uint32_t ocs, st;
	return scsi(0, cdb, sizeof(cdb), buf, count * BLOCK, &ocs, &st);
}

static uint64_t le64(const uint8_t *p)
{
	uint64_t v = 0;
	for (int i = 7; i >= 0; i--)
		v = v << 8 | p[i];
	return v;
}

/* Name the filesystem whose superblock starts at block lba (f2fs/ext4). */
static const char *fs_type(uint32_t lba)
{
	if (read_blocks(lba, 1, data))
		return "read error";
	const uint8_t *sb = data + 1024;
	if (sb[0] == 0x10 && sb[1] == 0x20 && sb[2] == 0xf5 && sb[3] == 0xf2)
		return "f2fs";
	if (sb[56] == 0x53 && sb[57] == 0xef)
		return "ext4";
	return "other";
}

/* One line per GPT entry of at least 64 MiB: name, first LBA, size in MiB (4K blocks). */
static void list_partitions(uint32_t entry_lba, uint32_t nent)
{
	static uint8_t ents[128 * 128] __attribute__((aligned(4096)));
	uint32_t blocks = (nent * 128 + BLOCK - 1) / BLOCK;
	if (read_blocks(entry_lba, blocks, ents)) {
		con_str("UFS PART", "read error");
		return;
	}
	for (uint32_t i = 0; i < nent; i++) {
		const uint8_t *e = ents + i * 128;
		uint64_t first = le64(e + 32), last = le64(e + 40);
		if (!first)
			continue;
		char name[17];
		for (int c = 0; c < 16; c++) {
			uint8_t ch = e[56 + 2 * c];
			name[c] = (ch >= 32 && ch < 127) ? ch : (ch ? '?' : 0);
		}
		name[16] = 0;
		if (((last - first + 1) >> 8) < 64)	/* keep the screen to the big ones */
			continue;
		con_hex2(name, "lba=", (uint32_t)first, "MiB=", (uint32_t)((last - first + 1) >> 8));
		if (!__builtin_strcmp(name, "userdata") || !__builtin_strcmp(name, "cust") ||
		    !__builtin_strcmp(name, "cache"))
			con_str(" fs", fs_type((uint32_t)first));
	}
}

void test_ufs(void)
{
	con_str("UFS", "milestone 5 probe (read-only)");
	uint32_t cap = ufs_rd(REG_CAP), hcs = ufs_rd(REG_HCS), hce = ufs_rd(REG_HCE);
	con_hex2("UFS", "ver=", ufs_rd(REG_VER), "cap=", cap);
	con_hex2("UFS", "hce=", hce, "hcs=", hcs);
	con_hex2("UFS", "is=", ufs_rd(REG_IS), "rsr=", ufs_rd(REG_UTRLRSR));
	con_hex2("UFS", "utrlba=", ufs_rd(REG_UTRLBA), "hi=", ufs_rd(REG_UTRLBAU));
	con_hex2("UFS", "dbr=", ufs_rd(REG_UTRLDBR), "nutrs=", (cap & 0x1f) + 1);

	if (!(hce & 1) || !(hcs & HCS_DP) || !(hcs & HCS_UTRLRDY)) {
		con_str("UFS", "host not enabled/ready: stop");
		return;
	}
	if (ufs_rd(REG_UTRLDBR)) {
		con_str("UFS", "requests outstanding: stop");
		return;
	}

	/* Take over the transfer request list: stop, point at ours, run. */
	ufs_wr(REG_UTRLRSR, 0);
	uintptr_t list = (uintptr_t)utrl;
	ufs_wr(REG_UTRLBA, (uint32_t)list);
	ufs_wr(REG_UTRLBAU, (uint32_t)(list >> 32));
	ufs_wr(REG_UTRLRSR, 1);
	con_hex2("UFS", "ours=", (uint32_t)list, "rsr=", ufs_rd(REG_UTRLRSR));
	/*
	 * Never ring a doorbell unless the host really uses our list: otherwise
	 * it would execute whatever ABL left in its own slot 0 (maybe a write).
	 */
	if (ufs_rd(REG_UTRLBA) != (uint32_t)list || ufs_rd(REG_UTRLBAU) != (uint32_t)(list >> 32) ||
	    !(ufs_rd(REG_UTRLRSR) & 1)) {
		con_str("UFS", "list base not taken: stop");
		return;
	}

	uint32_t ocs = 0, st = 0, rc;
	static const uint8_t tur[6] = { 0x00 };
	/* A first command may report a unit attention; retry a few times. */
	for (int i = 0; i < 3; i++) {
		rc = scsi(0, tur, sizeof(tur), 0, 0, &ocs, &st);
		con_hex2("UFS TUR", "rc=", rc, "ocs/st=", ocs << 8 | st);
		if (!rc)
			break;
	}

	static const uint8_t cap10[10] = { 0x25 };	/* READ CAPACITY(10) */
	rc = scsi(0, cap10, sizeof(cap10), data, 8, &ocs, &st);
	con_hex2("UFS CAP", "rc=", rc, "ocs/st=", ocs << 8 | st);
	if (!rc)
		con_hex2("UFS LUN0", "lastlba=", be32(data), "blk=", be32(data + 4));

	/* READ(10) LBA 0, 2 blocks: protective MBR and the GPT header. */
	static const uint8_t rd10[10] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 2, 0 };
	rc = scsi(0, rd10, sizeof(rd10), data, 2 * BLOCK, &ocs, &st);
	con_hex2("UFS READ", "rc=", rc, "ocs/st=", ocs << 8 | st);
	if (rc)
		return;
	con_hex2("UFS MBR", "sig=", data[510] | data[511] << 8, "type=", data[450]);
	const uint8_t *gpt = data + BLOCK;
	char sig[9];
	for (int i = 0; i < 8; i++)
		sig[i] = (gpt[i] >= 32 && gpt[i] < 127) ? gpt[i] : '.';
	sig[8] = 0;
	con_str("UFS GPT", sig);
	uint32_t nent = gpt[80] | gpt[81] << 8 | gpt[82] << 16, esz = gpt[84] | gpt[85] << 8;
	uint32_t elba = gpt[72] | gpt[73] << 8;
	con_hex2("UFS GPT", "entries=", nent, "lba=", elba);
	if (esz != 128 || nent > 128)
		return;
	list_partitions(elba, nent);
}

#ifdef V60_EXTERNAL_ROOT
/* Separate from the historical probe: strict GPT/geometry validation and a
 * bounded 128KiB DMA bounce buffer below 4GiB. No SCSI write commands exist.
 */
static uint8_t bounce[128 * 1024] __attribute__((aligned(4096)));
static uint8_t partition_entries[128 * 128] __attribute__((aligned(4096)));
static uint32_t le32(const uint8_t *p)
{ return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int userdata_name(const uint8_t *e)
{
    const char *name = "userdata";
    for (unsigned i = 0; i < 8; i++)
        if (e[56 + 2*i] != (uint8_t)name[i] || e[57 + 2*i]) return 0;
    return e[72] == 0 && e[73] == 0;
}
static const char *external_read(const struct v60_external_root *d)
{
    uint32_t ocs, status;
    const uint8_t tur[6] = {0};
    unsigned retry;
    for (retry = 0; retry < 3; retry++)
        if (!scsi(0, tur, 6, 0, 0, &ocs, &status)) break;
    if (retry == 3) return "external: UFS unit not ready";
    const uint8_t cap10[10] = {0x25};
    if (scsi(0, cap10, 10, data, 8, &ocs, &status) || be32(data + 4) != BLOCK ||
        d->last_lba >= be32(data)) return "external: capacity mismatch";
    if (read_blocks(1, 1, data)) return "external: GPT read failed";
    const char *sig = "EFI PART";
    for (unsigned i = 0; i < 8; i++) if (data[i] != (uint8_t)sig[i]) return "external: GPT signature";
    uint32_t size = le32(data + 12), expected_crc = le32(data + 16);
    if (size < 92 || size > BLOCK || le64(data + 24) != 1) return "external: GPT header bounds";
    for (unsigned i = 16; i < 20; i++) data[i] = 0;
    if ((v60_crc(~0u, data, size) ^ ~0u) != expected_crc) return "external: GPT header CRC";
    uint64_t entries_lba = le64(data + 72);
    uint32_t count = le32(data + 80), entry_size = le32(data + 84), array_crc = le32(data + 88);
    if (entries_lba != 2 || !count || count > 128 || entry_size != 128 ||
        le64(data + 40) > d->first_lba || le64(data + 48) < d->last_lba)
        return "external: GPT layout mismatch";
    if (read_blocks(2, (count * 128 + BLOCK - 1) / BLOCK, partition_entries))
        return "external: partition entries read failed";
    if ((v60_crc(~0u, partition_entries, count * 128) ^ ~0u) != array_crc)
        return "external: partition entries CRC";
    unsigned matches = 0;
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *e = partition_entries + i * 128;
        if (userdata_name(e)) {
            if (le64(e + 32) != d->first_lba || le64(e + 40) != d->last_lba)
                return "external: userdata bounds changed";
            matches++;
        }
    }
    if (matches != 1) return "external: userdata not unique";
    if (read_blocks((uint32_t)d->first_lba, 1, data)) return "external: HFS header read failed";
    uint8_t *h = data + 1024;
    if (h[0] != 'H' || h[1] != 'X' || be32(h + 40) != BLOCK ||
        (uint64_t)be32(h + 44) * BLOCK != d->hfs_bytes) return "external: existing HFS geometry changed";
    uint64_t isa;
    __asm__ volatile("mrs %0, id_aa64isar0_el1" : "=r"(isa));
    if (((isa >> 16) & 15) == 0) return "external: CPU CRC extension absent";
    uint64_t done = 0;
    uint32_t crc = ~0u, last_percent = 101;
    while (done < d->bytes) {
        uint32_t bytes = d->bytes - done > sizeof(bounce) ? sizeof(bounce) : (uint32_t)(d->bytes - done);
        uint64_t lba = d->first_lba + (d->relative + done) / BLOCK;
        if (read_blocks((uint32_t)lba, bytes / BLOCK, bounce)) return "external: APFS read failed";
        if (!done && (le32(bounce + 32) != 0x4253584e || le32(bounce + 36) != BLOCK))
            return "external: APFS superblock mismatch";
        volatile uint64_t *dest = (volatile uint64_t *)(uintptr_t)(d->destination + done);
        volatile const uint64_t *src = (volatile const uint64_t *)bounce;
        for (unsigned i = 0; i < bytes / 8; i++) {
            uint64_t word = src[i];
            dest[i] = word;
            __asm__ volatile(".arch_extension crc\ncrc32x %w0, %w0, %1" : "+r"(crc) : "r"(word));
        }
        done += bytes;
        uint32_t percent = (uint32_t)(done * 100 / d->bytes);
        if (percent / 10 != last_percent / 10) {
            con_hex2("V60 APFS", "percent=", percent, "MiB=", (uint32_t)(done >> 20));
            last_percent = percent;
        }
    }
    __asm__ volatile("dsb sy" ::: "memory");
    if ((crc ^ ~0u) != d->image_crc) return "external: APFS CRC mismatch";
    return 0;
}
const char *v60_load_external(const struct v60_external_root *d)
{
    if (!(ufs_rd(REG_HCE) & 1) || (ufs_rd(REG_HCS) & (HCS_DP | HCS_UTRLRDY)) !=
        (HCS_DP | HCS_UTRLRDY) || ufs_rd(REG_UTRLDBR)) return "external: UFS host busy/not ready";
    if ((uintptr_t)bounce + sizeof(bounce) > (1ull << 32)) return "external: DMA bounce buffer above 4GiB";
    uint32_t old_lo = ufs_rd(REG_UTRLBA), old_hi = ufs_rd(REG_UTRLBAU), old_run = ufs_rd(REG_UTRLRSR);
    ufs_wr(REG_UTRLRSR, 0);
    if (ufs_rd(REG_UTRLRSR) & 1) return "external: cannot stop transfer list";
    uintptr_t list = (uintptr_t)utrl;
    ufs_wr(REG_UTRLBA, (uint32_t)list);
    ufs_wr(REG_UTRLBAU, (uint32_t)(list >> 32));
    ufs_wr(REG_UTRLRSR, 1);
    const char *error = "external: transfer list not taken";
    if (ufs_rd(REG_UTRLBA) == (uint32_t)list && ufs_rd(REG_UTRLBAU) == (uint32_t)(list >> 32) &&
        (ufs_rd(REG_UTRLRSR) & 1)) error = external_read(d);
    ufs_wr(REG_UTRLRSR, 0);
    /* Never restore ABL's descriptors or continue if DMA has not quiesced. */
    if ((ufs_rd(REG_UTRLRSR) & 1) || ufs_rd(REG_UTRLDBR)) {
        con_str("V60 APFS", "DMA did not stop; halted");
        for (;;) __asm__ volatile("wfe");
    }
    ufs_wr(REG_UTRLBA, old_lo);
    ufs_wr(REG_UTRLBAU, old_hi);
    ufs_wr(REG_UTRLRSR, old_run & 1);
    return error;
}
#endif
