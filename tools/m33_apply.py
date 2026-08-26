#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"anchor not found: {path}: {old[:80]!r}")
    if text.count(old) != 1:
        raise SystemExit(f"anchor not unique: {path}: {text.count(old)}")
    p.write_text(text.replace(old, new, 1))


USER_MEMORY_ANCHOR = '''static uint32_t active_object_count(void) {\n'''
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

replace_once("kernel/core/user_memory.c", USER_MEMORY_ANCHOR, USER_MEMORY_INSERT)
