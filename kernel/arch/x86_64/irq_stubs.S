.section .text
.code64

.extern x86_64_irq_dispatch

.macro IRQ_STUB vector
.globl x86_64_irq_\vector
.type x86_64_irq_\vector, @function
x86_64_irq_\vector:
    pushq $0
    pushq $\vector
    jmp x86_64_irq_common
.size x86_64_irq_\vector, .-x86_64_irq_\vector
.endm

IRQ_STUB 32
IRQ_STUB 33
IRQ_STUB 34
IRQ_STUB 35
IRQ_STUB 36
IRQ_STUB 37
IRQ_STUB 38
IRQ_STUB 39
IRQ_STUB 40
IRQ_STUB 41
IRQ_STUB 42
IRQ_STUB 43
IRQ_STUB 44
IRQ_STUB 45
IRQ_STUB 46
IRQ_STUB 47

.type x86_64_irq_common, @function
x86_64_irq_common:
    cld

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
     * Match the existing CPL0 trap-frame convention. Hardware IRQs do not
     * push an error code, so the per-vector stub supplies a zero error field.
     * With 15 saved GPRs plus vector/error/RIP/CS/RFLAGS, interrupted RSP is
     * 160 bytes above the current RSP. No privilege transition occurs yet,
     * so SS is read explicitly rather than pretending the CPU pushed it.
     */
    leaq 160(%rsp), %rax
    xorl %edx, %edx
    movw %ss, %dx
    pushq %rdx
    pushq %rax

    movq %rsp, %rdi
    movq %rsp, %rbx
    andq $-16, %rsp
    call x86_64_irq_dispatch
    movq %rbx, %rsp

    addq $16, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq
.size x86_64_irq_common, .-x86_64_irq_common

.section .rodata
.align 8
.globl x86_64_irq_stub_table
.type x86_64_irq_stub_table, @object
x86_64_irq_stub_table:
    .quad x86_64_irq_32
    .quad x86_64_irq_33
    .quad x86_64_irq_34
    .quad x86_64_irq_35
    .quad x86_64_irq_36
    .quad x86_64_irq_37
    .quad x86_64_irq_38
    .quad x86_64_irq_39
    .quad x86_64_irq_40
    .quad x86_64_irq_41
    .quad x86_64_irq_42
    .quad x86_64_irq_43
    .quad x86_64_irq_44
    .quad x86_64_irq_45
    .quad x86_64_irq_46
    .quad x86_64_irq_47
.size x86_64_irq_stub_table, .-x86_64_irq_stub_table

.section .note.GNU-stack,"",@progbits
