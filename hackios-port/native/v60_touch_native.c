/* Native XNU adapter for the previously device-tested V60 GPIO-I2C driver.
 * No fixed kernel addresses and no GCC/Apple varargs bridge in this build.
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
typedef unsigned long uptr;
static uptr native_map(uptr pa, uptr size) { return ml_io_map(pa, size); }
static int native_start(void (*fn)(void *, int), void *arg, uptr *out)
{
    thread_t t = THREAD_NULL;
    kern_return_t rc = kernel_thread_start((thread_continue_t)fn, arg, &t);
    *out = (uptr)t;
    return rc;
}
static void native_deallocate(uptr t) { thread_deallocate((thread_t)t); }
static int native_terminate(uptr t) { return thread_terminate((thread_t)t); }
static uptr native_current(void) { return (uptr)current_thread(); }
static void native_sleep(unsigned ms)
{
    uint64_t deadline;
    clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
    assert_wait_deadline((event_t)current_thread(), THREAD_UNINT, deadline);
    thread_block(THREAD_CONTINUE_NULL);
}
static void native_delay(unsigned us) { delay(us); }
static int native_bootarg(const char *key, void *out, int size)
{ return PE_parse_boot_argn(key, out, size); }
static uint32_t native_active;
static void native_touch(int finger, int down, int x, int y)
{
    if (finger < 0 || finger >= 10) return;
    uint32_t bit = 1u << finger;
    uint32_t type = down ? ((native_active & bit) ? REDWOOD_TOUCH_MOVE : REDWOOD_TOUCH_DOWN) : REDWOOD_TOUCH_UP;
    if (!down && !(native_active & bit)) return;
    redwood_touch_post(type, (uint32_t)finger, v60_normalize(x, 1080), v60_normalize(y, 2460));
    if (down) native_active |= bit; else native_active &= ~bit;
}
#define kernel_io_map native_map
#define kernel_thread_start native_start
#define kernel_thread_deallocate native_deallocate
#define kernel_thread_terminate native_terminate
#define kernel_current_thread native_current
#define kernel_sleep native_sleep
#define kernel_delay native_delay
#define kernel_bootarg native_bootarg
#define kernel_hid_touch native_touch
#define logmsg printf
#define memset v60_touch_memset
#include "v60_touch_impl.inc"

void v60_keys_init(void);
void v60_input_init(void);
void v60_input_init(void)
{
    int board = 0;
    if (!PE_parse_boot_argn("v60", &board, sizeof(board)) || board != 1) return;
    printf("V60 INPUT: native SW42902 and SPMI input, normalized redwood event ABI\n");
    v60_touch_init();
    v60_keys_init();
}
