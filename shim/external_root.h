/* V60 external APFS descriptor. All sizes/offsets in bytes except GPT LBAs.
 * Loading is read-only. The existing HFS volume and final 1MiB are protected.
 */
#pragma once
#include <stdint.h>
struct v60_external_root {
    uint8_t magic[8];
    uint32_t version, length;
    uint64_t first_lba, last_lba, hfs_bytes, relative, bytes, destination;
    uint32_t image_crc, header_crc;
    uint64_t reserved;
};
_Static_assert(sizeof(struct v60_external_root) == 80, "external root ABI");
static inline uint32_t v60_crc(uint32_t crc, const void *buffer, uint64_t bytes)
{
    const uint8_t *p = buffer;
    while (bytes--) {
        crc ^= *p++;
        for (unsigned n = 0; n < 8; n++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}
static inline int v60_external_valid(const struct v60_external_root *d,
    uint64_t memory_start, uint64_t memory_bytes)
{
    const char *magic = "V60APFS2";
    for (unsigned i = 0; i < 8; i++) if (d->magic[i] != (uint8_t)magic[i]) return 0;
    if (d->version != 1 || d->length != sizeof(*d) || d->reserved ||
        d->header_crc != (v60_crc(~0u, d, 68) ^ ~0u)) return 0;
    if (!d->first_lba || d->first_lba >= d->last_lba || d->last_lba >= 0xffffffffu ||
        !d->hfs_bytes || (d->hfs_bytes & 4095) || !d->bytes || (d->bytes & 4095) ||
        (d->relative & 4095) || (d->destination & 4095)) return 0;
    uint64_t partition_bytes = (d->last_lba - d->first_lba + 1) * 4096;
    if (d->hfs_bytes > partition_bytes || d->relative < d->hfs_bytes ||
        d->relative - d->hfs_bytes < (1ull << 30) ||
        d->relative > partition_bytes || d->bytes > partition_bytes - d->relative ||
        partition_bytes - d->relative - d->bytes < (1ull << 20)) return 0;
    if (memory_start < (1ull << 32) || !memory_bytes ||
        memory_start > UINT64_MAX - memory_bytes || d->destination < memory_start ||
        d->destination - memory_start > memory_bytes ||
        d->bytes > memory_bytes - (d->destination - memory_start)) return 0;
    return 1;
}
const char *v60_load_external(const struct v60_external_root *d);
