/* The resident part is linked at address 0 and must be position independent. */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(resident_header)

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
    ASSERT(SIZEOF(.got) == 0, "The resident part must not use a GOT")

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) *(.note*) }
}
