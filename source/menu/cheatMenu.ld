/* The cheat menu is linked at address 0 and must be position independent. */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(cheatmenu_header)

SECTIONS
{
    . = 0;
    .text :
    {
        KEEP(*(.header))
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.data .data.*)
        . = ALIGN(4);
    }

    .bss (NOLOAD) :
    {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(32);
        __blob_end = .;
    }

    .got : { *(.got .got.plt) }
    ASSERT(SIZEOF(.got) == 0, "The cheat menu must not use a GOT")

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) *(.note*) }
}
