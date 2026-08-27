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


/*
 * Scheduler-started Ring3 process entry.
 * RDI = user RIP, RSI = user RSP, DX = user CS, CX = user SS,
 * R8 = argc, R9 = argv.  Unlike the early Ring3 proof, userspace enters
 * with IF set so the established timer/input/scheduler path remains live.
 * Keep this function in .text: the lost M36 tree already proved that placing
 * this entry in a non-loadable/incorrect section causes a real linker fault.
 */
.section .text
.globl x86_64_enter_ring3_argv
.type x86_64_enter_ring3_argv, @function
x86_64_enter_ring3_argv:
    movq %rdi, %rax
    movq %r9, %r11

    movw %cx, %r10w
    movw %r10w, %ds
    movw %r10w, %es

    movzwq %cx, %r10
    pushq %r10
    pushq %rsi
    pushq $0x202
    movzwq %dx, %r10
    pushq %r10
    pushq %rax

    movq %r8, %rdi
    movq %r11, %rsi
    iretq

    ud2
.size x86_64_enter_ring3_argv, .-x86_64_enter_ring3_argv

.section .note.GNU-stack,"",@progbits
