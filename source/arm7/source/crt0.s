.section ".crt0", "ax"
.arm

.global _start
.type _start, %function
_start:
    // disable irqs
    ldr r0,= 0x04000208
    strb r0, [r0]
    // clear bss
    ldr r0,= __bss_start
    ldr r1,= __bss_end
    cmp r0, r1
    beq bss_done
    mov r2, #0
1:
    str r2, [r0], #4
    cmp r0, r1
    bne 1b
bss_done:
    ldr sp,= 0x0380FD80
    b loaderMain

.pool

.end