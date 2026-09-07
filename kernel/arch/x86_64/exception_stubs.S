.section .text
.code64

.extern x86_64_exception_dispatch

.macro EXCEPTION_NO_ERROR vector
.globl x86_64_exception_\vector
.type x86_64_exception_\vector, @function
x86_64_exception_\vector:
    pushq $0
    pushq $\vector
    jmp x86_64_exception_common
.size x86_64_exception_\vector, .-x86_64_exception_\vector
.endm

.macro EXCEPTION_WITH_ERROR vector
.globl x86_64_exception_\vector
.type x86_64_exception_\vector, @function
x86_64_exception_\vector:
    pushq $\vector
    jmp x86_64_exception_common
.size x86_64_exception_\vector, .-x86_64_exception_\vector
.endm

EXCEPTION_NO_ERROR 0
EXCEPTION_NO_ERROR 1
EXCEPTION_NO_ERROR 2
EXCEPTION_NO_ERROR 3
EXCEPTION_NO_ERROR 4
EXCEPTION_NO_ERROR 5
EXCEPTION_NO_ERROR 6
EXCEPTION_NO_ERROR 7
EXCEPTION_WITH_ERROR 8
EXCEPTION_NO_ERROR 9
EXCEPTION_WITH_ERROR 10
EXCEPTION_WITH_ERROR 11
EXCEPTION_WITH_ERROR 12
EXCEPTION_WITH_ERROR 13
EXCEPTION_WITH_ERROR 14
EXCEPTION_NO_ERROR 15
EXCEPTION_NO_ERROR 16
EXCEPTION_WITH_ERROR 17
EXCEPTION_NO_ERROR 18
EXCEPTION_NO_ERROR 19
EXCEPTION_NO_ERROR 20
EXCEPTION_WITH_ERROR 21
EXCEPTION_NO_ERROR 22
EXCEPTION_NO_ERROR 23
EXCEPTION_NO_ERROR 24
EXCEPTION_NO_ERROR 25
EXCEPTION_NO_ERROR 26
EXCEPTION_NO_ERROR 27
EXCEPTION_NO_ERROR 28
EXCEPTION_WITH_ERROR 29
EXCEPTION_WITH_ERROR 30
EXCEPTION_NO_ERROR 31

.type x86_64_exception_common, @function
x86_64_exception_common:
    cld

    /* Save interrupted general-purpose register values first. */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /*
     * Long-mode exception entry has a hardware return frame containing
     * RIP, CS, RFLAGS, RSP and SS even for CPL0 -> CPL0 delivery. After the
     * 15 GPR pushes and normalized vector/error pair, the CPU-saved RSP and
     * SS are at offsets 160 and 168 from this point. Copy those actual values
     * to the two C-facing normalized fields while leaving the hardware words
     * intact at the tail of the complete frame.
     */
    movq 160(%rsp), %rax
    movq 168(%rsp), %rdx
    pushq %rdx
    pushq %rax

    movq %rsp, %rdi
    andq $-16, %rsp
    call x86_64_exception_dispatch

    /* The C dispatcher is noreturn. Never iret a fatal exception here. */
    ud2
.size x86_64_exception_common, .-x86_64_exception_common

.globl x86_64_load_idt
.type x86_64_load_idt, @function
x86_64_load_idt:
    /* Hardware IRQ vectors are not configured yet, so keep IF cleared. */
    cli
    lidt (%rdi)
    ret
.size x86_64_load_idt, .-x86_64_load_idt

.globl x86_64_store_idt
.type x86_64_store_idt, @function
x86_64_store_idt:
    sidt (%rdi)
    ret
.size x86_64_store_idt, .-x86_64_store_idt

.globl x86_64_read_cs
.type x86_64_read_cs, @function
x86_64_read_cs:
    xorl %eax, %eax
    movw %cs, %ax
    ret
.size x86_64_read_cs, .-x86_64_read_cs

.globl x86_64_read_cr2
.type x86_64_read_cr2, @function
x86_64_read_cr2:
    movq %cr2, %rax
    ret
.size x86_64_read_cr2, .-x86_64_read_cr2

.globl x86_64_trigger_divide_error
.type x86_64_trigger_divide_error, @function
x86_64_trigger_divide_error:
    movq $1, %rax
    xorl %edx, %edx
    xorl %ecx, %ecx
    divq %rcx
    ud2
.size x86_64_trigger_divide_error, .-x86_64_trigger_divide_error

.globl x86_64_trigger_page_fault
.type x86_64_trigger_page_fault, @function
x86_64_trigger_page_fault:
    movq (%rdi), %rax
    ud2
.size x86_64_trigger_page_fault, .-x86_64_trigger_page_fault

.section .rodata
.align 8
.globl x86_64_exception_stub_table
.type x86_64_exception_stub_table, @object
x86_64_exception_stub_table:
    .quad x86_64_exception_0
    .quad x86_64_exception_1
    .quad x86_64_exception_2
    .quad x86_64_exception_3
    .quad x86_64_exception_4
    .quad x86_64_exception_5
    .quad x86_64_exception_6
    .quad x86_64_exception_7
    .quad x86_64_exception_8
    .quad x86_64_exception_9
    .quad x86_64_exception_10
    .quad x86_64_exception_11
    .quad x86_64_exception_12
    .quad x86_64_exception_13
    .quad x86_64_exception_14
    .quad x86_64_exception_15
    .quad x86_64_exception_16
    .quad x86_64_exception_17
    .quad x86_64_exception_18
    .quad x86_64_exception_19
    .quad x86_64_exception_20
    .quad x86_64_exception_21
    .quad x86_64_exception_22
    .quad x86_64_exception_23
    .quad x86_64_exception_24
    .quad x86_64_exception_25
    .quad x86_64_exception_26
    .quad x86_64_exception_27
    .quad x86_64_exception_28
    .quad x86_64_exception_29
    .quad x86_64_exception_30
    .quad x86_64_exception_31
.size x86_64_exception_stub_table, .-x86_64_exception_stub_table

.section .note.GNU-stack,"",@progbits
