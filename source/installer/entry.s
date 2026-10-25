.syntax unified
.cpu arm946e-s
.section .header, "ax"
.arm
.global installer_header
installer_header:
    .word installer_entry
    .word __installer_end - installer_header
    .space 8
.global installer_entry
installer_entry:
    mov r4, r0
    // read/write access in every protection region, to find the cheat list in the arm7 memory
    // (the game sets up its own protection regions when it starts)
    mov r0, #0x33
    orr r0, r0, r0, lsl #8
    orr r0, r0, r0, lsl #16
    mcr p15, 0, r0, c5, c0, 2
    mcr p15, 0, r0, c5, c0, 3
    mov r5, sp
    ldr sp, =installer_stack_top
    blx installer_run
    mov sp, r5
    push {r4}
    mov r0, #0
    mov r1, #0
    mov r2, #0
    mov r3, #0
    mov r4, #0
    mov r5, #0
    mov r6, #0
    mov r7, #0
    mov r8, #0
    mov r9, #0
    mov r10, #0
    mov r11, #0
    mov r12, #0
    mov lr, #0
    pop {pc}
    .pool
.global installer_flush
.type installer_flush, %function
installer_flush:
    mov r3, #0
    mov r1, #0
1:
    mov r0, #0
2:
    orr r2, r1, r0
    mcr p15, 0, r3, c7, c10, 4
    mcr p15, 0, r2, c7, c14, 2
    add r0, r0, #32
    cmp r0, #0x400
    bne 2b
    adds r1, r1, #0x40000000
    bne 1b
    mcr p15, 0, r3, c7, c5, 0
    bx lr
.section .data.stack, "aw"
.balign 8
    .space 1024
installer_stack_top:
