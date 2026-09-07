#include <stddef.h>
#include <stdint.h>

#include <boring/ipc.h>
#include <boring/syscall_abi.h>

long boring_service_register(const char *name, size_t length) {
    long result;
    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_SERVICE_REGISTER), "D"(name), "S"(length)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_service_connect(const char *name, size_t length) {
    long result;
    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_SERVICE_CONNECT), "D"(name), "S"(length)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_service_accept(uint32_t listener_handle) {
    long result;
    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_SERVICE_ACCEPT),
          "D"((uint64_t)listener_handle)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_ipc_send(uint32_t endpoint_handle,
                     const void *payload,
                     size_t payload_length,
                     uint32_t buffer_handle) {
    long result;
    register uint64_t buffer_argument __asm__("r10") = (uint64_t)buffer_handle;
    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_IPC_SEND),
          "D"((uint64_t)endpoint_handle), "S"(payload), "d"(payload_length),
          "r"(buffer_argument)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_ipc_receive(uint32_t endpoint_handle,
                        void *payload,
                        size_t payload_capacity,
                        struct boring_ipc_receive_result *result_out) {
    long result;
    register struct boring_ipc_receive_result *result_argument __asm__("r10") =
        result_out;
    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_IPC_RECEIVE),
          "D"((uint64_t)endpoint_handle), "S"(payload), "d"(payload_capacity),
          "r"(result_argument)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_ipc_close(uint32_t handle) {
    long result;
    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_IPC_CLOSE), "D"((uint64_t)handle)
        : "rcx", "r11", "cc", "memory");
    return result;
}
