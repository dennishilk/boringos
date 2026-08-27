#ifndef BORING_PTY_H
#define BORING_PTY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KERNEL_PTY_MAX 8U
#define KERNEL_PTY_RING_CAPACITY 4096U
#define KERNEL_PTY_ENDPOINT_MASTER 0U
#define KERNEL_PTY_ENDPOINT_SLAVE 1U

struct pty_handle {
    uint16_t slot;
    uint16_t endpoint;
    uint32_t generation;
};

enum pty_result {
    PTY_RESULT_OK = 0,
    PTY_RESULT_INVALID,
    PTY_RESULT_NO_SPACE,
    PTY_RESULT_WOULD_BLOCK,
    PTY_RESULT_HUP
};

struct pty_poll_state {
    bool readable;
    bool hup;
};

struct pty_stats {
    uint32_t active_pairs;
    uint32_t read_waiters;
    uint64_t references;
    size_t queued_bytes;
};

bool pty_init(void);
bool pty_get_stats(struct pty_stats *stats);
enum pty_result pty_create_pair(struct pty_handle *master_out,
                                struct pty_handle *slave_out);
enum pty_result pty_retain(struct pty_handle handle);
enum pty_result pty_close(struct pty_handle handle);
enum pty_result pty_read(struct pty_handle handle, void *buffer, size_t length,
                         size_t *transferred_out);
enum pty_result pty_write(struct pty_handle handle, const void *buffer,
                          size_t length, size_t *transferred_out);
enum pty_result pty_poll(struct pty_handle handle,
                         struct pty_poll_state *state_out);
enum pty_result pty_arm_read_waiter(struct pty_handle handle, uint64_t pid);
void pty_cancel_read_waiter(struct pty_handle handle, uint64_t pid);

#endif
