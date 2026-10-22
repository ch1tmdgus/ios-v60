/* Minimal Darwin/arm64 PID 1. No SDK, libc, dyld or Apple userland files.
 * Stay alive after printing the milestone so a missing iOS root cannot cause
 * an init-exited panic. This is a bring-up root, not SpringBoard.
 */
.section __TEXT,__text,regular,pure_instructions
.globl _start
.p2align 2
_start:
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
1:
    sub sp, sp, #16
    mov x8, #1
    str x8, [sp]
    str xzr, [sp, #8]
    mov x0, sp
    mov x1, #0
    mov x16, #240               // nanosleep
    svc #0x80
    add sp, sp, #16
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
    .ascii "V60 HACKIOS: diagnostic root alive; iOS GUI and V60 drivers pending\n"
message_end:
