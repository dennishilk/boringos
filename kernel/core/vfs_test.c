#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/heap.h>
#include <boring/pmm.h>
#include <boring/process.h>
#include <boring/serial.h>
#include <boring/vfs.h>
#include <boring/vfs_test.h>

struct vfs_test_backend {
    uint64_t lookup_count;
    uint64_t create_count;
    uint64_t mkdir_count;
    uint64_t unlink_count;
    uint64_t rmdir_count;
    uint64_t rename_count;
    uint64_t read_count;
    uint64_t write_count;
    uint64_t truncate_count;
    uint64_t readdir_count;
    uint64_t last_read_offset;
    uint64_t last_write_offset;
    uint64_t last_truncate_size;
};

static struct vfs_test_backend root_backend;
static struct vfs_test_backend child_backend;
static struct vfs_test_backend unsupported_backend;

static struct vfs_filesystem root_filesystem;
static struct vfs_filesystem child_filesystem;
static struct vfs_filesystem unsupported_filesystem;

static struct vfs_node root_node;
static struct vfs_node root_alpha;
static struct vfs_node root_alpha_item;
static struct vfs_node root_beta;
static struct vfs_node root_beta_item;
static struct vfs_node root_file;
static struct vfs_node root_mountpoint;
static struct vfs_node root_unsupported;
static struct vfs_node root_created_sentinel;
static struct vfs_node root_mkdir_sentinel;
static struct vfs_node child_root;
static struct vfs_node child_mounted_file;
static struct vfs_node unsupported_root;
static struct vfs_node unsupported_file;

static bool vfs_test_name_equals(const char *name,
                                 size_t name_length,
                                 const char *literal) {
    size_t index = 0U;

    if ((name == NULL) || (literal == NULL)) {
        return false;
    }
    while (literal[index] != '\0') {
        if ((index >= name_length) || (name[index] != literal[index])) {
            return false;
        }
        ++index;
    }
    return index == name_length;
}

static struct vfs_test_backend *vfs_test_context(
    struct vfs_filesystem *filesystem) {
    if ((filesystem == NULL) || (filesystem->backend_context == NULL)) {
        return NULL;
    }
    return (struct vfs_test_backend *)filesystem->backend_context;
}

static enum vfs_result vfs_test_lookup(struct vfs_filesystem *filesystem,
                                       struct vfs_node *directory,
                                       const char *name,
                                       size_t name_length,
                                       struct vfs_node **node_out) {
    struct vfs_test_backend *const backend = vfs_test_context(filesystem);

    if ((backend == NULL) || (directory == NULL) || (name == NULL) ||
        (name_length == 0U) || (node_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    ++backend->lookup_count;
    *node_out = NULL;

    if (filesystem == &root_filesystem) {
        if (directory == &root_node) {
            if (vfs_test_name_equals(name, name_length, "alpha")) {
                *node_out = &root_alpha;
            } else if (vfs_test_name_equals(name, name_length, "beta")) {
                *node_out = &root_beta;
            } else if (vfs_test_name_equals(name, name_length, "file")) {
                *node_out = &root_file;
            } else if (vfs_test_name_equals(name, name_length,
                                            "mountpoint")) {
                *node_out = &root_mountpoint;
            } else if (vfs_test_name_equals(name, name_length,
                                            "unsupported")) {
                *node_out = &root_unsupported;
            } else if (vfs_test_name_equals(name, name_length, "corrupt")) {
                *node_out = &child_mounted_file;
            }
        } else if ((directory == &root_alpha) &&
                   vfs_test_name_equals(name, name_length, "item")) {
            *node_out = &root_alpha_item;
        } else if ((directory == &root_beta) &&
                   vfs_test_name_equals(name, name_length, "item")) {
            *node_out = &root_beta_item;
        }
    } else if ((filesystem == &child_filesystem) &&
               (directory == &child_root) &&
               vfs_test_name_equals(name, name_length, "mounted-file")) {
        *node_out = &child_mounted_file;
    } else if ((filesystem == &unsupported_filesystem) &&
               (directory == &unsupported_root) &&
               vfs_test_name_equals(name, name_length, "no-read")) {
        *node_out = &unsupported_file;
    }

    return (*node_out == NULL) ? VFS_RESULT_NOT_FOUND : VFS_RESULT_OK;
}

static enum vfs_result vfs_test_create(struct vfs_filesystem *filesystem,
                                       struct vfs_node *directory,
                                       const char *name,
                                       size_t name_length,
                                       struct vfs_node **node_out) {
    struct vfs_test_backend *const backend = vfs_test_context(filesystem);

    if ((backend == NULL) || (filesystem != &root_filesystem) ||
        (directory != &root_node) || (name == NULL) ||
        (name_length == 0U) || (node_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    ++backend->create_count;
    *node_out = &root_created_sentinel;
    return VFS_RESULT_OK;
}

static enum vfs_result vfs_test_mkdir(struct vfs_filesystem *filesystem,
                                      struct vfs_node *directory,
                                      const char *name,
                                      size_t name_length,
                                      struct vfs_node **node_out) {
    struct vfs_test_backend *const backend = vfs_test_context(filesystem);

    if ((backend == NULL) || (filesystem != &root_filesystem) ||
        (directory != &root_node) || (name == NULL) ||
        (name_length == 0U) || (node_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    ++backend->mkdir_count;
    *node_out = &root_mkdir_sentinel;
    return VFS_RESULT_OK;
}

static enum vfs_result vfs_test_unlink(struct vfs_filesystem *filesystem,
                                       struct vfs_node *directory,
                                       const char *name,
                                       size_t name_length) {
    struct vfs_test_backend *const backend = vfs_test_context(filesystem);

    if ((backend == NULL) || (filesystem != &root_filesystem) ||
        (directory != &root_node) || (name == NULL) ||
        (name_length == 0U)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    ++backend->unlink_count;
    return VFS_RESULT_OK;
}

static enum vfs_result vfs_test_rmdir(struct vfs_filesystem *filesystem,
                                      struct vfs_node *directory,
                                      const char *name,
                                      size_t name_length) {
    struct vfs_test_backend *const backend = vfs_test_context(filesystem);

    if ((backend == NULL) || (filesystem != &root_filesystem) ||
        (directory != &root_node) || (name == NULL) ||
        (name_length == 0U)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    ++backend->rmdir_count;
    return VFS_RESULT_OK;
}

static enum vfs_result vfs_test_rename(struct vfs_filesystem *filesystem,
                                       struct vfs_node *old_directory,
                                       const char *old_name,
                                       size_t old_name_length,
                                       struct vfs_node *new_directory,
                                       const char *new_name,
                                       size_t new_name_length) {
    struct vfs_test_backend *const backend = vfs_test_context(filesystem);

    if ((backend == NULL) || (filesystem != &root_filesystem) ||
        (old_directory != &root_node) || (new_directory != &root_node) ||
        (old_name == NULL) || (new_name == NULL) ||
        (old_name_length == 0U) || (new_name_length == 0U)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    ++backend->rename_count;
    return VFS_RESULT_OK;
}

static enum vfs_result vfs_test_read(struct vfs_filesystem *filesystem,
                                     struct vfs_node *node,
                                     uint64_t offset,
                                     void *buffer,
                                     size_t length,
                                     size_t *transferred_out) {
    static const uint8_t data[3] = { 'V', 'F', 'S' };
    struct vfs_test_backend *const backend = vfs_test_context(filesystem);
    uint8_t *const output = (uint8_t *)buffer;
    size_t transferred;
    size_t index;

    if ((backend == NULL) || (node == NULL) || (buffer == NULL) ||
        (transferred_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    transferred = (length < sizeof(data)) ? length : sizeof(data);
    for (index = 0U; index < transferred; ++index) {
        output[index] = data[index];
    }
    ++backend->read_count;
    backend->last_read_offset = offset;
    *transferred_out = transferred;
    return VFS_RESULT_OK;
}

static enum vfs_result vfs_test_write(struct vfs_filesystem *filesystem,
                                      struct vfs_node *node,
                                      uint64_t offset,
                                      const void *buffer,
                                      size_t length,
                                      size_t *transferred_out) {
    struct vfs_test_backend *const backend = vfs_test_context(filesystem);

    if ((backend == NULL) || (node == NULL) || (buffer == NULL) ||
        (transferred_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    ++backend->write_count;
    backend->last_write_offset = offset;
    *transferred_out = length;
    return VFS_RESULT_OK;
}

static enum vfs_result vfs_test_truncate(struct vfs_filesystem *filesystem,
                                         struct vfs_node *node,
                                         uint64_t size) {
    struct vfs_test_backend *const backend = vfs_test_context(filesystem);

    if ((backend == NULL) || (node == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    ++backend->truncate_count;
    backend->last_truncate_size = size;
    return VFS_RESULT_OK;
}

static enum vfs_result vfs_test_readdir(struct vfs_filesystem *filesystem,
                                        struct vfs_node *directory,
                                        uint64_t index,
                                        struct vfs_dirent *entry_out) {
    struct vfs_test_backend *const backend = vfs_test_context(filesystem);

    if ((backend == NULL) || (filesystem != &root_filesystem) ||
        (directory != &root_node) || (entry_out == NULL)) {
        return VFS_RESULT_INVALID_ARGUMENT;
    }
    ++backend->readdir_count;
    if (index != 0ULL) {
        return VFS_RESULT_NOT_FOUND;
    }

    entry_out->node_id = root_alpha.id;
    entry_out->type = VFS_NODE_DIRECTORY;
    entry_out->name_length = 5U;
    entry_out->name[0] = 'a';
    entry_out->name[1] = 'l';
    entry_out->name[2] = 'p';
    entry_out->name[3] = 'h';
    entry_out->name[4] = 'a';
    entry_out->name[5] = '\0';
    return VFS_RESULT_OK;
}

static const struct vfs_operations root_operations = {
    .lookup = vfs_test_lookup,
    .create = vfs_test_create,
    .mkdir = vfs_test_mkdir,
    .unlink = vfs_test_unlink,
    .rmdir = vfs_test_rmdir,
    .rename = vfs_test_rename,
    .read = vfs_test_read,
    .write = vfs_test_write,
    .truncate = vfs_test_truncate,
    .readdir = vfs_test_readdir
};

static const struct vfs_operations child_operations = {
    .lookup = vfs_test_lookup,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL,
    .rename = NULL,
    .read = vfs_test_read,
    .write = vfs_test_write,
    .truncate = vfs_test_truncate,
    .readdir = NULL
};

static const struct vfs_operations unsupported_operations = {
    .lookup = vfs_test_lookup,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL,
    .rename = NULL,
    .read = NULL,
    .write = NULL,
    .truncate = NULL,
    .readdir = NULL
};

static void vfs_test_fail(const char *check) __attribute__((noreturn));
static void vfs_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("VFS core test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static void vfs_test_pass(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": PASS\n");
}

static bool vfs_test_prepare_filesystems(void) {
    root_backend = (struct vfs_test_backend){ 0 };
    child_backend = (struct vfs_test_backend){ 0 };
    unsupported_backend = (struct vfs_test_backend){ 0 };

    if (!vfs_filesystem_prepare(&root_filesystem, 1ULL,
                                &root_operations, &root_backend) ||
        !vfs_node_prepare(&root_node, &root_filesystem, 1ULL,
                          VFS_NODE_DIRECTORY, NULL, NULL) ||
        !vfs_filesystem_set_root(&root_filesystem, &root_node) ||
        !vfs_node_prepare(&root_alpha, &root_filesystem, 2ULL,
                          VFS_NODE_DIRECTORY, &root_node, NULL) ||
        !vfs_node_prepare(&root_alpha_item, &root_filesystem, 3ULL,
                          VFS_NODE_REGULAR, &root_alpha, NULL) ||
        !vfs_node_prepare(&root_beta, &root_filesystem, 4ULL,
                          VFS_NODE_DIRECTORY, &root_node, NULL) ||
        !vfs_node_prepare(&root_beta_item, &root_filesystem, 5ULL,
                          VFS_NODE_REGULAR, &root_beta, NULL) ||
        !vfs_node_prepare(&root_file, &root_filesystem, 6ULL,
                          VFS_NODE_REGULAR, &root_node, NULL) ||
        !vfs_node_prepare(&root_mountpoint, &root_filesystem, 7ULL,
                          VFS_NODE_DIRECTORY, &root_node, NULL) ||
        !vfs_node_prepare(&root_unsupported, &root_filesystem, 8ULL,
                          VFS_NODE_DIRECTORY, &root_node, NULL) ||
        !vfs_node_prepare(&root_created_sentinel, &root_filesystem, 9ULL,
                          VFS_NODE_REGULAR, &root_node, NULL) ||
        !vfs_node_prepare(&root_mkdir_sentinel, &root_filesystem, 10ULL,
                          VFS_NODE_DIRECTORY, &root_node, NULL)) {
        return false;
    }

    if (!vfs_filesystem_prepare(&child_filesystem, 2ULL,
                                &child_operations, &child_backend) ||
        !vfs_node_prepare(&child_root, &child_filesystem, 101ULL,
                          VFS_NODE_DIRECTORY, NULL, NULL) ||
        !vfs_filesystem_set_root(&child_filesystem, &child_root) ||
        !vfs_node_prepare(&child_mounted_file, &child_filesystem, 102ULL,
                          VFS_NODE_REGULAR, &child_root, NULL)) {
        return false;
    }

    if (!vfs_filesystem_prepare(&unsupported_filesystem, 3ULL,
                                &unsupported_operations,
                                &unsupported_backend) ||
        !vfs_node_prepare(&unsupported_root, &unsupported_filesystem, 201ULL,
                          VFS_NODE_DIRECTORY, NULL, NULL) ||
        !vfs_filesystem_set_root(&unsupported_filesystem,
                                 &unsupported_root) ||
        !vfs_node_prepare(&unsupported_file, &unsupported_filesystem, 202ULL,
                          VFS_NODE_REGULAR, &unsupported_root, NULL)) {
        return false;
    }

    return true;
}

static bool vfs_test_all_references_released(void) {
    const struct vfs_node *const nodes[] = {
        &root_node,
        &root_alpha,
        &root_alpha_item,
        &root_beta,
        &root_beta_item,
        &root_file,
        &root_mountpoint,
        &root_unsupported,
        &root_created_sentinel,
        &root_mkdir_sentinel,
        &child_root,
        &child_mounted_file,
        &unsupported_root,
        &unsupported_file
    };
    size_t index;

    for (index = 0U; index < (sizeof(nodes) / sizeof(nodes[0])); ++index) {
        if (nodes[index]->reference_count != 0U) {
            return false;
        }
    }
    return true;
}

void vfs_test_run(void) {
    struct vfs_path root_path = { NULL, NULL };
    struct vfs_path alpha_path = { NULL, NULL };
    struct vfs_path beta_path = { NULL, NULL };
    struct vfs_path file_path = { NULL, NULL };
    struct vfs_path mountpoint_path = { NULL, NULL };
    struct vfs_path unsupported_target = { NULL, NULL };
    struct vfs_path mounted_root = { NULL, NULL };
    struct vfs_path mounted_file = { NULL, NULL };
    struct vfs_path unsupported_file_path = { NULL, NULL };
    struct vfs_path created_path = { NULL, NULL };
    struct vfs_path mkdir_path = { NULL, NULL };
    struct vfs_path temporary = { NULL, NULL };
    struct vfs_handle handle = { { NULL, NULL }, 0ULL, 0U, false };
    struct vfs_handle unsupported_handle = {
        { NULL, NULL }, 0ULL, 0U, false
    };
    struct vfs_handle invalid_handle = {
        { NULL, NULL }, 0ULL, 0U, false
    };
    struct vfs_dirent entry;
    struct process *process_a = NULL;
    struct process *process_b = NULL;
    struct process_stats process_final;
    struct pmm_stats pmm_before;
    struct pmm_stats pmm_after;
    struct heap_stats heap_before;
    struct heap_stats heap_after;
    struct vfs_stats vfs_stats;
    uint8_t read_buffer[4] = { 0U, 0U, 0U, 0U };
    static const uint8_t write_buffer[2] = { 'O', 'K' };
    char overlong_path[VFS_PATH_MAX + 2U];
    char overlong_component[VFS_NAME_MAX + 2U];
    size_t index;
    size_t transferred = 0U;
    uint64_t offset = 0ULL;
    uint64_t lookup_before;
    uint64_t rename_before;

    serial_write_string("VFS core:\n");

    if (!process_init() || !vfs_test_prepare_filesystems() ||
        (vfs_init(&root_filesystem) != VFS_RESULT_OK) ||
        !pmm_get_stats(&pmm_before) || !heap_get_stats(&heap_before) ||
        (vfs_get_root(&root_path) != VFS_RESULT_OK)) {
        vfs_test_fail("root-path");
    }
    if ((vfs_resolve(NULL, "/", &temporary) != VFS_RESULT_OK) ||
        !vfs_path_equal(&root_path, &temporary) ||
        (vfs_path_release(&temporary) != VFS_RESULT_OK)) {
        vfs_test_fail("root-path");
    }
    vfs_test_pass("root-path");

    if ((vfs_resolve(NULL, "/alpha/item", &temporary) != VFS_RESULT_OK) ||
        (temporary.node != &root_alpha_item) ||
        (vfs_path_release(&temporary) != VFS_RESULT_OK)) {
        vfs_test_fail("absolute-path");
    }
    vfs_test_pass("absolute-path");

    if ((vfs_resolve(NULL, "///alpha//item", &temporary) != VFS_RESULT_OK) ||
        (temporary.node != &root_alpha_item) ||
        (vfs_path_release(&temporary) != VFS_RESULT_OK)) {
        vfs_test_fail("repeated-slash");
    }
    vfs_test_pass("repeated-slash");

    if ((vfs_resolve(NULL, "/alpha/./item", &temporary) != VFS_RESULT_OK) ||
        (temporary.node != &root_alpha_item) ||
        (vfs_path_release(&temporary) != VFS_RESULT_OK)) {
        vfs_test_fail("dot");
    }
    vfs_test_pass("dot");

    if ((vfs_resolve(NULL, "/../../", &temporary) != VFS_RESULT_OK) ||
        !vfs_path_equal(&root_path, &temporary) ||
        (vfs_path_release(&temporary) != VFS_RESULT_OK)) {
        vfs_test_fail("dotdot-root");
    }
    vfs_test_pass("dotdot-root");

    if ((vfs_resolve(NULL, "/alpha", &alpha_path) != VFS_RESULT_OK) ||
        (vfs_resolve(NULL, "/beta", &beta_path) != VFS_RESULT_OK) ||
        !process_create(&process_a) || !process_create(&process_b) ||
        !process_set_cwd(process_a, &alpha_path) ||
        !process_set_cwd(process_b, &beta_path)) {
        vfs_test_fail("process-cwd");
    }
    if ((vfs_resolve_process(process_a, "item", &temporary) !=
         VFS_RESULT_OK) || (temporary.node != &root_alpha_item) ||
        (vfs_path_release(&temporary) != VFS_RESULT_OK) ||
        (vfs_resolve_process(process_b, "item", &temporary) !=
         VFS_RESULT_OK) || (temporary.node != &root_beta_item) ||
        (vfs_path_release(&temporary) != VFS_RESULT_OK)) {
        vfs_test_fail("relative-path");
    }
    vfs_test_pass("relative-path");
    vfs_test_pass("process-cwd");

    for (index = 0U; index < ((size_t)VFS_PATH_MAX + 1U); ++index) {
        overlong_path[index] = 'a';
    }
    overlong_path[VFS_PATH_MAX + 1U] = '\0';
    if ((vfs_resolve(&root_path, "", &temporary) !=
         VFS_RESULT_EMPTY_PATH) ||
        (vfs_resolve(&root_path, overlong_path, &temporary) !=
         VFS_RESULT_PATH_TOO_LONG)) {
        vfs_test_fail("path-bounds");
    }
    vfs_test_pass("path-bounds");

    for (index = 0U; index < ((size_t)VFS_NAME_MAX + 1U); ++index) {
        overlong_component[index] = 'n';
    }
    overlong_component[VFS_NAME_MAX + 1U] = '\0';
    if (vfs_resolve(&root_path, overlong_component, &temporary) !=
        VFS_RESULT_NAME_TOO_LONG) {
        vfs_test_fail("component-bounds");
    }
    vfs_test_pass("component-bounds");

    if ((vfs_resolve(NULL, "/file", &file_path) != VFS_RESULT_OK) ||
        (vfs_resolve(NULL, "/file/child", &temporary) !=
         VFS_RESULT_NOT_DIRECTORY)) {
        vfs_test_fail("non-directory-lookup");
    }
    vfs_test_pass("non-directory-lookup");

    if ((vfs_resolve(NULL, "/mountpoint", &mountpoint_path) !=
         VFS_RESULT_OK) ||
        (vfs_resolve(NULL, "/unsupported", &unsupported_target) !=
         VFS_RESULT_OK) ||
        (vfs_mount_filesystem(&child_filesystem, &mountpoint_path) !=
         VFS_RESULT_OK) ||
        (vfs_resolve(NULL, "/mountpoint/mounted-file", &mounted_file) !=
         VFS_RESULT_OK) ||
        (mounted_file.node != &child_mounted_file)) {
        vfs_test_fail("mount-enter");
    }
    vfs_test_pass("mount-enter");

    if ((vfs_resolve(NULL, "/mountpoint/../alpha/item", &temporary) !=
         VFS_RESULT_OK) ||
        (temporary.node != &root_alpha_item) ||
        (vfs_path_release(&temporary) != VFS_RESULT_OK)) {
        vfs_test_fail("mount-leave");
    }
    vfs_test_pass("mount-leave");

    if ((vfs_mount_filesystem(&child_filesystem, &mountpoint_path) !=
         VFS_RESULT_ALREADY_MOUNTED) ||
        (vfs_mount_filesystem(&root_filesystem, &mountpoint_path) !=
         VFS_RESULT_MOUNT_CONFLICT) ||
        (vfs_mount_filesystem(&unsupported_filesystem, &mountpoint_path) !=
         VFS_RESULT_MOUNT_CONFLICT) ||
        (vfs_mount_filesystem(&unsupported_filesystem, &root_path) !=
         VFS_RESULT_MOUNT_CONFLICT) ||
        (vfs_mount_filesystem(&unsupported_filesystem,
                              &unsupported_target) != VFS_RESULT_OK)) {
        vfs_test_fail("mount-validation");
    }
    vfs_test_pass("mount-validation");

    lookup_before = root_backend.lookup_count;
    if ((vfs_resolve(NULL, "/alpha/item", &temporary) != VFS_RESULT_OK) ||
        (root_backend.lookup_count != (lookup_before + 2ULL)) ||
        (vfs_path_release(&temporary) != VFS_RESULT_OK)) {
        vfs_test_fail("lookup-dispatch");
    }
    vfs_test_pass("lookup-dispatch");

    if ((vfs_create_at(&root_path, "created", &created_path) !=
         VFS_RESULT_OK) || (root_backend.create_count != 1ULL) ||
        (created_path.node != &root_created_sentinel)) {
        vfs_test_fail("create-dispatch");
    }
    vfs_test_pass("create-dispatch");

    if ((vfs_mkdir_at(&root_path, "newdir", &mkdir_path) !=
         VFS_RESULT_OK) || (root_backend.mkdir_count != 1ULL) ||
        (mkdir_path.node != &root_mkdir_sentinel)) {
        vfs_test_fail("mkdir-dispatch");
    }
    vfs_test_pass("mkdir-dispatch");

    if ((vfs_unlink_at(&root_path, "oldfile") != VFS_RESULT_OK) ||
        (root_backend.unlink_count != 1ULL)) {
        vfs_test_fail("unlink-dispatch");
    }
    vfs_test_pass("unlink-dispatch");

    if ((vfs_rmdir_at(&root_path, "olddir") != VFS_RESULT_OK) ||
        (root_backend.rmdir_count != 1ULL)) {
        vfs_test_fail("rmdir-dispatch");
    }
    vfs_test_pass("rmdir-dispatch");

    rename_before = root_backend.rename_count;
    if ((vfs_rename_at(&root_path, "file", &root_path, "renamed") !=
         VFS_RESULT_OK) ||
        (root_backend.rename_count != (rename_before + 1ULL))) {
        vfs_test_fail("rename-dispatch");
    }
    vfs_test_pass("rename-dispatch");

    if ((vfs_resolve(NULL, "/mountpoint", &mounted_root) != VFS_RESULT_OK) ||
        (mounted_root.node != &child_root) ||
        (vfs_rename_at(&root_path, "file", &mounted_root, "other") !=
         VFS_RESULT_CROSS_FILESYSTEM) ||
        (root_backend.rename_count != (rename_before + 1ULL))) {
        vfs_test_fail("cross-fs-rename");
    }
    vfs_test_pass("cross-fs-rename");

    if (vfs_handle_open(&file_path, VFS_ACCESS_READ | VFS_ACCESS_WRITE,
                        &handle) != VFS_RESULT_OK) {
        vfs_test_fail("handle-open");
    }
    vfs_test_pass("handle-open");

    if ((vfs_handle_read(&handle, read_buffer, 3U, &transferred) !=
         VFS_RESULT_OK) || (transferred != 3U) ||
        (read_buffer[0] != 'V') || (read_buffer[1] != 'F') ||
        (read_buffer[2] != 'S') || (root_backend.read_count != 1ULL) ||
        (root_backend.last_read_offset != 0ULL)) {
        vfs_test_fail("read-dispatch");
    }
    vfs_test_pass("read-dispatch");

    if ((vfs_handle_write(&handle, write_buffer, sizeof(write_buffer),
                          &transferred) != VFS_RESULT_OK) ||
        (transferred != sizeof(write_buffer)) ||
        (root_backend.write_count != 1ULL) ||
        (root_backend.last_write_offset != 3ULL)) {
        vfs_test_fail("write-dispatch");
    }
    vfs_test_pass("write-dispatch");

    if ((vfs_handle_get_offset(&handle, &offset) != VFS_RESULT_OK) ||
        (offset != 5ULL)) {
        vfs_test_fail("handle-offset");
    }
    handle.offset = UINT64_MAX;
    if ((vfs_handle_write(&handle, write_buffer, 1U, &transferred) !=
         VFS_RESULT_OVERFLOW) ||
        (vfs_handle_read(&handle, read_buffer,
                         (size_t)VFS_IO_MAX + 1U, &transferred) !=
         VFS_RESULT_INVALID_ARGUMENT) ||
        (vfs_handle_read(&invalid_handle, read_buffer, 1U, &transferred) !=
         VFS_RESULT_INVALID_ARGUMENT)) {
        vfs_test_fail("handle-offset");
    }
    handle.offset = 5ULL;
    vfs_test_pass("handle-offset");

    if ((vfs_truncate_path(&file_path, 17ULL) != VFS_RESULT_OK) ||
        (root_backend.truncate_count != 1ULL) ||
        (root_backend.last_truncate_size != 17ULL)) {
        vfs_test_fail("truncate-dispatch");
    }
    vfs_test_pass("truncate-dispatch");

    if ((vfs_readdir_path(&root_path, 0ULL, &entry) != VFS_RESULT_OK) ||
        (root_backend.readdir_count != 1ULL) ||
        (entry.node_id != root_alpha.id) ||
        (entry.type != VFS_NODE_DIRECTORY) ||
        (entry.name_length != 5U)) {
        vfs_test_fail("readdir-dispatch");
    }
    vfs_test_pass("readdir-dispatch");

    if ((vfs_resolve(NULL, "/corrupt", &temporary) !=
         VFS_RESULT_CORRUPT) ||
        (vfs_resolve(NULL, "/unsupported/no-read",
                     &unsupported_file_path) != VFS_RESULT_OK) ||
        (vfs_handle_open(&unsupported_file_path, VFS_ACCESS_READ,
                         &unsupported_handle) != VFS_RESULT_OK) ||
        (vfs_handle_read(&unsupported_handle, read_buffer, 1U,
                         &transferred) != VFS_RESULT_NOT_SUPPORTED)) {
        vfs_test_fail("backend-validation");
    }
    vfs_test_pass("backend-validation");

    if ((vfs_handle_close(&unsupported_handle) != VFS_RESULT_OK) ||
        (vfs_handle_close(&handle) != VFS_RESULT_OK) ||
        (vfs_path_release(&created_path) != VFS_RESULT_OK) ||
        (vfs_path_release(&mkdir_path) != VFS_RESULT_OK) ||
        (vfs_path_release(&mounted_file) != VFS_RESULT_OK) ||
        (vfs_path_release(&mounted_root) != VFS_RESULT_OK) ||
        (vfs_path_release(&unsupported_file_path) != VFS_RESULT_OK) ||
        !process_mark_finished(process_a) ||
        !process_mark_finished(process_b) ||
        !process_destroy(process_a) || !process_destroy(process_b) ||
        (vfs_path_release(&alpha_path) != VFS_RESULT_OK) ||
        (vfs_path_release(&beta_path) != VFS_RESULT_OK) ||
        (vfs_path_release(&file_path) != VFS_RESULT_OK) ||
        (vfs_path_release(&mountpoint_path) != VFS_RESULT_OK) ||
        (vfs_path_release(&unsupported_target) != VFS_RESULT_OK) ||
        (vfs_path_release(&root_path) != VFS_RESULT_OK) ||
        !vfs_get_stats(&vfs_stats) ||
        (vfs_stats.mount_count != 3ULL) ||
        (vfs_stats.path_reference_count != 0ULL) ||
        (vfs_stats.open_handle_count != 0ULL) ||
        (vfs_shutdown() != VFS_RESULT_OK) || vfs_is_initialized() ||
        !vfs_test_all_references_released() ||
        !process_get_stats(&process_final) ||
        (process_final.active_processes != 0ULL) ||
        !pmm_get_stats(&pmm_after) || !heap_get_stats(&heap_after) ||
        (pmm_after.free_frames != pmm_before.free_frames) ||
        (pmm_after.usable_frames != pmm_before.usable_frames) ||
        (heap_after.allocation_count != heap_before.allocation_count) ||
        (heap_after.used_bytes != heap_before.used_bytes)) {
        vfs_test_fail("cleanup");
    }
    vfs_test_pass("cleanup");

    serial_write_string("VFS path max: 1024\n");
    serial_write_string("VFS name max: 255\n");
    serial_write_string("VFS mount slots: 8\n");
    serial_write_string("VFS I/O max: 4096\n");
    serial_write_string("VFS PMM free frames restored: ");
    serial_write_u64(pmm_after.free_frames);
    serial_write_string("\nBoringKernel VFS core test passed.\n");
    x86_64_halt_forever();
}
