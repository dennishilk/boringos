.section .text.entry
.code64
.globl _start
.type _start, @function
_start:
    movq %rsp, %rax
    andq $0xf, %rax
    jne .Lbad_stack

    call boring_main
    movslq %eax, %r12

.globl boring_runtime_after_main
boring_runtime_after_main:
    cli
    ud2

.Lbad_stack:
    ud2
.size _start, .-_start

.section .note.GNU-stack,"",@progbits
