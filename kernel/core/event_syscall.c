#include <stddef.h>
#include <stdint.h>
#include <boring/cpu.h>
#include <boring/display_syscall.h>
#include <boring/fd.h>
#include <boring/event_abi.h>
#include <boring/event_syscall.h>
#include <boring/input.h>
#include <boring/ipc.h>
#include <boring/process.h>
#include <boring/ring3_memory.h>
#if defined(BORING_M54_USB_ONLY_DESKTOP)
#include <boring/serial.h>
#endif
#include <boring/syscall.h>
#include <boring/syscall_abi.h>
#include <boring/task.h>
#include <boring/vmm.h>
#if defined(BORING_M54_USB_ONLY_DESKTOP)
#include <boring/xhci.h>
#endif

/* No consumption, allocation or authority transfer occurs during a wait. */
static bool user_copy(uintptr_t address, void *buffer, size_t size, bool out) {
    struct process *process = process_current();
    struct vmm_stats stats;
    uint8_t *bytes = buffer;
    size_t done = 0U;

    if ((process == NULL) || !process_is_alive(process) ||
        process->address_space.bootstrap ||
        !ring3_user_range_valid(address, size) || !vmm_get_stats(&stats)) {
        return false;
    }
    /* Validate the whole writable range before any output is written. */
    while (done < size) {
        uint64_t physical;
        size_t chunk = (size_t)VMM_PAGE_SIZE -
            (size_t)((address + done) & (VMM_PAGE_SIZE - 1ULL));
        if (chunk > size - done) {
            chunk = size - done;
        }
        if (!ring3_user_translate(&process->address_space, address + done,
                                  true, &physical) ||
            (physical > UINT64_MAX - stats.hhdm_offset)) {
            return false;
        }
        done += chunk;
    }
    done = 0U;
    while (done < size) {
        uint64_t physical;
        size_t index;
        size_t chunk = (size_t)VMM_PAGE_SIZE -
            (size_t)((address + done) & (VMM_PAGE_SIZE - 1ULL));
        uint8_t *mapped;
        if (chunk > size - done) {
            chunk = size - done;
        }
        if (!ring3_user_translate(&process->address_space, address + done,
                                  true, &physical)) {
            return false;
        }
        mapped = (uint8_t *)(uintptr_t)(stats.hhdm_offset + physical);
        for (index = 0U; index < chunk; ++index) {
            if (out) {
                mapped[index] = bytes[done + index];
            } else {
                bytes[done + index] = mapped[index];
            }
        }
        done += chunk;
    }
    return true;
}

static long poll_watches(struct process *process,
                         struct boring_event_watch *watches, size_t count) {
    size_t index;
    long ready = 0L;
    for (index = 0U; index < count; ++index) {
        struct boring_event_watch *watch = &watches[index];
        watch->events = 0U;
        watch->peer_pid = 0ULL;
        if (watch->reserved != 0U) {
            return -(long)BORING_SYSCALL_EINVAL;
        }
        if (watch->kind == BORING_EVENT_IPC) {
            if (boring_ipc_poll(process, watch->handle, &watch->events,
                                &watch->peer_pid) != BORING_IPC_RESULT_OK) {
                return -(long)BORING_SYSCALL_EINVAL;
            }
        } else if ((watch->kind == BORING_EVENT_INPUT) && (watch->handle == 0U)) {
            struct boring_input_stats input;
            if (!boring_input_get_stats(&input) || !input.initialized) {
                return -(long)BORING_SYSCALL_EINVAL;
            }
            if (!input.owned || (input.owner_pid != process->pid)) {
                return -(long)BORING_SYSCALL_EACCES;
            }
            watch->events = (input.queued_events != 0U) ? BORING_EVENT_READ : 0U;
        } else if (watch->kind == BORING_EVENT_FD) {
            enum kernel_fd_kind kind;
            uint32_t access;
            struct pty_poll_state pty_state;
            if (!kernel_fd_describe(&process->fd_table, watch->handle, &kind, &access) ||
                (kind != KERNEL_FD_PTY) || ((access & VFS_ACCESS_READ) == 0U) ||
                (kernel_fd_poll_pty(&process->fd_table, watch->handle, &pty_state) != PTY_RESULT_OK)) {
                return -(long)BORING_SYSCALL_EINVAL;
            }
            if (pty_state.readable) { watch->events |= BORING_EVENT_READ; }
            if (pty_state.hup) { watch->events |= BORING_EVENT_HUP; }
        } else {
            return -(long)BORING_SYSCALL_EINVAL;
        }
        if (watch->events != 0U) {
            ++ready;
        }
    }
    return ready;
}

#if defined(BORING_M54_USB_ONLY_DESKTOP)
static bool m54_is_input_owner(const struct process *process) {
    struct boring_input_stats input;
    return (process != NULL) && boring_input_get_stats(&input) &&
           input.initialized && input.owned &&
           (input.owner_pid == process->pid);
}

static void m61_trace_serviced_keyboard_state(void) {
    struct boring_input_stats input;
    const struct xhci_state *active;
    uint8_t device_index;

    if (!boring_input_get_stats(&input)) { return; }
    serial_write_string("m61-hid-trace: queue/drop/mod=");
    serial_write_u64((uint64_t)input.queued_events);
    serial_write_string("/");
    serial_write_u64(input.dropped_events);
    serial_write_string("/");
    serial_write_u64((uint64_t)input.modifiers);
    serial_write_string("\n");
    active = xhci_get_state();
    if (active == NULL) { return; }
    for (device_index = 0U; device_index < active->addressed_count;
         ++device_index) {
        const struct xhci_addressed_device *device = &active->addressed[device_index];
        uint8_t endpoint_index;
        for (endpoint_index = 0U;
             endpoint_index < device->hid_configuration.endpoint_count;
             ++endpoint_index) {
            const struct xhci_hid_endpoint_descriptor *descriptor =
                &device->hid_configuration.endpoints[endpoint_index];
            const struct xhci_hid_endpoint_runtime *runtime =
                &device->hid_runtime[endpoint_index];
            uint8_t key_index;
            if (descriptor->protocol != 1U) { continue; }
            serial_write_string("m61-hid-trace: keyboard slot/ep decoded/press/release last/down state-mod keys=");
            serial_write_u64((uint64_t)device->slot_id);
            serial_write_string("/");
            serial_write_u64((uint64_t)descriptor->endpoint_id);
            serial_write_string(" ");
            serial_write_u64((uint64_t)runtime->decoded_reports);
            serial_write_string("/");
            serial_write_u64((uint64_t)runtime->key_presses);
            serial_write_string("/");
            serial_write_u64((uint64_t)runtime->key_releases);
            serial_write_string(" ");
            serial_write_u64((uint64_t)runtime->last_key_usage);
            serial_write_string("/");
            serial_write_u64(runtime->last_key_down ? 1ULL : 0ULL);
            serial_write_string(" ");
            serial_write_u64((uint64_t)runtime->keyboard_state.modifiers);
            serial_write_string(" ");
            for (key_index = 0U; key_index < USB_HID_BOOT_KEYS; ++key_index) {
                serial_write_u64((uint64_t)runtime->keyboard_state.keys[key_index]);
                if (key_index + 1U != USB_HID_BOOT_KEYS) {
                    serial_write_string(",");
                }
            }
            serial_write_string("\n");
        }
    }
}
#endif

static bool arm_fd_watches(struct process *process,
                           const struct boring_event_watch *watches,
                           size_t count) {
    size_t index;
    if ((process == NULL) || !process_is_alive(process)) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        if ((watches[index].kind == BORING_EVENT_FD) &&
            (kernel_fd_arm_pty_waiter(&process->fd_table, watches[index].handle,
                                      process->pid) != PTY_RESULT_OK)) {
            return false;
        }
    }
    return true;
}

static void cancel_fd_watches(struct process *process,
                              const struct boring_event_watch *watches,
                              size_t count) {
    size_t index;
    if (process == NULL) { return; }
    for (index = 0U; index < count; ++index) {
        if (watches[index].kind == BORING_EVENT_FD) {
            kernel_fd_cancel_pty_waiter(&process->fd_table, watches[index].handle,
                                        process->pid);
        }
    }
}

void boring_event_input_irq(void) {
    struct boring_input_stats input;
    if (boring_input_get_stats(&input) && input.owned &&
        (input.queued_events != 0U)) {
        (void)task_wake_pid(input.owner_pid);
    }
}

void x86_64_syscall_dispatch_events(struct x86_64_syscall_frame *frame) {
    struct boring_event_watch watches[BORING_EVENT_MAX];
    struct process *process;
    size_t count;
    long result;

    /* Retain all established trusted-stack/SYSRET checks and slots 0..40. */
    x86_64_syscall_dispatch_m34(frame);
    if ((frame == NULL) || (frame->syscall_number != BORING_SYS_EVENT_WAIT)) {
        return;
    }
    if ((frame->rsi == 0ULL) || (frame->rsi > BORING_EVENT_MAX) ||
        ((frame->rdx & ~(uint64_t)BORING_EVENT_QUERY) != 0ULL)) {
        frame->result = (uint64_t)(-(int64_t)BORING_SYSCALL_EINVAL);
        return;
    }
    count = (size_t)frame->rsi;
    if (!user_copy((uintptr_t)frame->rdi, watches, count * sizeof(watches[0]), false)) {
        frame->result = (uint64_t)(-(int64_t)BORING_SYSCALL_EFAULT);
        return;
    }
    process = process_current();
    for (;;) {
        x86_64_interrupts_disable();
        result = poll_watches(process, watches, count);
        if ((result != 0L) || (frame->rdx == BORING_EVENT_QUERY)) {
            break;
        }
#if defined(BORING_M54_USB_ONLY_DESKTOP)
        if (m54_is_input_owner(process)) {
            struct xhci_state usb_state = {0};
            const bool serviced = xhci_service_hid_reports(&usb_state);
            static bool m54_initial_service_witness = false;
            if (!m54_initial_service_witness) {
                const struct xhci_state *active = xhci_get_state();
                uint64_t submitted = 0ULL;
                uint64_t outstanding = 0ULL;
                uint8_t device_index;
                if (active != NULL) {
                    for (device_index = 0U; device_index < active->addressed_count;
                         ++device_index) {
                        const struct xhci_addressed_device *device =
                            &active->addressed[device_index];
                        uint8_t endpoint_index;
                        for (endpoint_index = 0U;
                             endpoint_index < device->hid_configuration.endpoint_count;
                             ++endpoint_index) {
                            submitted += device->hid_runtime[endpoint_index].submitted_transfers;
                            if (device->hid_runtime[endpoint_index].transfer_outstanding) {
                                ++outstanding;
                            }
                        }
                    }
                }
                serial_write_string("m54-desktop: HID service submitted/outstanding=");
                serial_write_u64(submitted);
                serial_write_string("/");
                serial_write_u64(outstanding);
                serial_write_string("\n");
                m54_initial_service_witness = true;
            }
            if (serviced) {
                serial_write_string("m54-desktop: real xHCI HID completion serviced\n");
                m61_trace_serviced_keyboard_state();
            }
            x86_64_interrupts_enable();
            task_yield();
            if (!serviced) {
                /*
                 * The controller/device model needs host time between bounded
                 * observations. Sleep only until the already-established PIT
                 * interrupt, then retry on the next cooperative pass.
                 */
                x86_64_enable_and_halt();
                x86_64_interrupts_disable();
            }
            continue;
        }
#endif
        if (!arm_fd_watches(process, watches, count)) {
            result = -(long)BORING_SYSCALL_EINVAL;
            break;
        }
        if (!task_block_current()) {
            /* Last runnable task: sleep until hardware wakes any waiter. */
            x86_64_enable_and_halt();
            x86_64_interrupts_disable();
            task_yield();
        }
        cancel_fd_watches(process, watches, count);
    }
    cancel_fd_watches(process, watches, count);
    if ((result >= 0L) &&
        !user_copy((uintptr_t)frame->rdi, watches, count * sizeof(watches[0]), true)) {
        result = -(long)BORING_SYSCALL_EFAULT;
    }
    frame->result = (uint64_t)(int64_t)result;
}
