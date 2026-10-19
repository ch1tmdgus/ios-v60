.section .text.start,"ax"
.global _start
_start:
    adr x9, stack_top
    mov sp, x9
    mov x29, #0x2829
    mov x19, #0x1919
    adr x0, format
    mov x1, #0x11
    mov x2, #0x22
    mov x3, #0x33
    mov x4, #0x44
    mov x5, #0x55
    mov x6, #0x66
    mov x7, #0x77
    bl logmsg
    cbnz x0, finish
    mov x10, #0x2829
    cmp x29, x10
    b.ne fail
    mov x10, #0x1919
    cmp x19, x10
    b.ne fail
    b finish
fail:
    mov x0, #1
finish:
    adr x1, exit_block
    str x0, [x1, #8]
    mov x0, #0x20
    hlt #0xf000
    b .
.global kernel_printf
kernel_printf:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    mov x0, #1
    ldr x9, [sp, #16]
    cmp x9, #0x11
    b.ne done
    ldr x9, [sp, #24]
    cmp x9, #0x22
    b.ne done
    ldr x9, [sp, #32]
    cmp x9, #0x33
    b.ne done
    ldr x9, [sp, #40]
    cmp x9, #0x44
    b.ne done
    ldr x9, [sp, #48]
    cmp x9, #0x55
    b.ne done
    ldr x9, [sp, #56]
    cmp x9, #0x66
    b.ne done
    ldr x9, [sp, #64]
    cmp x9, #0x77
    b.ne done
    mov x0, #0
done:
    ldp x29, x30, [sp], #16
    ret
format: .asciz "test"
.balign 8
exit_block: .quad 0x20026, 0
.balign 16
.space 0x4000
stack_top:
