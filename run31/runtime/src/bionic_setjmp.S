/*
 * ARM32 non-local jump bridge for Android/Bionic guests.
 *
 * The guest owns a 64-word jmp_buf. We keep an internal layout compatible
 * with the original ARM Bionic implementation: magic and signal mask first,
 * d8-d15 plus FPSCR, then r4-r14. Guest code treats this layout as opaque.
 */

    .syntax unified
    .arm
    .fpu neon
    .text

    .equ JB_MAGIC,       0
    .equ JB_SIGMASK,     4
    .equ JB_FLOAT_BASE,  8
    .equ JB_FLOAT_STATE, 72
    .equ JB_CORE_BASE,   76
    .equ MAGIC_MASK_N,   0x4278f500
    .equ MAGIC_MASK_Y,   0x4278f501

    .global nfsmw_bionic_setjmp
    .type nfsmw_bionic_setjmp, %function
nfsmw_bionic_setjmp:
    mov     r1, #1
    b       nfsmw_bionic_sigsetjmp
    .size nfsmw_bionic_setjmp, .-nfsmw_bionic_setjmp

    .global nfsmw_bionic_sigsetjmp
    .type nfsmw_bionic_sigsetjmp, %function
nfsmw_bionic_sigsetjmp:
    cmp     r1, #0
    beq     1f
    push    {r0, lr}
    bl      nfsmw_bionic_signal_mask_capture
    mov     r1, r0
    pop     {r0, lr}
    str     r1, [r0, #JB_SIGMASK]
    movw    r1, #:lower16:MAGIC_MASK_Y
    movt    r1, #:upper16:MAGIC_MASK_Y
    b       2f
1:
    movw    r1, #:lower16:MAGIC_MASK_N
    movt    r1, #:upper16:MAGIC_MASK_N
2:
    str     r1, [r0, #JB_MAGIC]
    add     r1, r0, #JB_FLOAT_BASE
    vstmia  r1, {d8-d15}
    vmrs    r1, fpscr
    str     r1, [r0, #JB_FLOAT_STATE]
    add     r1, r0, #JB_CORE_BASE
    stmia   r1, {r4-r14}
    mov     r0, #0
    bx      lr
    .size nfsmw_bionic_sigsetjmp, .-nfsmw_bionic_sigsetjmp

    .global nfsmw_bionic_longjmp
    .type nfsmw_bionic_longjmp, %function
nfsmw_bionic_longjmp:
    b       nfsmw_bionic_siglongjmp
    .size nfsmw_bionic_longjmp, .-nfsmw_bionic_longjmp

    .global nfsmw_bionic_siglongjmp
    .type nfsmw_bionic_siglongjmp, %function
nfsmw_bionic_siglongjmp:
    ldr     r3, [r0, #JB_MAGIC]
    movw    r2, #:lower16:MAGIC_MASK_N
    movt    r2, #:upper16:MAGIC_MASK_N
    cmp     r3, r2
    beq     1f
    movw    r2, #:lower16:MAGIC_MASK_Y
    movt    r2, #:upper16:MAGIC_MASK_Y
    cmp     r3, r2
    bne     3f
    push    {r0, r1, r2, lr}
    ldr     r0, [r0, #JB_SIGMASK]
    bl      nfsmw_bionic_signal_mask_restore
    pop     {r0, r1, r2, lr}
1:
    add     r2, r0, #JB_FLOAT_BASE
    vldmia  r2, {d8-d15}
    ldr     r2, [r0, #JB_FLOAT_STATE]
    vmsr    fpscr, r2
    add     r2, r0, #JB_CORE_BASE
    ldmia   r2, {r4-r14}
    cmp     sp, #0
    beq     3f
    cmp     lr, #0
    beq     3f
    mov     r0, r1
    cmp     r0, #0
    moveq   r0, #1
    bx      lr
3:
    b       nfsmw_bionic_longjmp_error
    .size nfsmw_bionic_siglongjmp, .-nfsmw_bionic_siglongjmp

    .section .note.GNU-stack,"",%progbits
