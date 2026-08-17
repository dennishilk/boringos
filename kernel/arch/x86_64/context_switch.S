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

.global x86_64_preemption_probe_reset
.type x86_64_preemption_probe_reset, @function
x86_64_preemption_probe_reset:
    movb $0, x86_64_preemption_probe_release_flag(%rip)
    ret
.size x86_64_preemption_probe_reset, .-x86_64_preemption_probe_reset

.global x86_64_preemption_probe_release
.type x86_64_preemption_probe_release, @function
x86_64_preemption_probe_release:
    movb $1, x86_64_preemption_probe_release_flag(%rip)
    ret
.size x86_64_preemption_probe_release, .-x86_64_preemption_probe_release

/*
 * This probe deliberately keeps every integer caller-saved register live
 * while waiting for a different kernel task to run. The wait loop uses only
 * a RIP-relative byte compare, so no GPR is repurposed before validation.
 */
.global x86_64_context_test_preemptive_gprs
.type x86_64_context_test_preemptive_gprs, @function
x86_64_context_test_preemptive_gprs:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $8, %rsp
    movq %rsp, (%rsp)

    movq $0x11111111, %rax
    movq $0x12222222, %rbx
    movq $0x13333333, %rcx
    movq $0x14444444, %rdx
    movq $0x15555555, %rsi
    movq $0x16666666, %rdi
    movq $0x17777777, %rbp
    movq $0x18888888, %r8
    movq $0x19999999, %r9
    movq $0x1aaaaaaa, %r10
    movq $0x1bbbbbbb, %r11
    movq $0x1ccccccc, %r12
    movq $0x1ddddddd, %r13
    movq $0x1eeeeeee, %r14
    movq $0x1fffffff, %r15

.Lpreemption_probe_wait:
    cmpb $0, x86_64_preemption_probe_release_flag(%rip)
    je .Lpreemption_probe_wait

    cmpq $0x11111111, %rax
    jne .Lpreemption_probe_fail
    cmpq $0x12222222, %rbx
    jne .Lpreemption_probe_fail
    cmpq $0x13333333, %rcx
    jne .Lpreemption_probe_fail
    cmpq $0x14444444, %rdx
    jne .Lpreemption_probe_fail
    cmpq $0x15555555, %rsi
    jne .Lpreemption_probe_fail
    cmpq $0x16666666, %rdi
    jne .Lpreemption_probe_fail
    cmpq $0x17777777, %rbp
    jne .Lpreemption_probe_fail
    cmpq $0x18888888, %r8
    jne .Lpreemption_probe_fail
    cmpq $0x19999999, %r9
    jne .Lpreemption_probe_fail
    cmpq $0x1aaaaaaa, %r10
    jne .Lpreemption_probe_fail
    cmpq $0x1bbbbbbb, %r11
    jne .Lpreemption_probe_fail
    cmpq $0x1ccccccc, %r12
    jne .Lpreemption_probe_fail
    cmpq $0x1ddddddd, %r13
    jne .Lpreemption_probe_fail
    cmpq $0x1eeeeeee, %r14
    jne .Lpreemption_probe_fail
    cmpq $0x1fffffff, %r15
    jne .Lpreemption_probe_fail
    cmpq (%rsp), %rsp
    jne .Lpreemption_probe_fail

    movl $1, %eax
    jmp .Lpreemption_probe_restore

.Lpreemption_probe_fail:
    xorl %eax, %eax

.Lpreemption_probe_restore:
    addq $8, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret
.size x86_64_context_test_preemptive_gprs, .-x86_64_context_test_preemptive_gprs

.bss
.align 1
x86_64_preemption_probe_release_flag:
    .byte 0

.section .note.GNU-stack,"",@progbits
