/* Linker script of the installer. It is linked at PARASITE_STASH_ADDRESS in the arm9 ITCM, where the arm9 part of the
   parasite copies it, and must not have zero-initialized data, since nothing would clear it. */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(installer_entry)

SECTIONS
{
    . = 0x01002000;
    .text :
    {
        KEEP(*(.header))
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.data .data.*)
        . = ALIGN(32);
        __installer_end = .;
    }

    .bss : { *(.bss .bss.* COMMON) }
    ASSERT(SIZEOF(.bss) == 0, "The installer must not have zero-initialized data")

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) *(.note*) }
}
