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

    . = ALIGN(0x1000);
    .rodata : ALIGN(0x1000)
    {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(0x1000);
    .data : ALIGN(8)
    {
        *(.data .data.*)
    } :data

    .bss (NOLOAD) : ALIGN(8)
    {
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
