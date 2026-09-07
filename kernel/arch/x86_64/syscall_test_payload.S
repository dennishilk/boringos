.section .text
.code64

/*
 * Position-independent acceptance payload copied into the fixed user code
 * page. All result storage and the valid DEBUG_WRITE buffer are addressed
 * relative to the unchanged user RSP, so the copied instruction stream does
 * not depend on its kernel link address.
 *
 * Result block starts at user RSP - 0x100.
 * Valid debug message lives at user RSP - 0xe00.
 */
.align 16
.globl x86_64_syscall_test_payload_start
.type x86_64_syscall_test_payload_start, @object
x86_64_syscall_test_payload_start:
    xorl %eax, %eax
    movw %cs, %ax
    movq %rax, -0x100(%rsp)
    movq %rsp, %rax
    movq %rax, -0xf8(%rsp)

    movabsq $0x1111111122222222, %rbx
    movabsq $0x3333333344444444, %rbp
    movabsq $0x5555555566666666, %r12
    movabsq $0x7777777788888888, %r13
    movabsq $0x99999999aaaaaaaa, %r14
    movabsq $0xbbbbbbbbcccccccc, %r15

    /* BORING_SYS_GETPID = 0. */
    xorl %eax, %eax
    syscall
    movq %rax, -0xf0(%rsp)
    movabsq $0x5359535245544f4b, %rax
    movq %rax, -0xe8(%rsp)
    xorl %eax, %eax
    movw %cs, %ax
    movq %rax, -0xe0(%rsp)
    movq %rsp, %rax
    movq %rax, -0xd8(%rsp)

    /* BORING_SYS_DEBUG_WRITE = 1: valid mapped user buffer. */
    leaq -0xe00(%rsp), %rdi
    movq $25, %rsi
    movq $1, %rax
    syscall
    movq %rax, -0xd0(%rsp)

    /* Unmapped canonical lower-half pointer. */
    movabsq $0x0000000040020000, %rdi
    movq $4, %rsi
    movq $1, %rax
    syscall
    movq %rax, -0xc8(%rsp)

    /* Shared higher-half kernel pointer. */
    movabsq $0xffffffff80000000, %rdi
    movq $4, %rsi
    movq $1, %rax
    syscall
    movq %rax, -0xc0(%rsp)

    /* Integer-overflowing pointer + length range. */
    movabsq $0xfffffffffffffff8, %rdi
    movq $16, %rsi
    movq $1, %rax
    syscall
    movq %rax, -0xb8(%rsp)

    /* Oversized DEBUG_WRITE length with otherwise valid pointer. */
    leaq -0xe00(%rsp), %rdi
    movq $65, %rsi
    movq $1, %rax
    syscall
    movq %rax, -0xb0(%rsp)

    /* Unknown syscall number must return ENOSYS, not fault. */
    movq $0xffff, %rax
    syscall
    movq %rax, -0xa8(%rsp)

    /* BORING_SYS_FD_READ = 19: invalid descriptor. */
    movq $99, %rdi
    leaq -0xe00(%rsp), %rsi
    movq $1, %rdx
    movq $19, %rax
    syscall
    movq %rax, -0x90(%rsp)

    /* BORING_SYS_FD_READ = 19: stdout is write-only. */
    movq $1, %rdi
    leaq -0xe00(%rsp), %rsi
    movq $1, %rdx
    movq $19, %rax
    syscall
    movq %rax, -0x88(%rsp)

    /* BORING_SYS_FD_WRITE = 20: stdin is read-only. */
    xorl %edi, %edi
    leaq -0xe00(%rsp), %rsi
    movq $1, %rdx
    movq $20, %rax
    syscall
    movq %rax, -0x80(%rsp)

    /* FD_READ on stdin must reject an unmapped destination before blocking. */
    xorl %edi, %edi
    movabsq $0x0000000040020000, %rsi
    movq $1, %rdx
    movq $19, %rax
    syscall
    movq %rax, -0x78(%rsp)

    /* FD_WRITE must reject a transfer larger than the 4096-byte ABI bound. */
    movq $1, %rdi
    leaq -0xe00(%rsp), %rsi
    movq $4097, %rdx
    movq $20, %rax
    syscall
    movq %rax, -0x70(%rsp)

    /* BORING_SYS_INPUT_READ = 23: no owner must be rejected before wait. */
    leaq -0xe00(%rsp), %rdi
    movq $1, %rsi
    movq $23, %rax
    syscall
    movq %rax, -0x68(%rsp)

    /* BORING_SYS_INPUT_RELEASE = 24: no owner must be rejected. */
    movq $24, %rax
    syscall
    movq %rax, -0x60(%rsp)

    /* BORING_SYS_INPUT_CLAIM = 22: first and same-owner second claim succeed. */
    movq $22, %rax
    syscall
    movq %rax, -0x58(%rsp)
    movq $22, %rax
    syscall
    movq %rax, -0x50(%rsp)

    /* INPUT_READ validates destination before consuming or blocking. */
    movabsq $0x0000000040020000, %rdi
    movq $1, %rsi
    movq $23, %rax
    syscall
    movq %rax, -0x48(%rsp)

    /* INPUT_READ count bounds are 1..16 events. */
    leaq -0xe00(%rsp), %rdi
    xorl %esi, %esi
    movq $23, %rax
    syscall
    movq %rax, -0x40(%rsp)
    leaq -0xe00(%rsp), %rdi
    movq $17, %rsi
    movq $23, %rax
    syscall
    movq %rax, -0x38(%rsp)

    /* Owner release succeeds after deterministic same-owner claim. */
    movq $24, %rax
    syscall
    movq %rax, -0x30(%rsp)

    movq $1, %rax
    movabsq $0x1111111122222222, %rcx
    cmpq %rcx, %rbx
    jne .Lcallee_bad
    movabsq $0x3333333344444444, %rcx
    cmpq %rcx, %rbp
    jne .Lcallee_bad
    movabsq $0x5555555566666666, %rcx
    cmpq %rcx, %r12
    jne .Lcallee_bad
    movabsq $0x7777777788888888, %rcx
    cmpq %rcx, %r13
    jne .Lcallee_bad
    movabsq $0x99999999aaaaaaaa, %rcx
    cmpq %rcx, %r14
    jne .Lcallee_bad
    movabsq $0xbbbbbbbbcccccccc, %rcx
    cmpq %rcx, %r15
    jne .Lcallee_bad
    jmp .Lcallee_store
.Lcallee_bad:
    xorl %eax, %eax
.Lcallee_store:
    movq %rax, -0xa0(%rsp)

    movabsq $0x53595343414c4c21, %rax
    movq %rax, -0x98(%rsp)

.globl x86_64_syscall_test_payload_cli
x86_64_syscall_test_payload_cli:
    cli
    ud2

.globl x86_64_syscall_test_payload_end
x86_64_syscall_test_payload_end:
.size x86_64_syscall_test_payload_start, .-x86_64_syscall_test_payload_start

.section .note.GNU-stack,"",@progbits
