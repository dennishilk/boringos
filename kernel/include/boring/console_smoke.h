#ifndef BORING_CONSOLE_SMOKE_H
#define BORING_CONSOLE_SMOKE_H

#define BORING_CONSOLE_SMOKE_TEXT_VA 0x0000000040000000ULL
#define BORING_CONSOLE_SMOKE_RODATA_VA 0x0000000040001000ULL
#define BORING_CONSOLE_SMOKE_DATA_VA 0x0000000040002000ULL
#define BORING_CONSOLE_SMOKE_BSS_VA 0x0000000040002100ULL
#define BORING_CONSOLE_SMOKE_STACK_BASE 0x0000000040010000ULL
#define BORING_CONSOLE_SMOKE_STACK_TOP 0x0000000040011000ULL

#define BORING_CONSOLE_SMOKE_MESSAGE_LENGTH 38U
#define BORING_CONSOLE_SMOKE_ECHO_LENGTH 2U
#define BORING_CONSOLE_SMOKE_INPUT_BYTE 0x4bU
#define BORING_CONSOLE_SMOKE_MAIN_RETURN 43
#define BORING_CONSOLE_SMOKE_DATA_MARKER 0x434f4e534f4c4531ULL
#define BORING_CONSOLE_SMOKE_BSS_MARKER 0x434f4e534f4c4542ULL

#ifndef __ASSEMBLER__
#include <stdint.h>

struct boring_console_smoke_result {
    uint64_t cs;
    uint64_t entered;
    uint64_t data_ok;
    uint64_t bss_zero;
    uint64_t getpid_result;
    uint64_t getpid_ok;
    uint64_t write_result;
    uint64_t write_ok;
    uint64_t read_result;
    uint64_t read_value;
    uint64_t read_ok;
    uint64_t echo_result;
    uint64_t echo_ok;
    uint64_t sysret_resume;
    uint64_t ready_to_return;
};
#endif

#endif
