.syntax unified
.arm
.section .rodata.handoff, "a"
.global arm7HandoffStart, arm7HandoffEnd
arm7HandoffStart:
    ldr r1, =0x04000180
    mov r2, #0x400
    strh r2, [r1]
1:
    ldrh r2, [r1]
    and r2, r2, #15
    cmp r2, #5
    bne 1b
    bx r0
    .pool
arm7HandoffEnd:
