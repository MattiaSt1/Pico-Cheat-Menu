// Entry points of the resident part: its header, the OS_IrqHandler hook and helpers that need ARM instructions.

.cpu arm946e-s
.syntax unified

// Header (cheat_menu_resident_t). It must be at the start of the blob, which is linked at address 0, so the symbol
// addresses below are offsets from the start of the blob.
.section ".header", "ax"
.arm
.global resident_header
.hidden resident_header
resident_header:
    .word resident_irqHook      // irqHookOffset
    .word __blob_end            // totalSize
    .word 0                     // dldiOffset
    .word 0                     // imageSectors
    .word 0                     // fileRunCount
    .space 8 * 8                // fileRuns[CHEAT_MENU_MAX_FILE_RUNS]
irqHandlerContinueAddress:
    .word 0                     // irqHandlerContinueAddress
    .word 0                     // cardLockAddress
    .word 0                     // dldiDriver
    .word 0                     // list
    .word 0                     // menuAddress

// The installer replaces the first instructions of OS_IrqHandler with code that saves the access permissions of the
// protection regions in r0 (data) and r1 (instructions), allows read/write access in every region and jumps here.
// This is needed because the game protects the arm7 memory, where this code is. The replaced instructions are
// equivalent to:
//   stmfd sp!, {lr}
//   mov r12, #0x04000000
//   add r12, r12, #0x210
//   ldr r1, [r12, #-8]          // IME
//   cmp r1, #0
//   ldmeqfd sp!, {pc}
//   ldmia r12, {r1, r2}         // IE, IF
//   ands r1, r1, r2
//   ldmeqfd sp!, {pc}
// The hook executes them itself after resident_tick, then continues in OS_IrqHandler.
resident_irqHook:
    stmfd sp!, {r0-r12, lr}
    // resident_tick runs on the stack of the resident part, since the interrupt stack of the game is small.
    mov r4, sp
    ldr r0, stackTopOffset
1:
    add sp, pc, r0
    blx resident_tick
    mov sp, r4
    ldmfd sp!, {r0-r12, lr}
    ldr r3, irqHandlerContinueAddress
    // From here on, the data access permissions of the game apply again. The instruction permissions stay open,
    // since the rest of this code is still fetched from the arm7 memory.
    mcr p15, 0, r0, c5, c0, 2
    stmfd sp!, {lr}
    mov r12, #0x04000000
    add r12, r12, #0x210
    ldr r1, [r12, #-8]
    cmp r1, #0
    ldmfdeq sp!, {pc}
    ldmia r12, {r1, r2}
    ands r1, r1, r2
    ldmfdeq sp!, {pc}
    bx r3

stackTopOffset:
    .word resident_stackTop - (1b + 8)

.text
.arm

// void resident_flushCaches(const void* start, u32 size)
// Cleans and invalidates the data cache for the given memory and invalidates the instruction cache. Used for memory
// that the DLDI driver transfers or that holds code.
.global resident_flushCaches
.hidden resident_flushCaches
.type resident_flushCaches, %function
resident_flushCaches:
    add r1, r0, r1
    bic r0, r0, #31
1:
    mcr p15, 0, r0, c7, c14, 1  // clean and invalidate data cache line
    add r0, r0, #32
    cmp r0, r1
    blo 1b
    mov r0, #0
    mcr p15, 0, r0, c7, c10, 4  // drain write buffer
    mcr p15, 0, r0, c7, c5, 0   // invalidate instruction cache
    bx lr

// u32 resident_swapWord(u32 value, volatile u32* address)
// Atomically stores value at address and returns the previous value.
.global resident_swapWord
.hidden resident_swapWord
.type resident_swapWord, %function
resident_swapWord:
    swp r0, r0, [r1]
    bx lr

.section ".bss.resident_stack", "aw", %nobits
.balign 8
    .space 512
resident_stackTop:

.end
