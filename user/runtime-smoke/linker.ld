OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x0000000040000000;
    .text : ALIGN(0x1000)
    {
        KEEP(*(.text.entry))
        *(.text .text.*)
    } :text

    . = 0x0000000040001000;
    .rodata : ALIGN(0x1000)
    {
        *(.rodata .rodata.*)
    } :rodata

    . = 0x0000000040002000;
    .runtime_result : ALIGN(16)
    {
        KEEP(*(.runtime_result))
    } :data

    .data : ALIGN(8)
    {
        *(.data .data.*)
    } :data

    . = 0x0000000040002100;
    .bss (NOLOAD) : ALIGN(8)
    {
        *(.bss.runtime_probe)
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.gcc_except_table*)
    }
}
