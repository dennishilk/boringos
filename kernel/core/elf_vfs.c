#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/elf_loader.h>
#include <boring/elf_vfs.h>
#include <boring/process.h>
#include <boring/vfs.h>

static bool elf_vfs_read(void *context,
                         uint64_t offset,
                         void *buffer,
                         size_t length) {
    struct boring_elf_vfs_source *source =
        (struct boring_elf_vfs_source *)context;
    size_t transferred = 0U;
    enum vfs_result result;

    if ((source == NULL) || !source->open || (buffer == NULL) ||
        (length == 0U) || (offset > source->source.size) ||
        ((uint64_t)length > source->source.size - offset)) {
        return false;
    }
    source->handle.offset = offset;
    result = vfs_handle_read(&source->handle, buffer, length,
                             &transferred);
    return (result == VFS_RESULT_OK) && (transferred == length);
}

static enum vfs_result elf_vfs_measure(
    struct boring_elf_vfs_source *source,
    uint64_t *size_out) {
    uint8_t buffer[VFS_IO_MAX];
    uint64_t total = 0ULL;

    if ((source == NULL) || !source->open || (size_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    for (;;) {
        size_t transferred = 0U;
        size_t request = sizeof(buffer);
        enum vfs_result result;

        if (total > (uint64_t)BORING_ELF_MODULE_MAX_SIZE) {
            return VFS_RESULT_OVERFLOW;
        }
        if (total == (uint64_t)BORING_ELF_MODULE_MAX_SIZE) {
            request = 1U;
        }
        source->handle.offset = total;
        result = vfs_handle_read(&source->handle, buffer, request,
                                 &transferred);
        if (result != VFS_RESULT_OK) {
            return result;
        }
        if (transferred == 0U) {
            *size_out = total;
            return VFS_RESULT_OK;
        }
        if ((uint64_t)transferred > UINT64_MAX - total) {
            return VFS_RESULT_OVERFLOW;
        }
        total += (uint64_t)transferred;
        if (total > (uint64_t)BORING_ELF_MODULE_MAX_SIZE) {
            return VFS_RESULT_OVERFLOW;
        }
    }
}

enum vfs_result boring_elf_vfs_source_open(
    const struct process *process,
    const char *path,
    struct boring_elf_vfs_source *source_out) {
    struct vfs_path resolved = { NULL, NULL };
    enum vfs_result result;
    uint64_t size = 0ULL;

    if ((process == NULL) || (path == NULL) || (source_out == NULL) ||
        source_out->open) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    source_out->source.size = 0ULL;
    source_out->source.read = NULL;
    source_out->source.context = NULL;
    source_out->handle.path.mount = NULL;
    source_out->handle.path.node = NULL;
    source_out->handle.offset = 0ULL;
    source_out->handle.access = 0U;
    source_out->handle.open = false;
    source_out->open = false;

    result = vfs_resolve_process(process, path, &resolved);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    result = vfs_handle_open(&resolved, VFS_ACCESS_READ,
                             &source_out->handle);
    if (vfs_path_release(&resolved) != VFS_RESULT_OK) {
        if (result == VFS_RESULT_OK) {
            (void)vfs_handle_close(&source_out->handle);
        }
        return VFS_RESULT_CORRUPT;
    }
    if (result != VFS_RESULT_OK) {
        return result;
    }
    source_out->open = true;
    result = elf_vfs_measure(source_out, &size);
    if (result != VFS_RESULT_OK) {
        (void)boring_elf_vfs_source_close(source_out);
        return result;
    }
    source_out->handle.offset = 0ULL;
    source_out->source.size = size;
    source_out->source.read = elf_vfs_read;
    source_out->source.context = source_out;
    return VFS_RESULT_OK;
}

enum vfs_result boring_elf_vfs_source_close(
    struct boring_elf_vfs_source *source) {
    enum vfs_result result;

    if ((source == NULL) || !source->open) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    result = vfs_handle_close(&source->handle);
    if (result != VFS_RESULT_OK) {
        return result;
    }
    source->source.size = 0ULL;
    source->source.read = NULL;
    source->source.context = NULL;
    source->open = false;
    return VFS_RESULT_OK;
}
