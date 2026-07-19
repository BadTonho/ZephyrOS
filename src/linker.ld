OUTPUT_FORMAT(binary)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS {
    . = 0x10000;
    __kernel_start = .;

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }

    __kernel_end = .;
    ASSERT(__kernel_end <= 0x40000, "kernel exceeds reserved memory")
}
