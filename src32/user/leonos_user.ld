OUTPUT_FORMAT(elf32-i386)
ENTRY(leo_header)

SECTIONS
{
    . = 0x40000000;

    .text : ALIGN(4)
    {
        KEEP(*(.text.leo_header))
        *(.text*)
    }

    .rodata : ALIGN(4)
    {
        *(.rodata*)
    }

    .data : ALIGN(4)
    {
        *(.data*)
    }

    . = ALIGN(4);
    leo_image_end = .;

    __bss_start = .;
    .bss (NOLOAD) : ALIGN(4)
    {
        *(.bss*)
        *(COMMON)
    }
    __bss_end = .;
    leo_bss_size = __bss_end - __bss_start;

    /DISCARD/ :
    {
        *(.comment*)
        *(.note*)
        *(.eh_frame*)
        *(.llvm_addrsig*)
    }
}
