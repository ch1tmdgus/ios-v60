#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../shim/external_root.h"
static void seal(struct v60_external_root *d)
{ d->header_crc = v60_crc(~0u, d, 68) ^ ~0u; }
int main(void)
{
    assert(offsetof(struct v60_external_root, header_crc) == 68);
    assert((v60_crc(~0u, "123456789", 9) ^ ~0u) == 0xcbf43926u);
    struct v60_external_root good = {
        .magic={'V','6','0','A','P','F','S','2'}, .version=1, .length=80,
        .first_lba=6000000, .last_lba=30000000,
        .hfs_bytes=16ull<<30, .relative=18ull<<30,
        .bytes=3ull<<30, .destination=0x108000000ull,
    };
    seal(&good);
    assert(v60_external_valid(&good, 1ull<<32, 6ull<<30));
    struct v60_external_root d = good;
    d.relative=16ull<<30; seal(&d); assert(!v60_external_valid(&d,1ull<<32,6ull<<30));
    d=good; d.bytes=UINT64_MAX; seal(&d); assert(!v60_external_valid(&d,1ull<<32,6ull<<30));
    d=good; d.destination=UINT64_MAX-4095; seal(&d); assert(!v60_external_valid(&d,1ull<<32,6ull<<30));
    d=good; d.destination=(1ull<<32)-4096; seal(&d); assert(!v60_external_valid(&d,1ull<<32,6ull<<30));
    d=good; d.last_lba=d.first_lba-1; seal(&d); assert(!v60_external_valid(&d,1ull<<32,6ull<<30));
    d=good; d.relative=((d.last_lba-d.first_lba+1)*4096)-d.bytes; seal(&d);
    assert(!v60_external_valid(&d,1ull<<32,6ull<<30)); /* preserve trailing footer */
    for (unsigned i=0; i<sizeof(good); i++) {
        d=good; ((unsigned char *)&d)[i]^=1;
        assert(!v60_external_valid(&d,1ull<<32,6ull<<30));
    }
    assert(!v60_external_valid(&good,UINT64_MAX-4095,8192));
    puts("PASS: CRC, corruption, HFS guard, footer, destination and overflow bounds");
}
