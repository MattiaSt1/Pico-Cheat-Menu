// Boots the real Pico Loader. Runs from the ITCM, since it overwrites VRAM A, where the arm9 part of the parasite runs.

.syntax unified
.arm
.section .itcm, "ax"

// void bootRealLoader(const void* loader9, u32 loader9Size, const void* loader7, u32 loader7Size)
// Copies picoLoader9.bin to VRAM A and picoLoader7.bin to VRAM C, maps VRAM C and D to the arm7, tells the arm7 to
// start the real Pico Loader (PARASITE_SYNC_BOOT_REAL_LOADER) and jumps to picoLoader9.bin.
.global bootRealLoader
.type bootRealLoader, %function
bootRealLoader:
    ldr r4, =0x04000242         // VRAMCNT_C
    mov r5, #0x80               // LCDC
    strb r5, [r4]
    strb r5, [r4, #1]           // VRAMCNT_D
    ldr r4, =0x06800000         // VRAM A
1:
    ldrh r5, [r0], #2
    strh r5, [r4], #2
    subs r1, r1, #2
    bgt 1b
    ldr r4, =0x06840000         // VRAM C
2:
    ldrh r5, [r2], #2
    strh r5, [r4], #2
    subs r3, r3, #2
    bgt 2b
    ldr r4, =0x04000242
    mov r5, #0x82               // VRAM C to the arm7 at 0x06000000
    strb r5, [r4]
    mov r5, #0x8A               // VRAM D to the arm7 at 0x06020000
    strb r5, [r4, #1]
    ldr r4, =0x04000180         // IPC sync
    mov r5, #0x500              // PARASITE_SYNC_BOOT_REAL_LOADER
    strh r5, [r4]
    ldr pc, =0x06800000
    .pool
