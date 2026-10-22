/* In-process, read-only startup sampler for the CPU GUI experiment.
 * No thread suspension, instruction patching or cross-process task rights.
 * Only included in explicitly diagnostic userland builds.
 */
#include <mach/mach.h>
#include <mach/arm/thread_status.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dispatch/dispatch.h>
#include <CoreFoundation/CoreFoundation.h>
extern const void *_dyld_get_shared_cache_range(size_t *);
static thread_t watched;
static int output;
static uint64_t cache_slide;
static void *sample(void *arg)
{
    (void)arg;
    for (unsigned round = 0; round < 12; round++) {
        sleep(10);
        arm_thread_state64_t state;
        mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
        kern_return_t rc = thread_get_state(watched, ARM_THREAD_STATE64, (thread_state_t)&state, &count);
        if (rc != KERN_SUCCESS) {
            dprintf(output, "V60 SB WATCH[%s]: sample=%u thread_get_state=%d\n", getprogname(), round, rc);
            continue;
        }
        uint64_t pc = arm_thread_state64_get_pc(state), fp = arm_thread_state64_get_fp(state);
        uint64_t lr = arm_thread_state64_get_lr(state), sp = arm_thread_state64_get_sp(state);
        dprintf(output, "V60 SB WATCH[%s]: sample=%u pc=%llx lr=%llx sp=%llx fp=%llx cache_slide=%llx\n",
                getprogname(), round, pc, lr, sp, fp, cache_slide);
        dprintf(output, "V60 SB WATCH: canonical pc=%llx lr=%llx\n", pc-cache_slide, lr-cache_slide);
        for (unsigned i = 0; i < 16 && fp >= sp && fp-sp < 0x100000 && !(fp & 15); i++) {
            uint64_t frame[2];
            vm_size_t got = 0;
            rc = vm_read_overwrite(mach_task_self(), fp, sizeof(frame), (vm_address_t)frame, &got);
            if (rc || got != sizeof(frame)) break;
            dprintf(output, "V60 SB WATCH: frame=%u return=%llx canonical=%llx\n", i, frame[1], frame[1]-cache_slide);
            if (frame[0] <= fp) break;
            fp = frame[0];
        }
    }
    return 0;
}
__attribute__((constructor)) static void start_watch(void)
{
    if (strcmp(getprogname(), "SpringBoard") && strcmp(getprogname(), "lsd")) return;
    output = open("/dev/console", O_WRONLY);
    if (output < 0) return;
    size_t length = 0;
    cache_slide = (uintptr_t)_dyld_get_shared_cache_range(&length) - 0x180000000ull;
    watched = pthread_mach_thread_np(pthread_self());
    pthread_t worker;
    int rc = pthread_create(&worker, 0, sample, 0);
    if (!rc) pthread_detach(worker);
    dprintf(output, "V60 SB WATCH: started rc=%d main_thread=%u\n", rc, watched);
    dprintf(output, "V60 SB WATCH: pthread_main=%d cf_main=%d\n", pthread_main_np(), CFRunLoopGetMain()==CFRunLoopGetCurrent());
    dispatch_async(dispatch_get_main_queue(), ^{
        dprintf(output, "V60 SB WATCH: MAIN ASYNC FIRED\n");
    });
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5*NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        dprintf(output, "V60 SB WATCH: MAIN TIMER FIRED\n");
    });
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5*NSEC_PER_SEC), dispatch_get_global_queue(0,0), ^{
        dprintf(output, "V60 SB WATCH: GLOBAL TIMER FIRED\n");
    });
    CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, ^{
        dprintf(output, "V60 SB WATCH: CF MAIN BLOCK FIRED\n");
    });
    CFRunLoopWakeUp(CFRunLoopGetMain());
}
