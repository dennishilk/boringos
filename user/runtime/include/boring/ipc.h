#ifndef BORING_USER_IPC_H
#define BORING_USER_IPC_H

#include <stddef.h>
#include <stdint.h>

#include <boring/syscall_abi.h>

long boring_service_register(const char *name, size_t length);
long boring_service_connect(const char *name, size_t length);
long boring_service_accept(uint32_t listener_handle);
long boring_ipc_send(uint32_t endpoint_handle,
                     const void *payload,
                     size_t payload_length,
                     uint32_t buffer_handle);
long boring_ipc_receive(uint32_t endpoint_handle,
                        void *payload,
                        size_t payload_capacity,
                        struct boring_ipc_receive_result *result);
long boring_ipc_close(uint32_t handle);

#endif
