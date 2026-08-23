ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data   PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x0000000040000000;
    .text : ALIGN(0x1000)
    {
        KEEP(*(.text))
        *(.text.*)
    } :text

    . = 0x0000000040001000;
    .rodata : ALIGN(0x1000)
    {
        *(.rodata)
        *(.rodata.*)
    } :rodata

    . = 0x0000000040002000;
    .data : ALIGN(0x1000)
    {
        *(.data)
        *(.data.*)
    } :data

    . = 0x0000000040002100;
    .bss (NOLOAD) : ALIGN(8)
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
