#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, marker: str | None = None) -> None:
    p = Path(path)
    text = p.read_text()
    if marker is not None and marker in text:
        return
    if old not in text:
        raise SystemExit(f"anchor not found: {path}: {old[:100]!r}")
    if text.count(old) != 1:
        raise SystemExit(f"anchor not unique: {path}: {text.count(old)}")
    p.write_text(text.replace(old, new, 1))


# Narrow M32 kernel-internal retain/install bridge. No existing M32 public
# handle/mapping semantics are changed.
USER_MEMORY_ANCHOR = "static uint32_t active_object_count(void) {\n"
USER_MEMORY_INSERT = r'''void user_buffer_retained_ref_clear(struct user_buffer_retained_ref *reference) {
    if (reference == NULL) {
        return;
    }
    reference->object_index = UINT32_MAX;
    reference->active = false;
}

bool user_buffer_retained_ref_active(
    const struct user_buffer_retained_ref *reference) {
    return (reference != NULL) && reference->active &&
           (object_at(reference->object_index) != NULL);
}

enum user_memory_result user_buffer_retain(
    struct process *process,
    uint32_t encoded_handle,
    struct user_buffer_retained_ref *reference_out) {
    struct user_memory_buffer_handle *handle;
    struct user_memory_buffer_object *object;

    if (!process_ready(process)) {
        return USER_MEMORY_RESULT_NOT_INITIALIZED;
    }
    if (reference_out == NULL) {
        return USER_MEMORY_RESULT_INVALID;
    }
    user_buffer_retained_ref_clear(reference_out);
    handle = decode_handle(&process->user_memory, encoded_handle, NULL);
    if (handle == NULL) {
        return USER_MEMORY_RESULT_INVALID;
    }
    object = object_at(handle->object_index);
    if (object == NULL) {
        return USER_MEMORY_RESULT_INTERNAL;
    }
    if (object->reference_count == UINT32_MAX) {
        return USER_MEMORY_RESULT_NO_SPACE;
    }
    ++object->reference_count;
    reference_out->object_index = handle->object_index;
    reference_out->active = true;
    return USER_MEMORY_RESULT_OK;
}

enum user_memory_result user_buffer_release_retained(
    struct user_buffer_retained_ref *reference) {
    uint32_t object_index;

    if (!user_memory_initialized || (reference == NULL) ||
        !user_buffer_retained_ref_active(reference)) {
        return USER_MEMORY_RESULT_INVALID;
    }
    object_index = reference->object_index;
    user_buffer_retained_ref_clear(reference);
    return object_unref(object_index) ? USER_MEMORY_RESULT_OK :
                                        USER_MEMORY_RESULT_INTERNAL;
}

void user_buffer_install_ticket_clear(struct user_buffer_install_ticket *ticket) {
    if (ticket == NULL) {
        return;
    }
    ticket->object_index = UINT32_MAX;
    ticket->generation = 0U;
    ticket->slot = UINT16_MAX;
    ticket->active = false;
}

enum user_memory_result user_buffer_prepare_install(
    struct process *process,
    const struct user_buffer_retained_ref *reference,
    struct user_buffer_install_ticket *ticket_out,
    uint32_t *handle_out) {
    struct user_memory_buffer_handle *slot;
    uint32_t slot_index;
    uint32_t encoded;

    if (!process_ready(process)) {
        return USER_MEMORY_RESULT_NOT_INITIALIZED;
    }
    if ((reference == NULL) || !user_buffer_retained_ref_active(reference) ||
        (ticket_out == NULL) || (handle_out == NULL)) {
        return USER_MEMORY_RESULT_INVALID;
    }
    user_buffer_install_ticket_clear(ticket_out);
    slot = find_handle_slot(&process->user_memory, &slot_index);
    if (slot == NULL) {
        return USER_MEMORY_RESULT_NO_SPACE;
    }
    if ((slot_index > (uint32_t)UINT16_MAX) ||
        !encode_handle(slot_index, slot->generation, &encoded)) {
        return USER_MEMORY_RESULT_INTERNAL;
    }
    ticket_out->object_index = reference->object_index;
    ticket_out->generation = slot->generation;
    ticket_out->slot = (uint16_t)slot_index;
    ticket_out->active = true;
    *handle_out = encoded;
    return USER_MEMORY_RESULT_OK;
}

enum user_memory_result user_buffer_commit_install(
    struct process *process,
    struct user_buffer_retained_ref *reference,
    struct user_buffer_install_ticket *ticket) {
    struct user_memory_buffer_handle *slot;

    if (!process_ready(process)) {
        return USER_MEMORY_RESULT_NOT_INITIALIZED;
    }
    if ((reference == NULL) || !user_buffer_retained_ref_active(reference) ||
        (ticket == NULL) || !ticket->active ||
        (ticket->slot >= (uint16_t)USER_MEMORY_BUFFER_HANDLE_MAX) ||
        (ticket->object_index != reference->object_index) ||
        (object_at(ticket->object_index) == NULL)) {
        return USER_MEMORY_RESULT_INVALID;
    }
    slot = &process->user_memory.handles[ticket->slot];
    if (slot->active || (slot->generation != ticket->generation)) {
        return USER_MEMORY_RESULT_NO_SPACE;
    }
    slot->object_index = ticket->object_index;
    slot->active = true;
    user_buffer_retained_ref_clear(reference);
    user_buffer_install_ticket_clear(ticket);
    return USER_MEMORY_RESULT_OK;
}

static uint32_t active_object_count(void) {
'''
replace_once("kernel/core/user_memory.c", USER_MEMORY_ANCHOR,
             USER_MEMORY_INSERT, "void user_buffer_retained_ref_clear(")

# ABI 31..36, preserving 0..30 byte-for-byte.
replace_once(
    "kernel/include/boring/syscall_abi.h",
    "#define BORING_SYS_BUFFER_CLOSE 30\n",
    "#define BORING_SYS_BUFFER_CLOSE 30\n"
    "#define BORING_SYS_SERVICE_REGISTER 31\n"
    "#define BORING_SYS_SERVICE_CONNECT 32\n"
    "#define BORING_SYS_SERVICE_ACCEPT 33\n"
    "#define BORING_SYS_IPC_SEND 34\n"
    "#define BORING_SYS_IPC_RECEIVE 35\n"
    "#define BORING_SYS_IPC_CLOSE 36\n",
    "#define BORING_SYS_SERVICE_REGISTER 31")
replace_once(
    "kernel/include/boring/syscall_abi.h",
    "#define BORING_BUFFER_HANDLE_INVALID 0U\n",
    "#define BORING_BUFFER_HANDLE_INVALID 0U\n\n"
    "#define BORING_IPC_SERVICE_NAME_MAX 31U\n"
    "#define BORING_IPC_HANDLE_INVALID 0U\n"
    "#define BORING_IPC_NO_ATTACHED_BUFFER BORING_BUFFER_HANDLE_INVALID\n"
    "#define BORING_IPC_INLINE_PAYLOAD_MAX 256U\n"
    "#define BORING_IPC_RECEIVE_FLAGS_NONE 0U\n",
    "#define BORING_IPC_NO_ATTACHED_BUFFER")
replace_once(
    "kernel/include/boring/syscall_abi.h",
    "#define BORING_SYSCALL_ENOMEM 16\n",
    "#define BORING_SYSCALL_ENOMEM 16\n"
    "#define BORING_SYSCALL_EPIPE 17\n",
    "#define BORING_SYSCALL_EPIPE 17")
replace_once(
    "kernel/include/boring/syscall_abi.h",
    "struct boring_dirent {\n",
    "struct boring_ipc_receive_result {\n"
    "    uint64_t payload_length;\n"
    "    uint32_t buffer_handle;\n"
    "    uint32_t flags;\n"
    "};\n\n"
    "struct boring_dirent {\n",
    "struct boring_ipc_receive_result")
replace_once(
    "kernel/include/boring/syscall_abi.h",
    "_Static_assert(sizeof(struct boring_system_info) == 256U,\n",
    "_Static_assert(BORING_SYS_SERVICE_REGISTER == 31,\n"
    "               \"SERVICE_REGISTER syscall number contract changed\");\n"
    "_Static_assert(BORING_SYS_SERVICE_CONNECT == 32,\n"
    "               \"SERVICE_CONNECT syscall number contract changed\");\n"
    "_Static_assert(BORING_SYS_SERVICE_ACCEPT == 33,\n"
    "               \"SERVICE_ACCEPT syscall number contract changed\");\n"
    "_Static_assert(BORING_SYS_IPC_SEND == 34,\n"
    "               \"IPC_SEND syscall number contract changed\");\n"
    "_Static_assert(BORING_SYS_IPC_RECEIVE == 35,\n"
    "               \"IPC_RECEIVE syscall number contract changed\");\n"
    "_Static_assert(BORING_SYS_IPC_CLOSE == 36,\n"
    "               \"IPC_CLOSE syscall number contract changed\");\n"
    "_Static_assert(sizeof(struct boring_ipc_receive_result) == 16U,\n"
    "               \"M33 IPC receive ABI size must remain fixed\");\n"
    "_Static_assert(sizeof(struct boring_system_info) == 256U,\n",
    "M33 IPC receive ABI size must remain fixed")

# Minimal cooperative task block/wake support. Scheduling policy remains the
# established round-robin cooperative selector; BLOCKED tasks are simply not
# eligible until explicitly woken.
replace_once(
    "kernel/include/boring/task.h",
    "    KERNEL_TASK_FINISHED = 2\n",
    "    KERNEL_TASK_FINISHED = 2,\n"
    "    KERNEL_TASK_BLOCKED = 3\n",
    "KERNEL_TASK_BLOCKED = 3")
replace_once(
    "kernel/include/boring/task.h",
    "bool task_create(void (*entry)(void *), void *arg, uint64_t *task_id);\n",
    "bool task_create(void (*entry)(void *), void *arg, uint64_t *task_id);\n"
    "bool task_create_for_process(struct process *process,\n"
    "                             void (*entry)(void *),\n"
    "                             void *arg, uint64_t *task_id);\n",
    "bool task_create_for_process")
replace_once(
    "kernel/include/boring/task.h",
    "void task_yield(void);\n",
    "void task_yield(void);\n"
    "bool task_block_current(void);\n"
    "bool task_wake_process(struct process *process);\n"
    "void task_exit_current_process(void) __attribute__((noreturn));\n",
    "bool task_block_current")
replace_once(
    "kernel/core/task.c",
    "#include <boring/irq.h>\n",
    "#include <boring/irq.h>\n#include <boring/ipc_syscall.h>\n",
    "#include <boring/ipc_syscall.h>")
replace_once(
    "kernel/core/task.c",
    "static struct kernel_task *task_select_next_cooperative(\n",
    r'''static void task_select_syscall_stack(const struct kernel_task *task) {
    if ((task != NULL) && (task != &bootstrap_task) && task->slot_used &&
        (task->stack_base != NULL) &&
        (task->stack_size == (size_t)KERNEL_TASK_STACK_SIZE)) {
        const uintptr_t base = (uintptr_t)task->stack_base;
        boring_ipc_syscall_use_task_stack(
            base + (uintptr_t)task->stack_size);
    } else {
        boring_ipc_syscall_use_bootstrap_stack();
    }
}

static struct kernel_task *task_select_next_cooperative(
''',
    "static void task_select_syscall_stack")
replace_once(
    "kernel/core/task.c",
    "bool task_create_preemptive(void (*entry)(void *), void *arg,\n",
    r'''bool task_create_for_process(struct process *process,
                             void (*entry)(void *),
                             void *arg, uint64_t *task_id) {
    return task_create_internal(process, entry, arg, task_id,
                                KERNEL_TASK_CONTEXT_COOPERATIVE);
}

bool task_create_preemptive(void (*entry)(void *), void *arg,
''',
    "bool task_create_for_process")
# Every cooperative switch selects the target task's trusted SYSCALL stack.
replace_once(
    "kernel/core/task.c",
    "    if (!process_activate(next->process)) {\n        from->state = KERNEL_TASK_RUNNING;\n",
    "    task_select_syscall_stack(next);\n"
    "    if (!process_activate(next->process)) {\n"
    "        task_select_syscall_stack(from);\n"
    "        from->state = KERNEL_TASK_RUNNING;\n",
    "task_select_syscall_stack(next);\n    if (!process_activate(next->process)) {\n        task_select_syscall_stack(from);")
replace_once(
    "kernel/core/task.c",
    "uint64_t task_current_id(void) {\n",
    r'''bool task_block_current(void) {
    struct kernel_task *from;
    struct kernel_task *next;

    if ((!task_initialized) || preemption_enabled ||
        !task_is_regular(current_task) ||
        (current_task->context_kind != KERNEL_TASK_CONTEXT_COOPERATIVE) ||
        (current_task->state != KERNEL_TASK_RUNNING)) {
        return false;
    }
    current_task->interrupts_enabled = x86_64_interrupts_enabled();
    x86_64_interrupts_disable();
    from = current_task;
    from->state = KERNEL_TASK_BLOCKED;
    next = task_select_next_cooperative(from);
    if (next == NULL) {
        from->state = KERNEL_TASK_RUNNING;
        task_restore_interrupt_state();
        return false;
    }
    task_select_syscall_stack(next);
    if (!process_activate(next->process)) {
        task_select_syscall_stack(from);
        from->state = KERNEL_TASK_RUNNING;
        task_restore_interrupt_state();
        return false;
    }
    next->state = KERNEL_TASK_RUNNING;
    current_task = next;
    ++cooperative_context_switch_count;
    x86_64_context_switch(&from->context, &next->context);
    task_restore_interrupt_state();
    return true;
}

bool task_wake_process(struct process *process) {
    size_t index;
    bool woke = false;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!task_initialized) || (process == NULL) || !process_is_alive(process)) {
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
        return false;
    }
    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        if (tasks[index].slot_used && (tasks[index].process == process) &&
            (tasks[index].context_kind == KERNEL_TASK_CONTEXT_COOPERATIVE) &&
            (tasks[index].state == KERNEL_TASK_BLOCKED)) {
            tasks[index].state = KERNEL_TASK_READY;
            woke = true;
        }
    }
    if (interrupts_were_enabled) {
        x86_64_interrupts_enable();
    }
    return woke;
}

void task_exit_current_process(void) {
    struct kernel_task *from;
    struct kernel_task *next;
    struct process *finished_process;

    if ((!task_initialized) || preemption_enabled ||
        !task_is_regular(current_task) ||
        (current_task->context_kind != KERNEL_TASK_CONTEXT_COOPERATIVE) ||
        (current_task->state != KERNEL_TASK_RUNNING) ||
        !task_process_is_valid(current_task)) {
        x86_64_halt_forever();
    }
    x86_64_interrupts_disable();
    from = current_task;
    finished_process = from->process;
    from->state = KERNEL_TASK_FINISHED;
    ++finished_task_count;
    next = task_select_next_cooperative(from);
    if (next == NULL) {
        next = &bootstrap_task;
    }
    task_select_syscall_stack(next);
    if (!task_process_is_valid(next) || !process_activate(next->process) ||
        !process_mark_finished(finished_process)) {
        x86_64_halt_forever();
    }
    next->state = KERNEL_TASK_RUNNING;
    current_task = next;
    ++cooperative_context_switch_count;
    x86_64_context_switch(&from->context, &next->context);
    x86_64_halt_forever();
}

uint64_t task_current_id(void) {
''',
    "bool task_block_current(void)")
# task_finish_current's normal cooperative switch also has to retarget SYSCALL.
replace_once(
    "kernel/core/task.c",
    "    if (!task_process_is_valid(next) || !process_activate(next->process)) {\n        x86_64_halt_forever();\n    }\n\n    next->state = KERNEL_TASK_RUNNING;\n",
    "    task_select_syscall_stack(next);\n"
    "    if (!task_process_is_valid(next) || !process_activate(next->process)) {\n"
    "        x86_64_halt_forever();\n"
    "    }\n\n"
    "    next->state = KERNEL_TASK_RUNNING;\n",
    "task_select_syscall_stack(next);\n    if (!task_process_is_valid(next) || !process_activate(next->process))")

# Dynamic trusted stack selection. The global scratch is only an entry handoff:
# each frame owns its user RSP before a blocking dispatcher can switch tasks.
replace_once(
    "kernel/arch/x86_64/syscall_entry.S",
    ".extern x86_64_syscall_dispatch\n",
    ".extern x86_64_syscall_dispatch_m33\n"
    ".extern x86_64_syscall_active_stack_top\n",
    ".extern x86_64_syscall_dispatch_m33")
replace_once(
    "kernel/arch/x86_64/syscall_entry.S",
    "    leaq x86_64_syscall_stack+16384(%rip), %rsp\n",
    "    movq x86_64_syscall_active_stack_top(%rip), %rsp\n"
    "    testq %rsp, %rsp\n"
    "    jnz .Lm33_stack_ready\n"
    "    leaq x86_64_syscall_stack+16384(%rip), %rsp\n"
    ".Lm33_stack_ready:\n",
    ".Lm33_stack_ready:")
replace_once(
    "kernel/arch/x86_64/syscall_entry.S",
    "    call x86_64_syscall_dispatch\n",
    "    call x86_64_syscall_dispatch_m33\n",
    "call x86_64_syscall_dispatch_m33")

print("M33 integration patch applied")
