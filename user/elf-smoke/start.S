#include <boring/elf_smoke.h>
#include <boring/syscall_abi.h>

.section .text
.code64
.globl _start
.type _start, @function
_start:
    xorl %eax, %eax
    movw %cs, %ax
    movq %rax, elf_smoke_result + BORING_ELF_RESULT_CS_OFFSET(%rip)
    cmpw $0x23, %ax
    jne .Lfail
    movq $1, elf_smoke_result + BORING_ELF_RESULT_ENTERED_OFFSET(%rip)

    movq elf_smoke_bss_probe(%rip), %rax
    testq %rax, %rax
    jne .Lfail
    movq $1, elf_smoke_result + BORING_ELF_RESULT_BSS_ZERO_OFFSET(%rip)

    movabsq $BORING_ELF_SMOKE_BSS_MARKER, %rax
    movq %rax, elf_smoke_bss_probe(%rip)
    movq elf_smoke_bss_probe(%rip), %rcx
    cmpq %rax, %rcx
    jne .Lfail
    movq %rcx, elf_smoke_result + BORING_ELF_RESULT_BSS_VALUE_OFFSET(%rip)
    movq $1, elf_smoke_result + BORING_ELF_RESULT_BSS_WRITE_OFFSET(%rip)

    movq $BORING_SYS_GETPID, %rax
    syscall
    movq %rax, elf_smoke_result + BORING_ELF_RESULT_GETPID_OFFSET(%rip)
    cmpq $1, %rax
    jne .Lfail
    movq $1, elf_smoke_result + BORING_ELF_RESULT_GETPID_PASS_OFFSET(%rip)

    leaq elf_smoke_message(%rip), %rdi
    movq $BORING_ELF_SMOKE_MESSAGE_LENGTH, %rsi
    movq $BORING_SYS_DEBUG_WRITE, %rax
    syscall
    movq %rax, elf_smoke_result + BORING_ELF_RESULT_DEBUG_RESULT_OFFSET(%rip)
    cmpq $BORING_ELF_SMOKE_MESSAGE_LENGTH, %rax
    jne .Lfail
    movq $1, elf_smoke_result + BORING_ELF_RESULT_DEBUG_PASS_OFFSET(%rip)
    movq $1, elf_smoke_result + BORING_ELF_RESULT_SYSRET_RESUME_OFFSET(%rip)

    movabsq $BORING_ELF_SMOKE_FINAL_MARKER, %rax
    movq %rax, elf_smoke_result + BORING_ELF_RESULT_FINAL_MARKER_OFFSET(%rip)
    movq $1, elf_smoke_result + BORING_ELF_RESULT_FINAL_CLI_OFFSET(%rip)

.globl elf_smoke_cli
elf_smoke_cli:
    cli
    ud2

.Lfail:
    ud2
.size _start, .-_start

.section .rodata
.align 16
.globl elf_smoke_message
elf_smoke_message:
    .ascii "hello from BoringOS ELF userspace"

.section .data
.align 16
.globl elf_smoke_result
elf_smoke_result:
    .zero BORING_ELF_RESULT_SIZE

.section .bss
.align 8
.globl elf_smoke_bss_probe
elf_smoke_bss_probe:
    .zero 8

.section .note.GNU-stack,"",@progbits
