.cpu arm946e-s
.syntax unified

// Header (cheat_menu_image_header_t), must be at the start of the blob.
.section ".header", "ax"
.arm
.global cheatmenu_header
.hidden cheatmenu_header
cheatmenu_header:
    .word cheatmenu_entry       // entryOffset (the blob is linked at address 0)
    .word __blob_end            // totalSize
    .word 0                     // dataOffset

// u32 cheatmenu_entry(cheat_menu_resident_t* resident, u32 numberOfCheats)
// Called by the resident part once the menu is loaded. The menu runs on its own stack.
cheatmenu_entry:
    stmfd sp!, {r4, lr}
    mov r4, sp
    ldr r2, stackTopOffset
1:
    add sp, pc, r2
    blx cheatmenu_main
    mov sp, r4
    ldmfd sp!, {r4, pc}

stackTopOffset:
    .word cheatmenu_stackTop - (1b + 8)

.text
.arm

// void cheatmenu_cleanDataCache(const void* start, u32 size)
.global cheatmenu_cleanDataCache
.hidden cheatmenu_cleanDataCache
.type cheatmenu_cleanDataCache, %function
cheatmenu_cleanDataCache:
    add r1, r0, r1
    bic r0, r0, #31
1:
    mcr p15, 0, r0, c7, c10, 1 // clean data cache line
    add r0, r0, #32
    cmp r0, r1
    blo 1b
    mov r0, #0
    mcr p15, 0, r0, c7, c10, 4 // drain write buffer
    bx lr

.global cheatmenu_invalidateDataCache
.hidden cheatmenu_invalidateDataCache
.type cheatmenu_invalidateDataCache, %function
cheatmenu_invalidateDataCache:
    add r1, r0, r1
    bic r0, r0, #31
1:
    mcr p15, 0, r0, c7, c6, 1
    add r0, r0, #32
    cmp r0, r1
    blo 1b
    bx lr

.section ".bss.cheatmenu_stack", "aw", %nobits
.balign 8
    .space 3072
cheatmenu_stackTop:

.end
