.section .text
.code64

.globl x86_64_enter_ring3
.type x86_64_enter_ring3, @function
x86_64_enter_ring3:
    /*
     * RDI = user RIP
     * RSI = user RSP
     * DX  = user CS (RPL 3)
     * CX  = user SS (RPL 3)
     * R8  = user result-memory address passed to the payload in RDI
     *
     * IF remains cleared deliberately for the first controlled CPL3 proof.
     */
    movq %rdi, %rax

    movw %cx, %r9w
    movw %r9w, %ds
    movw %r9w, %es

    movzwq %cx, %r9
    pushq %r9
    pushq %rsi
    pushq $0x2
    movzwq %dx, %r9
    pushq %r9
    pushq %rax

    movq %r8, %rdi
    iretq

    ud2
.size x86_64_enter_ring3, .-x86_64_enter_ring3

/*
 * Position-independent CPL3 payload copied byte-for-byte into one user page.
 * RDI is supplied by x86_64_enter_ring3 and points into the user stack page.
 */
.align 16
.globl x86_64_ring3_payload_start
.type x86_64_ring3_payload_start, @object
x86_64_ring3_payload_start:
    xorl %eax, %eax
    movw %cs, %ax
    movq %rax, 0(%rdi)
    movq %rsp, %rax
    movq %rax, 8(%rdi)
    movabsq $0x52494e4733544553, %rax
    movq %rax, 16(%rdi)
    pushq $0x5a
    popq %rax
    movq %rax, 24(%rdi)

.globl x86_64_ring3_payload_cli
x86_64_ring3_payload_cli:
    cli
    ud2
.globl x86_64_ring3_payload_end
x86_64_ring3_payload_end:
.size x86_64_ring3_payload_start, .-x86_64_ring3_payload_start

.section .note.GNU-stack,"",@progbits
