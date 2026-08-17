.text

.global x86_64_context_switch
.type x86_64_context_switch, @function
x86_64_context_switch:
    movq %rsp, 0(%rdi)
    movq %rbx, 8(%rdi)
    movq %rbp, 16(%rdi)
    movq %r12, 24(%rdi)
    movq %r13, 32(%rdi)
    movq %r14, 40(%rdi)
    movq %r15, 48(%rdi)

    movq 0(%rsi), %rsp
    movq 8(%rsi), %rbx
    movq 16(%rsi), %rbp
    movq 24(%rsi), %r12
    movq 32(%rsi), %r13
    movq 40(%rsi), %r14
    movq 48(%rsi), %r15
    ret
.size x86_64_context_switch, .-x86_64_context_switch

.global x86_64_context_test_callee_saved
.type x86_64_context_test_callee_saved, @function
x86_64_context_test_callee_saved:
    testq %rdi, %rdi
    je .Lregister_test_null

    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $8, %rsp
    movq %rsp, (%rsp)

    movabsq $0x1122334455667788, %rbx
    movabsq $0x2233445566778899, %rbp
    movabsq $0x33445566778899aa, %r12
    movabsq $0x445566778899aabb, %r13
    movabsq $0x5566778899aabbcc, %r14
    movabsq $0x66778899aabbccdd, %r15

    call *%rdi

    movq (%rsp), %r10
    cmpq %r10, %rsp
    jne .Lregister_test_fail
    movabsq $0x1122334455667788, %r10
    cmpq %r10, %rbx
    jne .Lregister_test_fail
    movabsq $0x2233445566778899, %r10
    cmpq %r10, %rbp
    jne .Lregister_test_fail
    movabsq $0x33445566778899aa, %r10
    cmpq %r10, %r12
    jne .Lregister_test_fail
    movabsq $0x445566778899aabb, %r10
    cmpq %r10, %r13
    jne .Lregister_test_fail
    movabsq $0x5566778899aabbcc, %r10
    cmpq %r10, %r14
    jne .Lregister_test_fail
    movabsq $0x66778899aabbccdd, %r10
    cmpq %r10, %r15
    jne .Lregister_test_fail

    movl $1, %eax
    jmp .Lregister_test_restore

.Lregister_test_fail:
    xorl %eax, %eax

.Lregister_test_restore:
    addq $8, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

.Lregister_test_null:
    xorl %eax, %eax
    ret
.size x86_64_context_test_callee_saved, .-x86_64_context_test_callee_saved

.section .note.GNU-stack,"",@progbits
