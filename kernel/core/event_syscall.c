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

#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
#include <boring/framebuffer_user.h>
#include <boring/m61_runtime_hid.h>
#define M61_RUNTIME_HID_WITNESS(code) \
    boring_m61_runtime_hid_post((uint8_t)(code))
#define M61_POST37_DISPLAY_WITNESS(process, code) \
    do { \
        if (boring_m61_runtime_hid_is_armed() && \
            m61_post37_is_display_process((process))) { \
            boring_m61_post37_witness((uint8_t)(code)); \
        } \
    } while (0)
void boring_m61_note_event_query(struct process *process, uint32_t handle,
                                 long result, uint64_t peer_pid);
#else
#define M61_RUNTIME_HID_WITNESS(code) do { } while (0)
#define M61_POST37_DISPLAY_WITNESS(process, code) \
    do { (void)(process); } while (0)
#endif

#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
static bool m61_post37_is_display_process(const struct process *process) {
    struct boring_framebuffer_user_stats framebuffer;
    return (process != NULL) &&
           boring_framebuffer_user_get_stats(&framebuffer) &&
           framebuffer.claimed && (framebuffer.owner_pid == process->pid);
}

static void m61_post37_classify_ready(
    struct process *process, struct boring_event_watch *watches,
    size_t count, long result) {
    size_t index;

    if ((result <= 0L) || !boring_m61_runtime_hid_is_armed() ||
        !m61_post37_is_display_process(process)) {
        return;
    }
    boring_m61_post37_witness((uint8_t)M61_POST37_FAST_READY);
    for (index = 0U; index < count; ++index) {
        const struct boring_event_watch *watch = &watches[index];
        if (watch->events == 0U) { continue; }
        if (watch->kind == BORING_EVENT_IPC) {
            if ((watch->events & BORING_EVENT_HUP) != 0U) {
                boring_m61_post37_witness(
                    (uint8_t)M61_POST37_READY_IPC_HUP);
            } else if ((watch->events & BORING_EVENT_READ) != 0U) {
                boring_m61_post37_witness((uint8_t)(
                    watch->peer_pid == 0ULL ?
                    M61_POST37_READY_IPC_LISTENER :
                    M61_POST37_READY_IPC_ENDPOINT));
            }
        } else if (watch->kind == BORING_EVENT_INPUT) {
            boring_m61_post37_witness((uint8_t)M61_POST37_READY_INPUT);
        } else if (watch->kind == BORING_EVENT_FD) {
            boring_m61_post37_witness((uint8_t)M61_POST37_READY_FD);
        }
        break;
    }
}
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
    while (done < size) {
        uint64_t physical;
        size_t chunk = (size_t)VMM_PAGE_SIZE -
            (size_t)((address + done) & (VMM_PAGE_SIZE - 1ULL));
        if (chunk > size - done) { chunk = size - done; }
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
        if (chunk > size - done) { chunk = size - done; }
        if (!ring3_user_translate(&process->address_space, address + done,
                                  true, &physical)) {
            return false;
        }
        mapped = (uint8_t *)(uintptr_t)(stats.hhdm_offset + physical);
        for (index = 0U; index < chunk; ++index) {
            if (out) { mapped[index] = bytes[done + index]; }
            else { bytes[done + index] = mapped[index]; }
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
        if (watch->reserved != 0U) { return -(long)BORING_SYSCALL_EINVAL; }
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
        } else { return -(long)BORING_SYSCALL_EINVAL; }
        if (watch->events != 0U) { ++ready; }
    }
    return ready;
}

#if defined(BORING_M54_USB_ONLY_DESKTOP)
#define M61_TRACE_TRB_CYCLE (1U << 0U)

static bool m54_is_input_owner(const struct process *process) {
    struct boring_input_stats input;
    return (process != NULL) && boring_input_get_stats(&input) &&
           input.initialized && input.owned && (input.owner_pid == process->pid);
}

static uint64_t m61_event_dequeue_count(const struct xhci_state *active) {
    return (active == NULL) ? 0ULL : active->event_dequeue_count;
}

static void m61_trace_keyboard_state(const char *prefix) {
    struct boring_input_stats input;
    const struct xhci_state *active = xhci_get_state();
    uint8_t device_index;
    if ((prefix == NULL) || !boring_input_get_stats(&input) || (active == NULL)) {
        return;
    }
    serial_write_string(prefix);
    serial_write_string(" queue/drop/mod=");
    serial_write_u64((uint64_t)input.queued_events);
    serial_write_string("/"); serial_write_u64(input.dropped_events);
    serial_write_string("/"); serial_write_u64((uint64_t)input.modifiers);
    serial_write_string(" consumed="); serial_write_u64(m61_event_dequeue_count(active));
    serial_write_string("\n");
    for (device_index = 0U; device_index < active->addressed_count; ++device_index) {
        const struct xhci_addressed_device *device = &active->addressed[device_index];
        uint8_t endpoint_index;
        for (endpoint_index = 0U; endpoint_index < device->hid_configuration.endpoint_count;
             ++endpoint_index) {
            const struct xhci_hid_endpoint_descriptor *descriptor =
                &device->hid_configuration.endpoints[endpoint_index];
            const struct xhci_hid_endpoint_runtime *runtime =
                &device->hid_runtime[endpoint_index];
            if (descriptor->protocol != 1U) { continue; }
            serial_write_string("m61-hid-trace: kbd slot/ep decoded/press/release out exp=");
            serial_write_u64((uint64_t)device->slot_id); serial_write_string("/");
            serial_write_u64((uint64_t)descriptor->endpoint_id); serial_write_string(" ");
            serial_write_u64((uint64_t)runtime->decoded_reports); serial_write_string("/");
            serial_write_u64((uint64_t)runtime->key_presses); serial_write_string("/");
            serial_write_u64((uint64_t)runtime->key_releases); serial_write_string(" ");
            serial_write_u64(runtime->transfer_outstanding ? 1ULL : 0ULL); serial_write_string(" ");
            serial_write_u64(runtime->expected_trb_physical);
            serial_write_string("\n");
        }
    }
}

static void m61_trace_failed_hid_service(void) {
    static uint64_t failures;
    const struct xhci_state *active = xhci_get_state();
    void *ring_virtual = NULL;
    uint64_t consumed;
    uint16_t index;
    bool cycle;
    volatile struct xhci_trb *ring;
    uint32_t control;
    bool entry_cycle;

    ++failures;
    if ((active == NULL) ||
        !vmm_pmm_frame_to_hhdm(active->event_ring_physical, &ring_virtual)) {
        return;
    }
    consumed = m61_event_dequeue_count(active);
    index = (uint16_t)(consumed % XHCI_EVENT_RING_TRBS);
    cycle = (((consumed / XHCI_EVENT_RING_TRBS) & 1ULL) == 0ULL);
    ring = (volatile struct xhci_trb *)ring_virtual;
    control = ring[index].control;
    entry_cycle = (control & M61_TRACE_TRB_CYCLE) != 0U;
    if ((entry_cycle == cycle) || ((failures & 0x3ffULL) == 0ULL)) {
        serial_write_string("m61-hid-stall: failures/consumed/index/cycle/entry/control/param/status=");
        serial_write_u64(failures); serial_write_string("/");
        serial_write_u64(consumed); serial_write_string("/");
        serial_write_u64((uint64_t)index); serial_write_string("/");
        serial_write_u64(cycle ? 1ULL : 0ULL); serial_write_string("/");
        serial_write_u64(entry_cycle ? 1ULL : 0ULL); serial_write_string("/");
        serial_write_u64((uint64_t)control); serial_write_string("/");
        serial_write_u64(ring[index].parameter); serial_write_string("/");
        serial_write_u64((uint64_t)ring[index].status); serial_write_string("\n");
        m61_trace_keyboard_state("m61-hid-stall-state:");
    }
}
#endif

static bool arm_fd_watches(struct process *process,
                           const struct boring_event_watch *watches, size_t count) {
    size_t index;
    if ((process == NULL) || !process_is_alive(process)) { return false; }
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
                              const struct boring_event_watch *watches, size_t count) {
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
    if (boring_input_get_stats(&input) && input.owned && (input.queued_events != 0U)) {
        (void)task_wake_pid(input.owner_pid);
    }
}

void x86_64_syscall_dispatch_events(struct x86_64_syscall_frame *frame) {
    struct boring_event_watch watches[BORING_EVENT_MAX];
    struct process *process;
    size_t count;
    long result;

    x86_64_syscall_dispatch_m34(frame);
    if ((frame == NULL) || (frame->syscall_number != BORING_SYS_EVENT_WAIT)) { return; }
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
#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
    {
        const bool m61_post37_display =
            boring_m61_runtime_hid_is_armed() &&
            m61_post37_is_display_process(process);
        if (m61_post37_display && (frame->rdx == BORING_EVENT_QUERY)) {
            boring_m61_post37_witness((uint8_t)(
                ((count == 1U) && (watches[0].kind == BORING_EVENT_IPC)) ?
                M61_POST37_DISPLAY_PRESENT_RETURNED :
                M61_POST37_DISPLAY_EVENT_LOOP_REENTRY));
        } else if (m61_post37_display) {
            boring_m61_post37_witness(
                (uint8_t)M61_POST37_EVENT_SYSCALL_ENTRY);
        }
    }
#endif
    for (;;) {
        x86_64_interrupts_disable();
        result = poll_watches(process, watches, count);
#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
        if ((frame->rdx != BORING_EVENT_QUERY) &&
            boring_m61_runtime_hid_is_armed() &&
            m61_post37_is_display_process(process)) {
            if (result > 0L) {
                m61_post37_classify_ready(process, watches, count, result);
            } else if (result < 0L) {
                boring_m61_post37_witness(
                    (uint8_t)M61_POST37_POLL_ERROR);
            }
        }
#endif
        if ((result != 0L) || (frame->rdx == BORING_EVENT_QUERY)) { break; }
#if defined(BORING_M54_USB_ONLY_DESKTOP)
        if (m54_is_input_owner(process)) {
            struct xhci_state usb_state = {0};
            M61_POST37_DISPLAY_WITNESS(
                process, M61_POST37_INPUT_OWNER_TRUE);
            M61_RUNTIME_HID_WITNESS(M61_RUNTIME_HID_POST_A_SERVICE_LOOP);
            const bool serviced = xhci_service_hid_reports(&usb_state);
            static bool m54_initial_service_witness;
            if (!m54_initial_service_witness) {
                const struct xhci_state *active = xhci_get_state();
                uint64_t submitted = 0ULL;
                uint64_t outstanding = 0ULL;
                uint8_t device_index;
                if (active != NULL) {
                    for (device_index = 0U; device_index < active->addressed_count; ++device_index) {
                        const struct xhci_addressed_device *device = &active->addressed[device_index];
                        uint8_t endpoint_index;
                        for (endpoint_index = 0U; endpoint_index < device->hid_configuration.endpoint_count;
                             ++endpoint_index) {
                            submitted += device->hid_runtime[endpoint_index].submitted_transfers;
                            if (device->hid_runtime[endpoint_index].transfer_outstanding) { ++outstanding; }
                        }
                    }
                }
                serial_write_string("m54-desktop: HID service submitted/outstanding=");
                serial_write_u64(submitted); serial_write_string("/"); serial_write_u64(outstanding);
                serial_write_string("\n");
                m54_initial_service_witness = true;
            }
            if (serviced) {
                serial_write_string("m54-desktop: real xHCI HID completion serviced\n");
                m61_trace_keyboard_state("m61-hid-trace:");
            } else {
                m61_trace_failed_hid_service();
            }
            x86_64_interrupts_enable();
            task_yield();

            /*
             * The yield can run a peer that makes any watched object ready.
             * Re-poll with interrupts disabled before deciding to sleep; the
             * pre-yield poll/service result is not a valid sleep predicate.
             */
            x86_64_interrupts_disable();
            result = poll_watches(process, watches, count);
            if (result != 0L) {
                break;
            }
            /*
             * Runtime HID completions are consumed by explicit polling.
             * Interrupter 0 is not enabled, so a Transfer Event cannot wake
             * this HLT path by itself. Keep the wait cooperative and return
             * directly to the next bounded xHCI poll instead of depending on
             * unrelated PIT/PIC delivery for HID liveness.
             */
            continue;
        }
        M61_POST37_DISPLAY_WITNESS(
            process, M61_POST37_INPUT_OWNER_FALSE);
#endif
        if (!arm_fd_watches(process, watches, count)) {
            result = -(long)BORING_SYSCALL_EINVAL;
            break;
        }
        if (!task_block_current()) {
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
#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
    if (!boring_m61_runtime_hid_is_armed() &&
        (result >= 0L) && (frame->rdx == BORING_EVENT_QUERY) &&
        (count == 1U)) {
        boring_m61_note_event_query(process, watches[0].handle, result,
                                    watches[0].peer_pid);
    }
#endif
    frame->result = (uint64_t)(int64_t)result;
}
