.section .text
.code64

.extern x86_64_syscall_stack
.extern x86_64_syscall_user_rsp_scratch
.extern x86_64_syscall_dispatch_m36
.extern x86_64_syscall_active_stack_top

.globl x86_64_syscall_entry
.type x86_64_syscall_entry, @function
x86_64_syscall_entry:
    /*
     * Architectural SYSCALL entry state:
     *   RCX = user return RIP
     *   R11 = user RFLAGS
     *   RSP = still the untrusted user RSP
     *   RAX = syscall number
     *
     * Do not push or call while RSP is user-controlled. The single scratch
     * word is acceptable only for the current single-CPU, non-preemptible
     * bootstrap user path.
     */
    movq %rsp, x86_64_syscall_user_rsp_scratch(%rip)
    movq x86_64_syscall_active_stack_top(%rip), %rsp
    testq %rsp, %rsp
    jnz .Lm34_stack_ready
    leaq x86_64_syscall_stack+16384(%rip), %rsp
.Lm34_stack_ready:

    /*
     * x86_64_syscall_frame is 144 bytes, keeping the trusted stack
     * 16-byte aligned before CALL as required by the SysV AMD64 ABI.
     */
    subq $144, %rsp

    movq %rcx, 8(%rsp)
    movq %r11, 16(%rsp)
    movq %rax, 24(%rsp)
    movq %rbx, 32(%rsp)
    movq %rbp, 40(%rsp)
    movq %r12, 48(%rsp)
    movq %r13, 56(%rsp)
    movq %r14, 64(%rsp)
    movq %r15, 72(%rsp)
    movq %rdi, 80(%rsp)
    movq %rsi, 88(%rsp)
    movq %rdx, 96(%rsp)
    movq %r10, 104(%rsp)
    movq %r8, 112(%rsp)
    movq %r9, 120(%rsp)
    movq $0, 128(%rsp)
    movq $0, 136(%rsp)

    movq x86_64_syscall_user_rsp_scratch(%rip), %rax
    movq %rax, 0(%rsp)

    /* FMASK clears DF, but CLD is deliberate defense at the C boundary. */
    cld
    movq %rsp, %rdi
    call x86_64_syscall_dispatch_m36

    /*
     * The C dispatcher has validated/sanitized user RIP/RSP/RFLAGS and set
     * frame->result. RCX/R11 are architectural SYSCALL/SYSRET clobbers.
     */
    movq 0(%rsp), %rax
    movq %rax, x86_64_syscall_user_rsp_scratch(%rip)
    movq 8(%rsp), %rcx
    movq 16(%rsp), %r11
    movq 32(%rsp), %rbx
    movq 40(%rsp), %rbp
    movq 48(%rsp), %r12
    movq 56(%rsp), %r13
    movq 64(%rsp), %r14
    movq 72(%rsp), %r15
    movq 80(%rsp), %rdi
    movq 88(%rsp), %rsi
    movq 96(%rsp), %rdx
    movq 104(%rsp), %r10
    movq 112(%rsp), %r8
    movq 120(%rsp), %r9
    movq 128(%rsp), %rax

    movq x86_64_syscall_user_rsp_scratch(%rip), %rsp
    sysretq

    ud2
.size x86_64_syscall_entry, .-x86_64_syscall_entry

.section .note.GNU-stack,"",@progbits
