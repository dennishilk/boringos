#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/context.h>
#include <boring/cpu.h>
#include <boring/heap.h>
#include <boring/task.h>

#define TASK_STACK_SENTINEL 0x424f52494e475354ULL
#define TASK_STACK_RESERVED_BYTES 16U

struct kernel_task {
    uint64_t id;
    enum kernel_task_state state;
    struct x86_64_kernel_context context;
    void *stack_base;
    size_t stack_size;
    void (*entry)(void *);
    void *arg;
    bool slot_used;
    bool interrupts_enabled;
};

static struct kernel_task bootstrap_task;
static struct kernel_task tasks[KERNEL_TASK_MAX];
static struct kernel_task *current_task;
static bool task_initialized;
static uint64_t next_task_id;
static uint64_t created_task_count;
static uint64_t finished_task_count;
static uint64_t context_switch_count;

static void kernel_task_trampoline(void) __attribute__((noreturn));
static void task_finish_current(void) __attribute__((noreturn));

static void context_clear(struct x86_64_kernel_context *context) {
    context->rsp = 0ULL;
    context->rbx = 0ULL;
    context->rbp = 0ULL;
    context->r12 = 0ULL;
    context->r13 = 0ULL;
    context->r14 = 0ULL;
    context->r15 = 0ULL;
}

static void task_clear(struct kernel_task *task) {
    task->id = 0ULL;
    task->state = KERNEL_TASK_FINISHED;
    context_clear(&task->context);
    task->stack_base = NULL;
    task->stack_size = 0U;
    task->entry = NULL;
    task->arg = NULL;
    task->slot_used = false;
    task->interrupts_enabled = false;
}

static bool task_is_regular(const struct kernel_task *task) {
    size_t index;

    if (task == NULL) {
        return false;
    }

    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        if (task == &tasks[index]) {
            return tasks[index].slot_used;
        }
    }

    return false;
}

static bool task_stack_ranges_overlap(uintptr_t first_base,
                                      size_t first_size,
                                      uintptr_t second_base,
                                      size_t second_size) {
    uintptr_t first_end;
    uintptr_t second_end;

    if ((first_size > (size_t)(UINTPTR_MAX - first_base)) ||
        (second_size > (size_t)(UINTPTR_MAX - second_base))) {
        return true;
    }

    first_end = first_base + (uintptr_t)first_size;
    second_end = second_base + (uintptr_t)second_size;
    return (first_base < second_end) && (second_base < first_end);
}

static bool task_stack_is_valid(const struct kernel_task *task) {
    volatile const uint64_t *sentinel;
    const uintptr_t base = (uintptr_t)task->stack_base;
    uintptr_t end;

    if ((!task->slot_used) || (task->stack_base == NULL) ||
        (task->stack_size != (size_t)KERNEL_TASK_STACK_SIZE) ||
        (task->stack_size > (size_t)(UINTPTR_MAX - base))) {
        return false;
    }

    end = base + (uintptr_t)task->stack_size;
    if ((base & ((uintptr_t)KERNEL_HEAP_ALIGNMENT - 1ULL)) != 0ULL ||
        (end & 0x0fULL) != 0ULL ||
        (task->context.rsp < (uint64_t)(base + TASK_STACK_RESERVED_BYTES)) ||
        (task->context.rsp >= (uint64_t)end) ||
        ((task->context.rsp & 0x07ULL) != 0ULL)) {
        return false;
    }

    sentinel = (volatile const uint64_t *)task->stack_base;
    return *sentinel == TASK_STACK_SENTINEL;
}

static void task_restore_interrupt_state(void) {
    if ((current_task != NULL) && current_task->interrupts_enabled) {
        x86_64_interrupts_enable();
    } else {
        x86_64_interrupts_disable();
    }
}

static struct kernel_task *task_select_next(const struct kernel_task *from) {
    size_t start = 0U;
    size_t offset;

    if ((from != NULL) && (from != &bootstrap_task)) {
        size_t index;

        for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
            if (from == &tasks[index]) {
                start = (index + 1U) % (size_t)KERNEL_TASK_MAX;
                break;
            }
        }
    }

    for (offset = 0U; offset < (size_t)KERNEL_TASK_MAX; ++offset) {
        const size_t index =
            (start + offset) % (size_t)KERNEL_TASK_MAX;

        if (tasks[index].slot_used &&
            (tasks[index].state == KERNEL_TASK_READY) &&
            (&tasks[index] != from)) {
            return &tasks[index];
        }
    }

    return NULL;
}

bool task_init(void) {
    size_t index;

    if (task_initialized) {
        return false;
    }

    task_clear(&bootstrap_task);
    bootstrap_task.id = KERNEL_BOOTSTRAP_TASK_ID;
    bootstrap_task.state = KERNEL_TASK_RUNNING;
    bootstrap_task.slot_used = true;
    bootstrap_task.interrupts_enabled = x86_64_interrupts_enabled();

    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        task_clear(&tasks[index]);
    }

    current_task = &bootstrap_task;
    next_task_id = 1ULL;
    created_task_count = 0ULL;
    finished_task_count = 0ULL;
    context_switch_count = 0ULL;
    task_initialized = true;
    return true;
}

bool task_create(void (*entry)(void *), void *arg, uint64_t *task_id) {
    struct kernel_task *task = NULL;
    void *stack;
    uintptr_t stack_base;
    uintptr_t stack_top;
    uintptr_t initial_rsp;
    uint64_t *initial_frame;
    size_t index;

    if ((!task_initialized) || (entry == NULL) || (task_id == NULL) ||
        (next_task_id == UINT64_MAX)) {
        return false;
    }

    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        if (!tasks[index].slot_used) {
            task = &tasks[index];
            break;
        }
    }
    if (task == NULL) {
        return false;
    }

    stack = kmalloc((size_t)KERNEL_TASK_STACK_SIZE);
    if (stack == NULL) {
        return false;
    }

    stack_base = (uintptr_t)stack;
    if (((stack_base & ((uintptr_t)KERNEL_HEAP_ALIGNMENT - 1ULL)) != 0ULL) ||
        ((size_t)KERNEL_TASK_STACK_SIZE >
         (size_t)(UINTPTR_MAX - stack_base))) {
        (void)kfree(stack);
        return false;
    }
    stack_top = stack_base + (uintptr_t)KERNEL_TASK_STACK_SIZE;

    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        if (tasks[index].slot_used &&
            task_stack_ranges_overlap(stack_base,
                                      (size_t)KERNEL_TASK_STACK_SIZE,
                                      (uintptr_t)tasks[index].stack_base,
                                      tasks[index].stack_size)) {
            (void)kfree(stack);
            return false;
        }
    }

    *(volatile uint64_t *)stack = TASK_STACK_SENTINEL;
    initial_rsp = stack_top - 16ULL;
    initial_frame = (uint64_t *)initial_rsp;
    initial_frame[0] = (uint64_t)(uintptr_t)&kernel_task_trampoline;
    initial_frame[1] = 0ULL;

    task_clear(task);
    task->id = next_task_id;
    ++next_task_id;
    task->state = KERNEL_TASK_READY;
    task->context.rsp = (uint64_t)initial_rsp;
    task->stack_base = stack;
    task->stack_size = (size_t)KERNEL_TASK_STACK_SIZE;
    task->entry = entry;
    task->arg = arg;
    task->slot_used = true;
    task->interrupts_enabled = x86_64_interrupts_enabled();

    if (!task_stack_is_valid(task)) {
        task_clear(task);
        (void)kfree(stack);
        return false;
    }

    *task_id = task->id;
    ++created_task_count;
    return true;
}

void task_yield(void) {
    struct kernel_task *from;
    struct kernel_task *next;

    if ((!task_initialized) || (current_task == NULL)) {
        return;
    }

    current_task->interrupts_enabled = x86_64_interrupts_enabled();
    x86_64_interrupts_disable();

    from = current_task;
    if (from->state == KERNEL_TASK_RUNNING) {
        from->state = KERNEL_TASK_READY;
    }

    next = task_select_next(from);
    if (next == NULL) {
        from->state = KERNEL_TASK_RUNNING;
        current_task = from;
        task_restore_interrupt_state();
        return;
    }

    next->state = KERNEL_TASK_RUNNING;
    current_task = next;
    ++context_switch_count;
    x86_64_context_switch(&from->context, &next->context);

    task_restore_interrupt_state();
}

uint64_t task_current_id(void) {
    if ((!task_initialized) || (current_task == NULL)) {
        return UINT64_MAX;
    }

    return current_task->id;
}

bool task_current_stack_contains(const void *address) {
    uintptr_t value;
    uintptr_t base;
    uintptr_t low;
    uintptr_t end;

    if ((!task_initialized) || (address == NULL) ||
        !task_is_regular(current_task) ||
        (current_task->stack_base == NULL)) {
        return false;
    }

    base = (uintptr_t)current_task->stack_base;
    if (current_task->stack_size > (size_t)(UINTPTR_MAX - base)) {
        return false;
    }
    end = base + (uintptr_t)current_task->stack_size;
    low = base + TASK_STACK_RESERVED_BYTES;
    value = (uintptr_t)address;
    return (value >= low) && (value < end);
}

bool task_get_stats(struct task_stats *stats) {
    uint64_t active = 0ULL;
    size_t index;

    if ((!task_initialized) || (stats == NULL) || (current_task == NULL)) {
        return false;
    }

    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        if (tasks[index].slot_used) {
            ++active;
        }
    }

    stats->created_tasks = created_task_count;
    stats->finished_tasks = finished_task_count;
    stats->context_switches = context_switch_count;
    stats->active_tasks = active;
    stats->current_task_id = current_task->id;
    stats->stack_size = (size_t)KERNEL_TASK_STACK_SIZE;
    return true;
}

bool task_cleanup_finished(uint64_t *freed_stacks) {
    uint64_t freed = 0ULL;
    size_t index;

    if ((!task_initialized) || (freed_stacks == NULL) ||
        (current_task != &bootstrap_task) ||
        (bootstrap_task.state != KERNEL_TASK_RUNNING)) {
        return false;
    }

    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        if (tasks[index].slot_used &&
            ((tasks[index].state != KERNEL_TASK_FINISHED) ||
             !task_stack_is_valid(&tasks[index]))) {
            return false;
        }
    }

    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        if (tasks[index].slot_used) {
            void *const stack = tasks[index].stack_base;

            if (!kfree(stack)) {
                return false;
            }
            task_clear(&tasks[index]);
            ++freed;
        }
    }

    *freed_stacks = freed;
    return true;
}

static void kernel_task_trampoline(void) {
    struct kernel_task *task = current_task;

    if ((!task_initialized) || !task_is_regular(task) ||
        (task->state != KERNEL_TASK_RUNNING) || (task->entry == NULL)) {
        x86_64_halt_forever();
    }

    task_restore_interrupt_state();
    task->entry(task->arg);
    task_finish_current();
}

static void task_finish_current(void) {
    struct kernel_task *from;
    struct kernel_task *next;

    if ((!task_initialized) || !task_is_regular(current_task) ||
        (current_task->state != KERNEL_TASK_RUNNING)) {
        x86_64_halt_forever();
    }

    current_task->interrupts_enabled = x86_64_interrupts_enabled();
    x86_64_interrupts_disable();
    from = current_task;
    from->state = KERNEL_TASK_FINISHED;
    ++finished_task_count;

    next = task_select_next(from);
    if (next == NULL) {
        next = &bootstrap_task;
    }

    next->state = KERNEL_TASK_RUNNING;
    current_task = next;
    ++context_switch_count;
    x86_64_context_switch(&from->context, &next->context);

    x86_64_halt_forever();
}
