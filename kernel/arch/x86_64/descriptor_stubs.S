.section .text
.code64

.globl x86_64_load_gdt
.type x86_64_load_gdt, @function
x86_64_load_gdt:
    /* RDI = GDTR, SI = new kernel CS, DX = new kernel data selector. */
    lgdt (%rdi)

    movw %dx, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorl %eax, %eax
    movw %ax, %fs
    movw %ax, %gs

    /* Reload CS against the new GDT with a far return. */
    movzwq %si, %rax
    pushq %rax
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    ret
.size x86_64_load_gdt, .-x86_64_load_gdt

.globl x86_64_store_gdt
.type x86_64_store_gdt, @function
x86_64_store_gdt:
    sgdt (%rdi)
    ret
.size x86_64_store_gdt, .-x86_64_store_gdt

.globl x86_64_load_tr
.type x86_64_load_tr, @function
x86_64_load_tr:
    movw %di, %ax
    ltr %ax
    ret
.size x86_64_load_tr, .-x86_64_load_tr

.globl x86_64_store_tr
.type x86_64_store_tr, @function
x86_64_store_tr:
    xorl %eax, %eax
    str %ax
    ret
.size x86_64_store_tr, .-x86_64_store_tr

.globl x86_64_read_ds
.type x86_64_read_ds, @function
x86_64_read_ds:
    xorl %eax, %eax
    movw %ds, %ax
    ret
.size x86_64_read_ds, .-x86_64_read_ds

.section .note.GNU-stack,"",@progbits
