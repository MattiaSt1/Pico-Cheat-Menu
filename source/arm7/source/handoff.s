// Stub that hands over to the real Pico Loader. The arm7 part copies it to arm7 WRAM (HANDOFF_STUB_ADDRESS in
// main.cpp) and calls it with r0 = the arm7 entry point of the real Pico Loader. The stub tells the arm9 that the arm7
// no longer runs from VRAM (PARASITE_SYNC_ARM7_IN_WRAM), waits until the arm9 has copied the real Pico Loader to VRAM
// (PARASITE_SYNC_BOOT_REAL_LOADER) and jumps to it. The literal pool must stay between the two labels.

.syntax unified
.arm
.section .rodata.handoff, "a"
.global arm7HandoffStart, arm7HandoffEnd
arm7HandoffStart:
    ldr r1, =0x04000180         // IPC sync
    mov r2, #0x400              // PARASITE_SYNC_ARM7_IN_WRAM
    strh r2, [r1]
1:
    ldrh r2, [r1]
    and r2, r2, #15
    cmp r2, #5                  // PARASITE_SYNC_BOOT_REAL_LOADER
    bne 1b
    bx r0
    .pool
arm7HandoffEnd:
