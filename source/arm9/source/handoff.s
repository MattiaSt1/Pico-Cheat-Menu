.syntax unified
.arm
.section .itcm, "ax"
.global bootRealLoader
.type bootRealLoader, %function
// r0/r1 = arm9 source/size, r2/r3 = arm7 source/size
bootRealLoader:
    ldr r4, =0x04000242
    mov r5, #0x80
    strb r5, [r4]
    strb r5, [r4, #1]
    ldr r4, =0x06800000
1:
    ldrh r5, [r0], #2
    strh r5, [r4], #2
    subs r1, r1, #2
    bgt 1b
    ldr r4, =0x06840000
2:
    ldrh r5, [r2], #2
    strh r5, [r4], #2
    subs r3, r3, #2
    bgt 2b
    ldr r4, =0x04000242
    mov r5, #0x82
    strb r5, [r4]
    mov r5, #0x8A
    strb r5, [r4, #1]
    ldr r4, =0x04000180
    mov r5, #0x500
    strh r5, [r4]
    ldr pc, =0x06800000
    .pool
