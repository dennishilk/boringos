#include <boring/cpu.h>

void x86_64_halt_forever(void) {
    /* cli/hlt are privileged CPU instructions and require tiny inline assembly. */
    __asm__ volatile ("cli");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
