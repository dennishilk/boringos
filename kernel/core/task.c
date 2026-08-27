#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/context.h>
#include <boring/cpu.h>
#include <boring/exception.h>
#include <boring/heap.h>
#include <boring/irq.h>
#include <boring/ipc_syscall.h>
#include <boring/process.h>
#include <boring/task.h>

#define TASK_STACK_SENTINEL 0x424f52494e475354ULL
#define TASK_STACK_RESERVED_BYTES 16U
#define X86_64_RFLAGS_RESERVED_BIT 0x2ULL
#define X86_64_RFLAGS_INTERRUPT_ENABLE (1ULL << 9)

enum kernel_task_context_kind {
    KERNEL_TASK_CONTEXT_NONE = 0,
    KERNEL_TASK_CONTEXT_COOPERATIVE = 1,
    KERNEL_TASK_CONTEXT_PREEMPTIVE = 2
};

struct kernel_task {
    uint64_t id;
    struct process *process;
    enum kernel_task_state state;
    enum kernel_task_context_kind context_kind;
    struct x86_64_kernel_context context;
    struct x86_64_trap_frame *preempt_frame;
    void *stack_base;
    size_t stack_size;
    void (*entry)(void *);
    void *arg;
    uint64_t preempt_slices;
    bool slot_used;
    bool interrupts_enabled;
};

static struct kernel_task bootstrap_task;
static struct kernel_task tasks[KERNEL_TASK_MAX];
static struct kernel_task *current_task;
static bool task_initialized;
static bool preemption_enabled;
static uint64_t next_task_id;
static uint64_t created_task_count;
static uint64_t finished_task_count;
static uint64_t cooperative_context_switch_count;
static uint64_t cooperative_yield_call_count;
static volatile uint64_t scheduler_tick_count;
static volatile uint64_t preemption_count;
static volatile uint64_t finished_resume_count;
static volatile uint64_t scheduler_fault_count;

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
    task->process = NULL;
    task->state = KERNEL_TASK_FINISHED;
    task->context_kind = KERNEL_TASK_CONTEXT_NONE;
    context_clear(&task->context);
    task->preempt_frame = NULL;
    task->stack_base = NULL;
    task->stack_size = 0U;
    task->entry = NULL;
    task->arg = NULL;
    task->preempt_slices = 0ULL;
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

static bool task_process_is_valid(const struct kernel_task *task) {
    return (task != NULL) && (task->process != NULL) &&
           process_is_alive(task->process);
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

static bool task_stack_storage_is_valid(const struct kernel_task *task,
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

static bool task_preempt_frame_is_valid_for(
    const struct kernel_task *task,
    const struct x86_64_trap_frame *frame) {
    uintptr_t base;
    uintptr_t end;
    uintptr_t frame_address;
    uintptr_t frame_end;

    if ((frame == NULL) ||
        !task_stack_common_is_valid(task, &base, &end)) {
        return false;
    }

    frame_address = (uintptr_t)frame;
    if ((frame_address < (base + TASK_STACK_RESERVED_BYTES)) ||
        (sizeof(*frame) > (size_t)(UINTPTR_MAX - frame_address))) {
        return false;
    }
    frame_end = frame_address + (uintptr_t)sizeof(*frame);

    if ((frame_end > end) ||
        (frame->rsp < (uint64_t)(base + TASK_STACK_RESERVED_BYTES)) ||
        (frame->rsp > (uint64_t)end) ||
        (frame->hardware_rsp != frame->rsp) ||
        (frame->hardware_ss != frame->ss) ||
        (frame->vector != (uint64_t)X86_64_TIMER_VECTOR) ||
        (frame->error_code != 0ULL) || (frame->rip == 0ULL) ||
        (frame->cs == 0ULL) ||
        ((frame->rflags & X86_64_RFLAGS_RESERVED_BIT) == 0ULL)) {
        return false;
    }

    return true;
}

static bool task_stack_is_valid(const struct kernel_task *task) {
    uintptr_t base;
    uintptr_t end;

    if (!task_stack_common_is_valid(task, &base, &end)) {
        return false;
    }

    if (task->context_kind == KERNEL_TASK_CONTEXT_COOPERATIVE) {
        return (task->context.rsp >=
                (uint64_t)(base + TASK_STACK_RESERVED_BYTES)) &&
               (task->context.rsp < (uint64_t)end) &&
               ((task->context.rsp & 0x07ULL) == 0ULL);
    }

    if (task->context_kind == KERNEL_TASK_CONTEXT_PREEMPTIVE) {
        return task_preempt_frame_is_valid_for(task, task->preempt_frame);
    }

    return false;
}

static void task_restore_interrupt_state(void) {
    if ((current_task != NULL) && current_task->interrupts_enabled) {
        x86_64_interrupts_enable();
    } else {
        x86_64_interrupts_disable();
    }
}

static void task_select_syscall_stack(const struct kernel_task *task) {
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
    const struct kernel_task *from) {
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

        if (tasks[index].slot_used && task_process_is_valid(&tasks[index]) &&
            (tasks[index].context_kind == KERNEL_TASK_CONTEXT_COOPERATIVE) &&
            (tasks[index].state == KERNEL_TASK_READY) &&
            (&tasks[index] != from)) {
            return &tasks[index];
        }
    }

    return NULL;
}

static struct kernel_task *task_select_next_preemptive(
    const struct kernel_task *from) {
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

        if (tasks[index].slot_used && task_process_is_valid(&tasks[index]) &&
            (tasks[index].context_kind == KERNEL_TASK_CONTEXT_PREEMPTIVE) &&
            (tasks[index].state == KERNEL_TASK_READY) &&
            (&tasks[index] != from) &&
            task_preempt_frame_is_valid_for(&tasks[index],
                                            tasks[index].preempt_frame)) {
            return &tasks[index];
        }
    }

    return NULL;
}

static bool task_prepare_cooperative_context(struct kernel_task *task,
                                             uintptr_t stack_top) {
    const uintptr_t initial_rsp = stack_top - 16ULL;
    uint64_t *const initial_frame = (uint64_t *)initial_rsp;

    initial_frame[0] = (uint64_t)(uintptr_t)&kernel_task_trampoline;
    initial_frame[1] = 0ULL;
    task->context.rsp = (uint64_t)initial_rsp;
    task->context_kind = KERNEL_TASK_CONTEXT_COOPERATIVE;
    return true;
}

static bool task_prepare_preemptive_context(struct kernel_task *task,
                                            uintptr_t stack_top) {
    struct exception_stats exception_state;
    struct x86_64_trap_frame *frame;
    uint64_t *frame_words;
    const uintptr_t resume_rsp = stack_top - 8ULL;
    uintptr_t frame_address;
    size_t word_index;
    const uint64_t stack_segment = (uint64_t)x86_64_read_ss();

    if (!exception_get_stats(&exception_state) ||
        (exception_state.code_selector == 0U) ||
        (stack_segment == 0ULL) ||
        (resume_rsp < (uintptr_t)sizeof(struct x86_64_trap_frame))) {
        return false;
    }

    frame_address = resume_rsp -
                    (uintptr_t)sizeof(struct x86_64_trap_frame);
    frame = (struct x86_64_trap_frame *)frame_address;
    frame_words = (uint64_t *)frame;

    for (word_index = 0U;
         word_index < (sizeof(*frame) / sizeof(uint64_t));
         ++word_index) {
        frame_words[word_index] = 0ULL;
    }

    *(uint64_t *)resume_rsp = (uint64_t)(uintptr_t)&x86_64_halt_forever;

    frame->rsp = (uint64_t)resume_rsp;
    frame->ss = stack_segment;
    frame->vector = (uint64_t)X86_64_TIMER_VECTOR;
    frame->error_code = 0ULL;
    frame->rip = (uint64_t)(uintptr_t)&kernel_task_trampoline;
    frame->cs = (uint64_t)exception_state.code_selector;
    frame->rflags = X86_64_RFLAGS_RESERVED_BIT |
                    X86_64_RFLAGS_INTERRUPT_ENABLE;
    frame->hardware_rsp = (uint64_t)resume_rsp;
    frame->hardware_ss = stack_segment;

    task->preempt_frame = frame;
    task->context_kind = KERNEL_TASK_CONTEXT_PREEMPTIVE;
    task->interrupts_enabled = true;
    return true;
}

static bool task_create_internal(struct process *owner,
                                 void (*entry)(void *),
                                 void *arg,
                                 uint64_t *task_id,
                                 enum kernel_task_context_kind context_kind) {
    struct kernel_task *task = NULL;
    void *stack = NULL;
    uintptr_t stack_base;
    uintptr_t stack_top;
    size_t index;
    bool interrupts_were_enabled;
    bool prepared;

    interrupts_were_enabled = x86_64_interrupts_enabled();
    x86_64_interrupts_disable();

    if ((!task_initialized) || preemption_enabled || (owner == NULL) ||
        !process_is_alive(owner) || (entry == NULL) || (task_id == NULL) ||
        (next_task_id == UINT64_MAX) ||
        ((context_kind != KERNEL_TASK_CONTEXT_COOPERATIVE) &&
         (context_kind != KERNEL_TASK_CONTEXT_PREEMPTIVE))) {
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
        return false;
    }

    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        if (!tasks[index].slot_used) {
            task = &tasks[index];
            break;
        }
    }
    if (task == NULL) {
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
        return false;
    }

    stack = kmalloc((size_t)KERNEL_TASK_STACK_SIZE);
    if (stack == NULL) {
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
        return false;
    }

    stack_base = (uintptr_t)stack;
    if (((stack_base & ((uintptr_t)KERNEL_HEAP_ALIGNMENT - 1ULL)) != 0ULL) ||
        ((size_t)KERNEL_TASK_STACK_SIZE >
         (size_t)(UINTPTR_MAX - stack_base))) {
        (void)kfree(stack);
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
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
            if (interrupts_were_enabled) {
                x86_64_interrupts_enable();
            }
            return false;
        }
    }

    *(volatile uint64_t *)stack = TASK_STACK_SENTINEL;

    task_clear(task);
    task->id = next_task_id;
    task->process = owner;
    task->state = KERNEL_TASK_READY;
    task->stack_base = stack;
    task->stack_size = (size_t)KERNEL_TASK_STACK_SIZE;
    task->entry = entry;
    task->arg = arg;
    task->slot_used = true;
    task->interrupts_enabled = interrupts_were_enabled;

    if (context_kind == KERNEL_TASK_CONTEXT_COOPERATIVE) {
        prepared = task_prepare_cooperative_context(task, stack_top);
    } else {
        prepared = task_prepare_preemptive_context(task, stack_top);
    }

    if ((!prepared) || !task_stack_is_valid(task)) {
        task_clear(task);
        (void)kfree(stack);
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
        return false;
    }

    ++next_task_id;
    *task_id = task->id;
    ++created_task_count;

    if (interrupts_were_enabled) {
        x86_64_interrupts_enable();
    }
    return true;
}

bool task_init(void) {
    struct process *bootstrap_process;
    size_t index;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    bootstrap_process = process_bootstrap();
    if (task_initialized || (bootstrap_process == NULL) ||
        (process_current() != bootstrap_process) ||
        !process_is_alive(bootstrap_process)) {
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
        return false;
    }

    task_clear(&bootstrap_task);
    bootstrap_task.id = KERNEL_BOOTSTRAP_TASK_ID;
    bootstrap_task.process = bootstrap_process;
    bootstrap_task.state = KERNEL_TASK_RUNNING;
    bootstrap_task.slot_used = true;
    bootstrap_task.interrupts_enabled = interrupts_were_enabled;

    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        task_clear(&tasks[index]);
    }

    current_task = &bootstrap_task;
    next_task_id = 1ULL;
    created_task_count = 0ULL;
    finished_task_count = 0ULL;
    cooperative_context_switch_count = 0ULL;
    cooperative_yield_call_count = 0ULL;
    scheduler_tick_count = 0ULL;
    preemption_count = 0ULL;
    finished_resume_count = 0ULL;
    scheduler_fault_count = 0ULL;
    preemption_enabled = false;
    task_initialized = true;

    if (interrupts_were_enabled) {
        x86_64_interrupts_enable();
    }
    return true;
}

bool task_create(void (*entry)(void *), void *arg, uint64_t *task_id) {
    return task_create_internal(process_bootstrap(), entry, arg, task_id,
                                KERNEL_TASK_CONTEXT_COOPERATIVE);
}

bool task_create_for_process(struct process *process,
                             void (*entry)(void *),
                             void *arg, uint64_t *task_id) {
    return task_create_internal(process, entry, arg, task_id,
                                KERNEL_TASK_CONTEXT_COOPERATIVE);
}

bool task_create_preemptive(void (*entry)(void *), void *arg,
                            uint64_t *task_id) {
    return task_create_internal(process_bootstrap(), entry, arg, task_id,
                                KERNEL_TASK_CONTEXT_PREEMPTIVE);
}

bool task_create_preemptive_for_process(struct process *process,
                                        void (*entry)(void *),
                                        void *arg,
                                        uint64_t *task_id) {
    return task_create_internal(process, entry, arg, task_id,
                                KERNEL_TASK_CONTEXT_PREEMPTIVE);
}

void task_yield(void) {
    struct kernel_task *from;
    struct kernel_task *next;

    if ((!task_initialized) || (current_task == NULL)) {
        return;
    }

    ++cooperative_yield_call_count;
    if (preemption_enabled) {
        return;
    }

    current_task->interrupts_enabled = x86_64_interrupts_enabled();
    x86_64_interrupts_disable();

    from = current_task;
    if (from->state == KERNEL_TASK_RUNNING) {
        from->state = KERNEL_TASK_READY;
    }

    next = task_select_next_cooperative(from);
    if (next == NULL) {
        from->state = KERNEL_TASK_RUNNING;
        current_task = from;
        task_restore_interrupt_state();
        return;
    }

    task_select_syscall_stack(next);
    if (!process_activate(next->process)) {
        task_select_syscall_stack(from);
        from->state = KERNEL_TASK_RUNNING;
        current_task = from;
        task_restore_interrupt_state();
        return;
    }

    next->state = KERNEL_TASK_RUNNING;
    current_task = next;
    ++cooperative_context_switch_count;
    x86_64_context_switch(&from->context, &next->context);

    task_restore_interrupt_state();
}

bool task_block_current(void) {
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

bool task_wake_pid(uint64_t pid) {
    size_t index;

    if (!task_initialized || (pid == 0ULL)) {
        return false;
    }
    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        if (tasks[index].slot_used && (tasks[index].process != NULL) &&
            (tasks[index].process->pid == pid)) {
            return task_wake_process(tasks[index].process);
        }
    }
    return false;
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
    if ((!task_initialized) || (current_task == NULL)) {
        return UINT64_MAX;
    }

    return current_task->id;
}

uint64_t task_current_process_id(void) {
    if ((!task_initialized) || (current_task == NULL) ||
        !task_process_is_valid(current_task) ||
        (process_current() != current_task->process)) {
        return UINT64_MAX;
    }

    return current_task->process->pid;
}

uint64_t task_current_preempt_slices(void) {
    if ((!task_initialized) || !task_is_regular(current_task) ||
        (current_task->context_kind != KERNEL_TASK_CONTEXT_PREEMPTIVE)) {
        return 0ULL;
    }

    return current_task->preempt_slices;
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
    bool interrupts_were_enabled;

    if ((!task_initialized) || (stats == NULL)) {
        return false;
    }

    interrupts_were_enabled = x86_64_interrupts_enabled();
    x86_64_interrupts_disable();

    if ((current_task == NULL) || !task_process_is_valid(current_task) ||
        (process_current() != current_task->process)) {
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
        return false;
    }

    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        if (tasks[index].slot_used) {
            ++active;
        }
    }

    stats->created_tasks = created_task_count;
    stats->finished_tasks = finished_task_count;
    stats->context_switches = cooperative_context_switch_count;
    stats->cooperative_yield_calls = cooperative_yield_call_count;
    stats->scheduler_ticks = scheduler_tick_count;
    stats->preemptions = preemption_count;
    stats->finished_resume_count = finished_resume_count;
    stats->scheduler_fault_count = scheduler_fault_count;
    stats->active_tasks = active;
    stats->current_task_id = current_task->id;
    stats->current_process_pid = current_task->process->pid;
    stats->stack_size = (size_t)KERNEL_TASK_STACK_SIZE;
    stats->preemption_enabled = preemption_enabled;

    if (interrupts_were_enabled) {
        x86_64_interrupts_enable();
    }
    return true;
}

bool task_finished_stacks_valid(void) {
    size_t index;
    bool valid = true;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();

    if ((!task_initialized) || preemption_enabled ||
        (current_task != &bootstrap_task) ||
        (bootstrap_task.state != KERNEL_TASK_RUNNING) ||
        (process_current() != bootstrap_task.process)) {
        valid = false;
    } else {
        for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
            if (tasks[index].slot_used &&
                ((tasks[index].state != KERNEL_TASK_FINISHED) ||
                 !task_stack_storage_is_valid(&tasks[index], NULL, NULL))) {
                valid = false;
                break;
            }
        }
    }

    if (interrupts_were_enabled) {
        x86_64_interrupts_enable();
    }
    return valid;
}

bool task_reap_finished_process(struct process *process) {
    size_t index;
    bool found = false;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if ((!task_initialized) || preemption_enabled || (process == NULL) ||
        (process == process_current()) ||
        (process->state != PROCESS_FINISHED)) {
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
        return false;
    }

    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        if (tasks[index].slot_used && (tasks[index].process == process)) {
            found = true;
            if ((&tasks[index] == current_task) ||
                (tasks[index].state != KERNEL_TASK_FINISHED) ||
                !task_stack_storage_is_valid(&tasks[index], NULL, NULL)) {
                if (interrupts_were_enabled) {
                    x86_64_interrupts_enable();
                }
                return false;
            }
        }
    }
    if (!found) {
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
        return false;
    }

    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        if (tasks[index].slot_used && (tasks[index].process == process)) {
            void *const stack = tasks[index].stack_base;

            if (!kfree(stack)) {
                if (interrupts_were_enabled) {
                    x86_64_interrupts_enable();
                }
                return false;
            }
            task_clear(&tasks[index]);
        }
    }

    if (interrupts_were_enabled) {
        x86_64_interrupts_enable();
    }
    return true;
}

bool task_cleanup_finished(uint64_t *freed_stacks) {
    uint64_t freed = 0ULL;
    size_t index;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();

    if ((!task_initialized) || (freed_stacks == NULL) ||
        preemption_enabled || (current_task != &bootstrap_task) ||
        (bootstrap_task.state != KERNEL_TASK_RUNNING) ||
        (process_current() != bootstrap_task.process)) {
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
        return false;
    }

    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        if (tasks[index].slot_used &&
            ((tasks[index].state != KERNEL_TASK_FINISHED) ||
             !task_stack_storage_is_valid(&tasks[index], NULL, NULL))) {
            if (interrupts_were_enabled) {
                x86_64_interrupts_enable();
            }
            return false;
        }
    }

    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        if (tasks[index].slot_used) {
            void *const stack = tasks[index].stack_base;

            if (!kfree(stack)) {
                if (interrupts_were_enabled) {
                    x86_64_interrupts_enable();
                }
                return false;
            }
            task_clear(&tasks[index]);
            ++freed;
        }
    }

    *freed_stacks = freed;
    if (interrupts_were_enabled) {
        x86_64_interrupts_enable();
    }
    return true;
}

bool task_preemption_start(void) {
    size_t index;
    uint64_t ready_count = 0ULL;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();

    if ((!task_initialized) || preemption_enabled ||
        (current_task != &bootstrap_task) ||
        (bootstrap_task.state != KERNEL_TASK_RUNNING) ||
        (process_current() != bootstrap_task.process)) {
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
        return false;
    }

    for (index = 0U; index < (size_t)KERNEL_TASK_MAX; ++index) {
        if (tasks[index].slot_used) {
            if ((tasks[index].state != KERNEL_TASK_READY) ||
                !task_process_is_valid(&tasks[index]) ||
                (tasks[index].context_kind !=
                 KERNEL_TASK_CONTEXT_PREEMPTIVE) ||
                !task_preempt_frame_is_valid_for(
                    &tasks[index], tasks[index].preempt_frame)) {
                if (interrupts_were_enabled) {
                    x86_64_interrupts_enable();
                }
                return false;
            }
            ++ready_count;
        }
    }

    if (ready_count == 0ULL) {
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
        return false;
    }

    bootstrap_task.preempt_frame = NULL;
    scheduler_tick_count = 0ULL;
    preemption_count = 0ULL;
    finished_resume_count = 0ULL;
    scheduler_fault_count = 0ULL;
    preemption_enabled = true;

    if (interrupts_were_enabled) {
        x86_64_interrupts_enable();
    }
    return true;
}

bool task_preemption_stop(void) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();

    if ((!task_initialized) || (!preemption_enabled) ||
        (current_task != &bootstrap_task) ||
        (bootstrap_task.state != KERNEL_TASK_RUNNING) ||
        (process_current() != bootstrap_task.process)) {
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
        return false;
    }

    preemption_enabled = false;
    bootstrap_task.preempt_frame = NULL;

    if (interrupts_were_enabled) {
        x86_64_interrupts_enable();
    }
    return true;
}

struct x86_64_trap_frame *task_scheduler_tick(
    struct x86_64_trap_frame *frame) {
    struct kernel_task *from;
    struct kernel_task *next;

    if (frame == NULL) {
        return NULL;
    }

    if ((!task_initialized) || (!preemption_enabled)) {
        return frame;
    }

    ++scheduler_tick_count;

    from = current_task;
    if ((from == NULL) || !task_process_is_valid(from) ||
        (process_current() != from->process)) {
        ++scheduler_fault_count;
        preemption_enabled = false;
        return frame;
    }

    if (from == &bootstrap_task) {
        bootstrap_task.preempt_frame = frame;
        bootstrap_task.state = KERNEL_TASK_READY;
    } else if (task_is_regular(from) &&
               (from->context_kind == KERNEL_TASK_CONTEXT_PREEMPTIVE) &&
               task_preempt_frame_is_valid_for(from, frame)) {
        from->preempt_frame = frame;
        if (from->state == KERNEL_TASK_RUNNING) {
            from->state = KERNEL_TASK_READY;
        } else if (from->state != KERNEL_TASK_FINISHED) {
            ++scheduler_fault_count;
            preemption_enabled = false;
            return frame;
        }
    } else {
        ++scheduler_fault_count;
        preemption_enabled = false;
        return frame;
    }

    next = task_select_next_preemptive(from);
    if (next == NULL) {
        if ((from != &bootstrap_task) &&
            (from->state == KERNEL_TASK_FINISHED)) {
            if (bootstrap_task.preempt_frame == NULL) {
                ++scheduler_fault_count;
                preemption_enabled = false;
                return frame;
            }
            next = &bootstrap_task;
        } else {
            from->state = KERNEL_TASK_RUNNING;
            current_task = from;
            return frame;
        }
    }

    if (next == &bootstrap_task) {
        if (bootstrap_task.preempt_frame == NULL) {
            ++scheduler_fault_count;
            preemption_enabled = false;
            return frame;
        }
    } else if (!task_preempt_frame_is_valid_for(next,
                                                next->preempt_frame)) {
        ++scheduler_fault_count;
        preemption_enabled = false;
        return frame;
    }

    if (!task_process_is_valid(next) || !process_activate(next->process)) {
        ++scheduler_fault_count;
        preemption_enabled = false;
        if (from->state == KERNEL_TASK_READY) {
            from->state = KERNEL_TASK_RUNNING;
        }
        current_task = from;
        return frame;
    }

    next->state = KERNEL_TASK_RUNNING;
    current_task = next;
    if (task_is_regular(next)) {
        ++next->preempt_slices;
    }
    ++preemption_count;
    return next->preempt_frame;
}

static void kernel_task_trampoline(void) {
    struct kernel_task *task = current_task;

    if ((!task_initialized) || !task_is_regular(task) ||
        !task_process_is_valid(task) || (process_current() != task->process) ||
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
        !task_process_is_valid(current_task) ||
        (process_current() != current_task->process) ||
        (current_task->state != KERNEL_TASK_RUNNING)) {
        x86_64_halt_forever();
    }

    current_task->interrupts_enabled = x86_64_interrupts_enabled();
    x86_64_interrupts_disable();
    from = current_task;
    from->state = KERNEL_TASK_FINISHED;
    ++finished_task_count;

    if (preemption_enabled) {
        for (;;) {
            x86_64_enable_and_halt();
            ++finished_resume_count;
        }
    }

    next = task_select_next_cooperative(from);
    if (next == NULL) {
        next = &bootstrap_task;
    }

    task_select_syscall_stack(next);
    if (!task_process_is_valid(next) || !process_activate(next->process)) {
        x86_64_halt_forever();
    }

    next->state = KERNEL_TASK_RUNNING;
    current_task = next;
    ++cooperative_context_switch_count;
    x86_64_context_switch(&from->context, &next->context);

    x86_64_halt_forever();
}
