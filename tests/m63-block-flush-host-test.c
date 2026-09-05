#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/block_device.h>

struct mock_state {
    uint32_t flushes;
};

static enum block_device_result mock_read(void *context,
                                          uint64_t first,
                                          uint32_t count,
                                          void *buffer) {
    (void)context;
    (void)first;
    (void)count;
    (void)buffer;
    return BLOCK_DEVICE_RESULT_OK;
}

static enum block_device_result mock_write(void *context,
                                           uint64_t first,
                                           uint32_t count,
                                           const void *buffer) {
    (void)context;
    (void)first;
    (void)count;
    (void)buffer;
    return BLOCK_DEVICE_RESULT_OK;
}

static enum block_device_result mock_flush(void *context) {
    struct mock_state *state = (struct mock_state *)context;
    ++state->flushes;
    return BLOCK_DEVICE_RESULT_OK;
}

static void require(bool condition, const char *message) {
    if (!condition) {
        (void)fprintf(stderr, "m63 block flush host test failed: %s\n", message);
        __builtin_trap();
    }
}

int main(void) {
    static const struct block_device_ops flush_ops = {
        .read = mock_read, .write = mock_write, .flush = mock_flush
    };
    static const struct block_device_ops sync_write_ops = {
        .read = mock_read, .write = mock_write, .flush = NULL
    };
    struct mock_state state = { 0U };
    const struct block_device first = {
        .name = "flush0", .logical_block_size = 512U, .block_count = 8ULL,
        .read_only = false, .context = &state, .ops = &flush_ops
    };
    const struct block_device second = {
        .name = "sync0", .logical_block_size = 512U, .block_count = 8ULL,
        .read_only = false, .context = &state, .ops = &sync_write_ops
    };

    block_device_init();
    require(block_device_register(&first) == BLOCK_DEVICE_RESULT_OK,
            "register explicit-flush device");
    require(block_device_register(&second) == BLOCK_DEVICE_RESULT_OK,
            "register synchronous-write device");
    require(block_device_flush(&first) == BLOCK_DEVICE_RESULT_OK &&
            state.flushes == 1U, "single device flush");
    require(block_device_flush_all() == BLOCK_DEVICE_RESULT_OK &&
            state.flushes == 2U, "flush-all invokes explicit durability callback");
    (void)puts("M63 block-device flush seam host tests passed.");
    return 0;
}
