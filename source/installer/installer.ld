OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(installer_entry)
SECTIONS {
    . = 0x01002000;
    .text : {
        KEEP(*(.header))
        *(.text .text.*) *(.rodata .rodata.*) *(.data .data.*)
        . = ALIGN(32);
        __installer_end = .;
    }
    .bss : { *(.bss .bss.* COMMON) }
    ASSERT(SIZEOF(.bss) == 0, "Installer must not have BSS")
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) *(.note*) }
}
