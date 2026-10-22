/* Minimal Darwin/arm64 PID 1. No SDK, libc, dyld or Apple userland files.
 * Stay alive after printing the milestone so a missing iOS root cannot cause
 * an init-exited panic. This is a bring-up root, not SpringBoard.
 */
.section __TEXT,__text,regular,pure_instructions
.globl _start
.p2align 2
_start:
    // Darwin's raw entry SP may be 8 mod 16. Our static entry replaces crt1.
    mov x9, sp
    and x9, x9, #0xfffffffffffffff0
    mov sp, x9
    adrp x0, console@PAGE
    add x0, x0, console@PAGEOFF
    mov x1, #2
    mov x2, #0
    mov x16, #5                 // open
    svc #0x80
    b.cs failed
    mov x19, x0
    mov x16, #20                // getpid
    svc #0x80
    cmp x0, #1
    b.ne failed
    mov x0, x19
    adrp x1, message@PAGE
    add x1, x1, message@PAGEOFF
    adrp x2, message_end@PAGE
    add x2, x2, message_end@PAGEOFF
    sub x2, x2, x1
    mov x16, #4                 // write
    svc #0x80
    b.cs failed
    mov x20, #0
1:
    // Darwin syscall 240 is listxattr, NOT nanosleep. A zero-fd select with
    // a user64 timeval is a one-second wait, using our now-aligned stack.
    sub sp, sp, #16
    mov x8, #1
    str x8, [sp]               // user_timeval.tv_sec (int64)
    str xzr, [sp, #8]          // tv_usec (int32) and padding
    mov x0, #0                 // nfds
    mov x1, #0
    mov x2, #0
    mov x3, #0
    mov x4, sp
    mov x16, #93               // select(0, NULL, NULL, NULL, &timeout)
    svc #0x80
    b.cs failed
    add sp, sp, #16
    cmp x0, #0                 // no ready descriptors after timeout
    b.ne failed
    add x20, x20, #1
    cmp x20, #5
    b.ne 1b
    mov x0, x19
    adrp x1, heartbeat@PAGE
    add x1, x1, heartbeat@PAGEOFF
    adrp x2, heartbeat_end@PAGE
    add x2, x2, heartbeat_end@PAGEOFF
    sub x2, x2, x1
    mov x16, #4
    svc #0x80
    b.cs failed
    mov x20, #0
    b 1b
failed:
    mov x0, #77
    mov x16, #1                 // exit: diagnostic failure is visible as panic
    svc #0x80
    b failed
.section __TEXT,__const
console: .asciz "/dev/console"
message:
    .ascii "\nV60 HACKIOS: PID1 PASS - XNU 8796.121.2 / APFS / arm64 syscalls\n"
    .ascii "V60 HACKIOS: boot2 aligned entry; testing Darwin timed wait\n"
message_end:
heartbeat:
    .ascii "V60 HACKIOS: WAIT PASS - 5 timed waits; PID1 alive (boot2)\n"
heartbeat_end:
