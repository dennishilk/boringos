#!/usr/bin/env python3
from pathlib import Path

p = Path("user/ipc-test/main.c")
text = p.read_text()
marker = "int boring_main(void);"
if marker not in text:
    anchor = "_Static_assert(sizeof(struct test_message) == 16U,\n               \"M33 test message must be fixed-size\");\n\n"
    if text.count(anchor) != 1:
        raise SystemExit("ipc-test prototype anchor mismatch")
    text = text.replace(anchor, anchor + "int boring_main(void);\n\n", 1)
    p.write_text(text)

p = Path("kernel/core/task.c")
text = p.read_text()
if "static bool task_stack_storage_is_valid(" not in text:
    old = '''static bool task_stack_common_is_valid(const struct kernel_task *task,
                                       uintptr_t *base_out,
                                       uintptr_t *end_out) {
    volatile const uint64_t *sentinel;
    uintptr_t base;
    uintptr_t end;

    if ((!task->slot_used) || !task_process_is_valid(task) ||
        (task->stack_base == NULL) ||
        (task->stack_size != (size_t)KERNEL_TASK_STACK_SIZE)) {
        return false;
    }

    base = (uintptr_t)task->stack_base;
    if (task->stack_size > (size_t)(UINTPTR_MAX - base)) {
        return false;
    }
    end = base + (uintptr_t)task->stack_size;

    if ((base & ((uintptr_t)KERNEL_HEAP_ALIGNMENT - 1ULL)) != 0ULL ||
        (end & 0x0fULL) != 0ULL) {
        return false;
    }

    sentinel = (volatile const uint64_t *)task->stack_base;
    if (*sentinel != TASK_STACK_SENTINEL) {
        return false;
    }

    if (base_out != NULL) {
        *base_out = base;
    }
    if (end_out != NULL) {
        *end_out = end;
    }
    return true;
}
'''
    new = '''static bool task_stack_storage_is_valid(const struct kernel_task *task,
                                        uintptr_t *base_out,
                                        uintptr_t *end_out) {
    volatile const uint64_t *sentinel;
    uintptr_t base;
    uintptr_t end;

    if ((task == NULL) || !task->slot_used ||
        (task->stack_base == NULL) ||
        (task->stack_size != (size_t)KERNEL_TASK_STACK_SIZE)) {
        return false;
    }

    base = (uintptr_t)task->stack_base;
    if (task->stack_size > (size_t)(UINTPTR_MAX - base)) {
        return false;
    }
    end = base + (uintptr_t)task->stack_size;

    if ((base & ((uintptr_t)KERNEL_HEAP_ALIGNMENT - 1ULL)) != 0ULL ||
        (end & 0x0fULL) != 0ULL) {
        return false;
    }

    sentinel = (volatile const uint64_t *)task->stack_base;
    if (*sentinel != TASK_STACK_SENTINEL) {
        return false;
    }

    if (base_out != NULL) {
        *base_out = base;
    }
    if (end_out != NULL) {
        *end_out = end;
    }
    return true;
}

static bool task_stack_common_is_valid(const struct kernel_task *task,
                                       uintptr_t *base_out,
                                       uintptr_t *end_out) {
    return task_process_is_valid(task) &&
           task_stack_storage_is_valid(task, base_out, end_out);
}
'''
    if text.count(old) != 1:
        raise SystemExit("task stack validator anchor mismatch")
    text = text.replace(old, new, 1)

old_check = '''            if (tasks[index].slot_used &&
                ((tasks[index].state != KERNEL_TASK_FINISHED) ||
                 !task_stack_is_valid(&tasks[index]))) {'''
new_check = '''            if (tasks[index].slot_used &&
                ((tasks[index].state != KERNEL_TASK_FINISHED) ||
                 !task_stack_storage_is_valid(&tasks[index], NULL, NULL))) {'''
if old_check in text:
    text = text.replace(old_check, new_check, 1)
elif new_check not in text:
    raise SystemExit("finished stack validation anchor mismatch")

old_cleanup = '''        if (tasks[index].slot_used &&
            ((tasks[index].state != KERNEL_TASK_FINISHED) ||
             !task_stack_is_valid(&tasks[index]))) {'''
new_cleanup = '''        if (tasks[index].slot_used &&
            ((tasks[index].state != KERNEL_TASK_FINISHED) ||
             !task_stack_storage_is_valid(&tasks[index], NULL, NULL))) {'''
if old_cleanup in text:
    text = text.replace(old_cleanup, new_cleanup, 1)
elif new_cleanup not in text:
    raise SystemExit("task cleanup validation anchor mismatch")

p.write_text(text)
