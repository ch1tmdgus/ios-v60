/* V60 PM8150 PON + GPIO6, observer reads only. Mapping proven in touch5.
 * No PMIC register writes. Bounded observer polling; require released keys
 * and two matching samples before posting events to /dev/redwood-touch.
 */
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/redwood_input.h>
#include "input_policy.h"
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern kern_return_t thread_terminate(thread_t);
static vm_offset_t core, cnfg, obs;
static uint16_t pon_apid = 0xffff, gpio_apid = 0xffff;
static uint32_t rd(vm_offset_t base, unsigned off)
{
    uint32_t v = *(volatile uint32_t *)(base + off);
    __asm__ volatile("dmb oshld" ::: "memory");
    return v;
}
static uint16_t find_apid(unsigned ppid, unsigned count)
{
    uint16_t found = 0xffff;
    for (unsigned i = 0; i < count; i++) {
        uint32_t map = rd(core, 0x900 + 4 * i);
        if (map && ((map >> 8) & 0xfff) == ppid &&
            (found == 0xffff || !(rd(cnfg, 0x700 + 4 * i) & 7))) found = (uint16_t)i;
    }
    return found;
}
static int read_byte(uint16_t apid, unsigned address, uint8_t *value)
{
    if (apid == 0xffff) return 0;
    unsigned off = 0x80u * apid;
    __asm__ volatile("dmb oshst" ::: "memory");
    *(volatile uint32_t *)(obs + off) = (1u << 27) | ((address & 255) << 4);
    __asm__ volatile("dsb osh" ::: "memory");
    for (unsigned n = 0; n < 1000; n++) {
        uint32_t status = rd(obs, off + 8);
        if (status & 1) {
            if (status & 14) return 0;
            *value = (uint8_t)rd(obs, off + 0x18);
            return 1;
        }
        delay(1);
    }
    return 0;
}
static void keys_worker(void *arg, wait_result_t result)
{
    (void)arg; (void)result;
    struct v60_key_state state[3] = {{0}};
    const uint32_t code[3] = {REDWOOD_BUTTON_LOCK, REDWOOD_BUTTON_VOLUME_UP, REDWOOD_BUTTON_VOLUME_DOWN};
    for (;;) {
        uint8_t pon = 0, gpio = 0;
        int p = read_byte(pon_apid, 0x0810, &pon), g = read_byte(gpio_apid, 0xc510, &gpio);
        int sample[3] = {p ? !!(pon & 1) : -1, g ? !(gpio & 1) : -1, p ? !!(pon & 2) : -1};
        for (unsigned i = 0; i < 3; i++) {
            int changed = v60_key_sample(&state[i], sample[i]);
            if (changed >= 0) {
                redwood_touch_post(changed ? REDWOOD_BUTTON_DOWN : REDWOOD_BUTTON_UP, code[i], 0, 0);
                printf("V60 KEYS: code=%u down=%d\n", code[i], changed);
            }
        }
        uint64_t deadline;
        clock_interval_to_deadline(20, NSEC_PER_MSEC, &deadline);
        assert_wait_deadline((event_t)current_thread(), THREAD_UNINT, deadline);
        thread_block(THREAD_CONTINUE_NULL);
    }
}
void v60_keys_init(void);
void v60_keys_init(void)
{
    int enabled = 0;
    if (!PE_parse_boot_argn("v60keys", &enabled, sizeof(enabled)) || !enabled) return;
    core = ml_io_map(0xc440000, 0x2000);
    cnfg = ml_io_map(0xc40a000, 0x1000);
    obs = ml_io_map(0xe600000, 0x10000);
    if (!core || !cnfg || !obs) { printf("V60 KEYS: map failed\n"); return; }
    uint32_t version = rd(core, 0), count = rd(core, 4) & 0x7ff;
    if (version < 0x50000000 || version >= 0x60000000 || !count || count > 512) {
        printf("V60 KEYS: unsupported version/count %#x/%u\n", version, count); return;
    }
    pon_apid = find_apid(0x008, count);
    gpio_apid = find_apid(0x0c5, count);
    uint8_t mode = 0, en = 0, subtype = 0;
    if (!read_byte(gpio_apid, 0xc540, &mode) || !read_byte(gpio_apid, 0xc546, &en) ||
        !read_byte(gpio_apid, 0xc505, &subtype) || !(en & 0x80)) gpio_apid = 0xffff;
    unsigned direction = subtype >= 0x10 && subtype <= 0x13 ? mode & 3 : (mode >> 4) & 7;
    if (direction != 0 && direction != 2) gpio_apid = 0xffff;
    printf("V60 KEYS: observer version=%#x pon=%u gpio6=%u mode=%#x subtype=%#x\n",
        version, pon_apid, gpio_apid, mode, subtype);
    if (pon_apid == 0xffff && gpio_apid == 0xffff) return;
    thread_t thread;
    kern_return_t rc = kernel_thread_start(keys_worker, NULL, &thread);
    if (!rc) thread_deallocate(thread); else printf("V60 KEYS: thread failed %d\n", rc);
}
