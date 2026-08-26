#ifndef BORING_IPC_H
#define BORING_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/user_memory.h>

#define BORING_IPC_SERVICE_NAME_MAX 31U
#define BORING_IPC_GLOBAL_SERVICE_MAX 16U
#define BORING_IPC_HANDLES_PER_PROCESS 32U
#define BORING_IPC_GLOBAL_CONNECTION_MAX 64U
#define BORING_IPC_PENDING_PER_SERVICE 16U
#define BORING_IPC_MESSAGES_PER_DIRECTION 16U
#define BORING_IPC_INLINE_PAYLOAD_MAX 256U
#define BORING_IPC_HANDLE_INVALID 0U

enum boring_ipc_result {
    BORING_IPC_RESULT_OK = 0,
    BORING_IPC_RESULT_INVALID = 1,
    BORING_IPC_RESULT_NOT_FOUND = 2,
    BORING_IPC_RESULT_EXISTS = 3,
    BORING_IPC_RESULT_NO_SPACE = 4,
    BORING_IPC_RESULT_PEER_CLOSED = 5,
    BORING_IPC_RESULT_WOULD_BLOCK = 6,
    BORING_IPC_RESULT_INTERNAL = 7
};

enum boring_ipc_handle_type {
    BORING_IPC_HANDLE_NONE = 0,
    BORING_IPC_HANDLE_LISTENER = 1,
    BORING_IPC_HANDLE_ENDPOINT = 2
};

struct process;

struct boring_ipc_receive_kernel_result {
    size_t payload_length;
    uint32_t buffer_handle;
};

struct boring_ipc_stats {
    uint32_t live_services;
    uint32_t live_connections;
    uint32_t queued_messages;
    uint32_t retained_buffer_attachments;
};

bool boring_ipc_system_init(void);
bool boring_ipc_service_name_valid(const char *name, size_t length);
enum boring_ipc_result boring_ipc_service_register(
    struct process *process,
    const char *name,
    size_t length,
    uint32_t *handle_out);
enum boring_ipc_result boring_ipc_service_connect(
    struct process *process,
    const char *name,
    size_t length,
    uint32_t *handle_out);
enum boring_ipc_result boring_ipc_service_accept(
    struct process *process,
    uint32_t listener_handle,
    uint32_t *endpoint_handle_out);
enum boring_ipc_result boring_ipc_send(
    struct process *process,
    uint32_t endpoint_handle,
    const uint8_t *payload,
    size_t payload_length,
    uint32_t buffer_handle);
enum boring_ipc_result boring_ipc_receive(
    struct process *process,
    uint32_t endpoint_handle,
    uint8_t *payload,
    size_t payload_capacity,
    struct boring_ipc_receive_kernel_result *result_out);
enum boring_ipc_result boring_ipc_close(struct process *process,
                                        uint32_t handle);
void boring_ipc_process_cleanup(struct process *process);
bool boring_ipc_get_stats(struct boring_ipc_stats *stats);

/* Host-test hook resets only when no live IPC object remains. */
bool boring_ipc_host_reset(void);

#endif
