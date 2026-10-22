/*
 * Milestone 4: act as iBoot for xnu.
 *
 * mkpayload.py already resolved the kernel's chained fixups and built the
 * ADT, so this only has to: find the payload in the ABL ramdisk, copy the
 * segments to phys_base + (vmaddr - virt_base), place the ADT and
 * boot_args behind the kernel, and jump to the entry point with the MMU
 * and caches off, x0 = boot_args (physical).
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"
#include "loader.h"
#ifdef V60_EXTERNAL_ROOT
#include "external_root.h"
#endif

#define BOOT_LINE_LENGTH	608
#define PAGE_16K		0x4000UL

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

/* pexpert/pexpert/arm64/boot.h, Revision/Version 2 */
struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

/* MMU off: everything is Device memory, so copy in aligned 8-byte words. */
static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

static int magic_ok(const char *m)
{
	static const char want[] = "SWEETXNU";
	for (int i = 0; i < 8; i++)
		if (m[i] != want[i])
			return 0;
	return 1;
}

typedef void (*xnu_entry_t)(uint64_t boot_args_pa);

const char *loader_boot(uintptr_t dtb)
{
	uint64_t rd_start, rd_end;

	if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) ||
	    !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end))
		return "no initrd in /chosen";
	con_hex2("INITRD", "lo=", rd_start, "hi=", rd_end);
	if (rd_end <= rd_start || rd_end - rd_start < sizeof(struct payload))
		return "initrd too small for payload header";

	const struct payload *p = (const struct payload *)rd_start;
	if (!magic_ok(p->magic))
		return "no SWEETXNU payload";
	if ((p->version != 1
#ifdef V60_EXTERNAL_ROOT
        && p->version != 2
#endif
        ) || (p->adt_off & 7) || (rd_start & 7))
		return "bad payload header";

#ifdef V60_EXTERNAL_ROOT
    const struct v60_external_root *external = 0;
    if (p->version == 2) {
        uint64_t size = rd_end - rd_start;
        uint64_t descriptors = sizeof(*p) + (uint64_t)p->nsegs * sizeof(p->segs[0]);
        if (rd_end <= rd_start || size < sizeof(*p) || p->nsegs > 32 ||
            descriptors + sizeof(*external) > size || p->adt_off < descriptors + sizeof(*external) ||
            p->adt_off > size || p->adt_size > size - p->adt_off || p->adt_size > 0x10000 ||
            p->phys_base != 0x100000000ull || p->mem_size != 0x180000000ull ||
            p->virt_base != 0xfffffff004000000ull || p->kernel_end < p->virt_base ||
            p->kernel_end - p->virt_base > p->mem_size - 0x20000 || p->cmdline[607])
            return "external: invalid payload bounds";
        external = (const struct v60_external_root *)(rd_start + descriptors);
        if (!v60_external_valid(external, p->phys_base, p->mem_size))
            return "external: invalid root descriptor";
        unsigned roots = 0, entries = 0;
        uint64_t previous_end = p->virt_base;
        for (unsigned i = 0; i < p->nsegs; i++) {
            const struct payload_seg *s = &p->segs[i];
            if (s->vmaddr < previous_end || s->vmaddr > p->kernel_end ||
                s->vmsize > p->kernel_end - s->vmaddr || s->file_size > s->vmsize ||
                s->file_off < descriptors + sizeof(*external) || s->file_off > p->adt_off ||
                s->file_size > p->adt_off - s->file_off || (s->vmaddr & 7) || (s->file_off & 7))
                return "external: invalid segment bounds";
            previous_end = s->vmaddr + s->vmsize;
            uint64_t pa = s->vmaddr - p->virt_base + p->phys_base;
            if (pa == external->destination && s->vmsize == external->bytes && !s->file_size) roots++;
            if (p->entry_va >= s->vmaddr && p->entry_va - s->vmaddr < s->file_size) entries++;
        }
        if (roots != 1 || entries != 1) return "external: missing root/entry segment";
    }
#endif

	uint64_t pa_of_va_base = p->phys_base - p->virt_base;	/* pa = va + this */
	con_hex2("PHYS", "base=", p->phys_base, "size=", p->mem_size);
	con_hex("VIRT", p->virt_base);
	con_hex("ENTRY", p->entry_va);

	/* The payload must not overlap where the kernel goes. */
	if (rd_end > p->phys_base && rd_start < p->phys_base + p->mem_size)
		return "ramdisk overlaps kernel memory";

	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + pa_of_va_base;
#ifdef V60_EXTERNAL_ROOT
        if (external && dst == external->destination && !s->file_size)
            continue; /* populated by read-only UFS DMA + CPU copy below */
#endif
		copy64(dst, rd_start + s->file_off, s->file_size);
		if (s->vmsize > s->file_size)
			zero64(dst + ((s->file_size + 7) & ~7UL), s->vmsize - ((s->file_size + 7) & ~7UL));
	}
#ifdef V60_EXTERNAL_ROOT
    if (external) {
        const char *error = v60_load_external(external);
        if (error) return error;
        con_str("V60 APFS", "image CRC verified; existing HFS unchanged");
    }
#endif
	con_hex2("SEGS", "n=", p->nsegs, "end=", p->kernel_end + pa_of_va_base);

	/* ADT, then boot_args, behind the kernel; everything below topOfKernelData is ours. */
	uintptr_t adt_pa = p->kernel_end + pa_of_va_base;
	copy64(adt_pa, rd_start + p->adt_off, p->adt_size);
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	uintptr_t top = ba_pa + PAGE_16K;

	zero64(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = FB_BASE;
#ifdef V60_TEXT_CONSOLE
    ba->Video.v_display = 0;
#else
	ba->Video.v_display = 1;
#endif
	ba->Video.v_rowBytes = FB_STRIDE;
	ba->Video.v_width = FB_W;
	ba->Video.v_height = FB_H;
	ba->Video.v_depth = 32;
	ba->deviceTreeP = adt_pa - pa_of_va_base;	/* xnu reads it with the MMU on: a VA */
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = p->mem_size;
	con_hex2("BOOTARG", "pa=", ba_pa, "top=", top);

	uintptr_t entry_pa = p->entry_va + pa_of_va_base;
	con_hex("JUMP", entry_pa);
	con_str("XNU", "jumping (fbcon clears screen)");

	/* Everything was written with the MMU and caches off; drop any stale I-cache lines too. */
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	((xnu_entry_t)entry_pa)(ba_pa);
	return "xnu returned";
}
