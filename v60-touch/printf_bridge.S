/* SPDX-License-Identifier: GPL-2.0-only */
.text
.global logmsg
.type logmsg,%function
logmsg:
    sub sp, sp, #80
    stp x29, x30, [sp, #64]
    add x29, sp, #64
    /* Apple arm64 unnamed arguments occupy consecutive eight-byte stack slots. */
    stp x1, x2, [sp, #0]
    stp x3, x4, [sp, #16]
    stp x5, x6, [sp, #32]
    str x7, [sp, #48]
    bl kernel_printf
    ldp x29, x30, [sp, #64]
    add sp, sp, #80
    ret
.size logmsg,.-logmsg
