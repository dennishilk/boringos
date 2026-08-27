#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/pty.h>

/*
 * Host-side PTY regression: compile the kernel PTY core directly with a tiny
 * scheduler wake stub so ring, generation, HUP and isolation semantics stay
 * testable without QEMU.
 */
static uint64_t last_woken_pid;

bool task_wake_pid(uint64_t pid);
bool task_wake_pid(uint64_t pid) {
    last_woken_pid = pid;
    return true;
}

#include "../kernel/core/pty.c"

static void expect_bytes(const uint8_t *actual, const char *expected,
                         size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        assert(actual[index] == (uint8_t)expected[index]);
    }
}

int main(void) {
    struct pty_handle master;
    struct pty_handle slave;
    struct pty_handle master2;
    struct pty_handle slave2;
    struct pty_handle stale_master;
    struct pty_poll_state poll;
    uint8_t bytes[KERNEL_PTY_RING_CAPACITY + 16U];
    size_t transferred;
    size_t index;

    assert(pty_init());
    assert(pty_create_pair(&master, &slave) == PTY_RESULT_OK);
    stale_master = master;

    assert(pty_write(master, "abc", 3U, &transferred) == PTY_RESULT_OK);
    assert(transferred == 3U);
    assert(pty_poll(slave, &poll) == PTY_RESULT_OK);
    assert(poll.readable && !poll.hup);
    assert(pty_read(slave, bytes, 2U, &transferred) == PTY_RESULT_OK);
    assert(transferred == 2U);
    expect_bytes(bytes, "ab", 2U);
    assert(pty_read(slave, bytes, sizeof(bytes), &transferred) == PTY_RESULT_OK);
    assert(transferred == 1U && bytes[0] == (uint8_t)'c');
    assert(pty_read(slave, bytes, 1U, &transferred) == PTY_RESULT_WOULD_BLOCK);

    assert(pty_write(slave, "XY", 2U, &transferred) == PTY_RESULT_OK);
    assert(pty_read(master, bytes, sizeof(bytes), &transferred) == PTY_RESULT_OK);
    assert(transferred == 2U);
    expect_bytes(bytes, "XY", 2U);

    for (index = 0U; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(index & 0xffU);
    }
    assert(pty_write(master, bytes, KERNEL_PTY_RING_CAPACITY, &transferred) == PTY_RESULT_OK);
    assert(transferred == KERNEL_PTY_RING_CAPACITY);
    assert(pty_write(master, bytes, 1U, &transferred) == PTY_RESULT_WOULD_BLOCK);
    assert(transferred == 0U);
    assert(pty_read(slave, bytes, 17U, &transferred) == PTY_RESULT_OK);
    assert(transferred == 17U);
    assert(pty_write(master, bytes, 17U, &transferred) == PTY_RESULT_OK);
    assert(transferred == 17U);
    assert(pty_read(slave, bytes, sizeof(bytes), &transferred) == PTY_RESULT_OK);
    assert(transferred == KERNEL_PTY_RING_CAPACITY);

    last_woken_pid = 0ULL;
    assert(pty_arm_read_waiter(master, 77ULL) == PTY_RESULT_OK);
    assert(pty_write(slave, "q", 1U, &transferred) == PTY_RESULT_OK);
    assert(last_woken_pid == 77ULL);
    assert(pty_read(master, bytes, 1U, &transferred) == PTY_RESULT_OK);

    assert(pty_retain(slave) == PTY_RESULT_OK);
    assert(pty_close(slave) == PTY_RESULT_OK);
    assert(pty_poll(master, &poll) == PTY_RESULT_OK && !poll.hup);
    assert(pty_close(slave) == PTY_RESULT_OK);
    assert(pty_poll(master, &poll) == PTY_RESULT_OK && poll.hup);
    assert(pty_read(master, bytes, 1U, &transferred) == PTY_RESULT_HUP);
    assert(pty_write(master, bytes, 1U, &transferred) == PTY_RESULT_HUP);
    assert(pty_close(master) == PTY_RESULT_OK);

    assert(pty_create_pair(&master, &slave) == PTY_RESULT_OK);
    assert(master.generation != stale_master.generation);
    assert(pty_poll(stale_master, &poll) == PTY_RESULT_INVALID);

    assert(pty_create_pair(&master2, &slave2) == PTY_RESULT_OK);
    assert(pty_write(master, "A", 1U, &transferred) == PTY_RESULT_OK);
    assert(pty_write(master2, "B", 1U, &transferred) == PTY_RESULT_OK);
    assert(pty_read(slave, bytes, 1U, &transferred) == PTY_RESULT_OK);
    assert(bytes[0] == (uint8_t)'A');
    assert(pty_read(slave2, bytes, 1U, &transferred) == PTY_RESULT_OK);
    assert(bytes[0] == (uint8_t)'B');

    assert(pty_close(master) == PTY_RESULT_OK);
    assert(pty_close(slave) == PTY_RESULT_OK);
    assert(pty_close(master2) == PTY_RESULT_OK);
    assert(pty_close(slave2) == PTY_RESULT_OK);

    puts("pty host test passed");
    return 0;
}
