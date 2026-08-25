OUTPUT_FORMAT(binary)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS {
    . = 0x00900000;
    .text : { *(.text) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : {
        __recovery_bss_start = .;
        *(.bss)
        *(COMMON)
        __recovery_bss_end = .;
    }
    __recovery_end = .;
    ASSERT(__recovery_end <= 0x00A00000, "recovery loader exceeds reserved memory")
}
