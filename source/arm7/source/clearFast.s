.text
.thumb

.global clearFast
.type clearFast, %function
clearFast:
    push {r4-r7,lr}
    mov r12, sp
    mov sp, r0
    add sp, r1
    mov r1, #0
    mov r2, #0
    mov r3, #0
    mov r4, #0
    mov r5, #0
    mov r6, #0
    mov r7, #0
    mov lr, r7
1:
    push {r1-r7,lr}
    push {r1-r7,lr}
    cmp sp, r0
    bne 1b
    mov sp, r12
    pop {r4-r7}
    pop {r3}
    bx r3

.end