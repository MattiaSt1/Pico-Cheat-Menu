.text
.arm

// ARM DDI 0201D, page 3-11
.global dc_flushAll
.type dc_flushAll, %function
dc_flushAll:
    // Temp register to set to 0. Needed for write buffer drain
    mov r3, #0
    // Initialize segment counter outer_loop
    mov r1, #0
outer_loop:

    // Initialize line counter inner_loop
    mov r0, #0
inner_loop:
    orr r2, r1, r0 // Generate segment and line address
    mcr p15, 0, r3, c7, c10, 4 // Drain write buffer. See errata ARM946-PRDC-000592 5.0, section 4.8
    mcr p15, 0, r2, c7, c14, 2 // Clean and flush the line
    add r0, r0, #0x20 // Increment to next line
    cmp r0, #0x400 // (data cache size / entries)
    bne inner_loop

    add r1, r1, #0x40000000 // Increment segment counter
    cmp r1, #0x0
    bne outer_loop

    bx lr

.global dc_invalidateRange
.type dc_invalidateRange, %function
dc_invalidateRange:
    add r1, r1, r0
    bic r0, r0, #0x1F
1:
    mcr p15, 0, r0, c7, c6, 1
    add r0, r0, #32
    cmp r0, r1
    blt 1b
    bx lr

.global dc_drainWriteBuffer
.type dc_drainWriteBuffer, %function
dc_drainWriteBuffer:
    mov r0, #0
    mcr p15, 0, r0, c7, c10, 4
    bx lr

.global dc_invalidateAll
.type dc_invalidateAll, %function
dc_invalidateAll:
    mov r0, #0
    mcr p15, 0, r0, c7, c6, 0
    bx lr

.global ic_invalidateAll
.type ic_invalidateAll, %function
ic_invalidateAll:
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0
    bx lr

.pool
.end
