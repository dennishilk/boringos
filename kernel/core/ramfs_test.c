#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/heap.h>
#include <boring/process.h>
#include <boring/ramfs.h>
#include <boring/ramfs_test.h>
#include <boring/serial.h>
#include <boring/vfs.h>

static uint8_t ramfs_test_io[VFS_IO_MAX];
static uint8_t ramfs_test_verify[VFS_IO_MAX];

static const uint8_t ramfs_roundtrip_pattern[] = {
    0x42U, 0x00U, 0x7fU, 0xa5U, 0x11U, 0x22U, 0x33U, 0x44U,
    0xfeU, 0x10U, 0x20U, 0x30U, 0x99U, 0x5aU, 0xc3U, 0x18U
};

static void ramfs_test_fail(const char *check) __attribute__((noreturn));
static void ramfs_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("RAMFS test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static void ramfs_test_pass(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": PASS\n");
}

static bool ramfs_test_bytes_equal(const uint8_t *left,
                                   const uint8_t *right,
                                   size_t length) {
    size_t index;

    if ((left == NULL) || (right == NULL)) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

static void ramfs_test_copy(uint8_t *destination,
                            const uint8_t *source,
                            size_t length) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        destination[index] = source[index];
    }
}

static bool ramfs_test_dirent_name(const struct vfs_dirent *entry,
                                   const char *name,
                                   size_t length) {
    size_t index;

    if ((entry == NULL) || (name == NULL) ||
        (entry->name_length != length)) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        if (entry->name[index] != name[index]) {
            return false;
        }
    }
    return entry->name[length] == '\0';
}

static void ramfs_test_release(struct vfs_path *path, const char *check) {
    if ((path == NULL) || (path->mount == NULL) || (path->node == NULL) ||
        (vfs_path_release(path) != VFS_RESULT_OK)) {
        ramfs_test_fail(check);
    }
}

static void ramfs_test_close(struct vfs_handle *handle, const char *check) {
    if ((handle == NULL) || !handle->open ||
        (vfs_handle_close(handle) != VFS_RESULT_OK)) {
        ramfs_test_fail(check);
    }
}

static void ramfs_test_write_path(const struct vfs_path *path,
                                  const uint8_t *data,
                                  size_t length,
                                  const char *check) {
    struct vfs_handle handle = { { NULL, NULL }, 0ULL, 0U, false };
    size_t transferred = 0U;

    if ((vfs_handle_open(path, VFS_ACCESS_WRITE, &handle) != VFS_RESULT_OK) ||
        (vfs_handle_write(&handle, data, length, &transferred) !=
         VFS_RESULT_OK) ||
        (transferred != length)) {
        ramfs_test_fail(check);
    }
    ramfs_test_close(&handle, check);
}

static bool ramfs_test_read_path(const struct vfs_path *path,
                                 uint8_t *buffer,
                                 size_t length,
                                 size_t *transferred_out) {
    struct vfs_handle handle = { { NULL, NULL }, 0ULL, 0U, false };
    enum vfs_result result;

    if ((buffer == NULL) || (transferred_out == NULL) ||
        (vfs_handle_open(path, VFS_ACCESS_READ, &handle) != VFS_RESULT_OK)) {
        return false;
    }
    result = vfs_handle_read(&handle, buffer, length, transferred_out);
    if (vfs_handle_close(&handle) != VFS_RESULT_OK) {
        return false;
    }
    return result == VFS_RESULT_OK;
}

void ramfs_test_run(void) {
    struct ramfs *root_ramfs = NULL;
    struct ramfs *child_ramfs = NULL;
    struct vfs_filesystem *root_fs;
    struct vfs_filesystem *child_fs;
    struct vfs_path root = { NULL, NULL };
    struct vfs_path temporary = { NULL, NULL };
    struct vfs_path etc = { NULL, NULL };
    struct vfs_path deep = { NULL, NULL };
    struct vfs_path level1 = { NULL, NULL };
    struct vfs_path level2 = { NULL, NULL };
    struct vfs_path empty_file = { NULL, NULL };
    struct vfs_path data_file = { NULL, NULL };
    struct vfs_path gap_file = { NULL, NULL };
    struct vfs_path truncate_file = { NULL, NULL };
    struct vfs_path limit_file = { NULL, NULL };
    struct vfs_path from_dir = { NULL, NULL };
    struct vfs_path to_dir = { NULL, NULL };
    struct vfs_path moving_file = { NULL, NULL };
    struct vfs_path old_dir = { NULL, NULL };
    struct vfs_path old_dir_child = { NULL, NULL };
    struct vfs_path tree_a = { NULL, NULL };
    struct vfs_path tree_b = { NULL, NULL };
    struct vfs_path inner = { NULL, NULL };
    struct vfs_path leaf = { NULL, NULL };
    struct vfs_path cycle = { NULL, NULL };
    struct vfs_path cycle_sub = { NULL, NULL };
    struct vfs_path busy = { NULL, NULL };
    struct vfs_path nonempty = { NULL, NULL };
    struct vfs_path nonempty_child = { NULL, NULL };
    struct vfs_path home = { NULL, NULL };
    struct vfs_path home_a = { NULL, NULL };
    struct vfs_path home_b = { NULL, NULL };
    struct vfs_path item_a = { NULL, NULL };
    struct vfs_path item_b = { NULL, NULL };
    struct vfs_path mountpoint = { NULL, NULL };
    struct vfs_path child_root = { NULL, NULL };
    struct vfs_path child_file = { NULL, NULL };
    struct vfs_handle handle = { { NULL, NULL }, 0ULL, 0U, false };
    struct vfs_dirent entry;
    struct ramfs_stats stats;
    struct heap_stats heap_before;
    struct heap_stats heap_after;
    struct vfs_stats vfs_stats;
    struct process *process_a = NULL;
    struct process *process_b = NULL;
    struct vfs_node *backend_node = NULL;
    static const uint8_t overwrite_bytes[3] = { 0xd1U, 0xd2U, 0xd3U };
    static const uint8_t truncate_bytes[6] = {
        'a', 'b', 'c', 'd', 'e', 'f'
    };
    static const uint8_t rename_bytes[5] = { 'r', 'e', 'n', 'a', 'm' };
    static const uint8_t moving_bytes[4] = { 'm', 'o', 'v', 'e' };
    static const uint8_t cwd_a_bytes[3] = { 'A', 'A', 'A' };
    static const uint8_t cwd_b_bytes[3] = { 'B', 'B', 'B' };
    static const uint8_t child_bytes[5] = { 'c', 'h', 'i', 'l', 'd' };
    uint8_t expected_roundtrip[sizeof(ramfs_roundtrip_pattern)];
    size_t transferred;
    size_t index;
    uint64_t stable_id;

    serial_write_string("RAMFS backend:\n");

    if (!process_init() || !heap_get_stats(&heap_before) ||
        (ramfs_create_filesystem(100ULL, &root_ramfs) != VFS_RESULT_OK) ||
        (root_ramfs == NULL)) {
        ramfs_test_fail("filesystem-create");
    }
    ramfs_test_pass("filesystem-create");

    root_fs = ramfs_get_vfs(root_ramfs);
    if ((root_fs == NULL) || (root_fs->root == NULL) ||
        !root_fs->root->valid || (root_fs->root->id == 0ULL) ||
        (root_fs->root->type != VFS_NODE_DIRECTORY) ||
        (root_fs->root->parent != root_fs->root) ||
        (ramfs_get_stats(root_ramfs, &stats) != VFS_RESULT_OK) ||
        (stats.live_nodes != 1ULL) || (stats.live_directories != 1ULL) ||
        (stats.live_files != 0ULL)) {
        ramfs_test_fail("root-create");
    }
    ramfs_test_pass("root-create");

    if ((vfs_init(root_fs) != VFS_RESULT_OK) ||
        (vfs_get_root(&root) != VFS_RESULT_OK) ||
        (ramfs_destroy_filesystem(root_ramfs) != VFS_RESULT_BUSY)) {
        ramfs_test_fail("root-vfs-init");
    }
    ramfs_test_pass("root-vfs-init");

    serial_write_string("Directory semantics:\n");
    if ((vfs_mkdir_at(&root, "etc", &etc) != VFS_RESULT_OK) ||
        (vfs_mkdir_at(&root, "deep", &deep) != VFS_RESULT_OK)) {
        ramfs_test_fail("mkdir");
    }
    ramfs_test_pass("mkdir");

    if ((vfs_mkdir_at(&deep, "level1", &level1) != VFS_RESULT_OK) ||
        (vfs_mkdir_at(&level1, "level2", &level2) != VFS_RESULT_OK)) {
        ramfs_test_fail("nested-mkdir");
    }
    ramfs_test_pass("nested-mkdir");

    if ((vfs_mkdir_at(&root, "etc", &temporary) !=
         VFS_RESULT_ALREADY_EXISTS) ||
        (temporary.mount != NULL) || (temporary.node != NULL)) {
        ramfs_test_fail("duplicate-name-reject");
    }
    ramfs_test_pass("duplicate-name-reject");

    if ((vfs_resolve(NULL, "/deep/level1/level2", &temporary) !=
         VFS_RESULT_OK) || !vfs_path_equal(&temporary, &level2)) {
        ramfs_test_fail("nested-resolve");
    }
    ramfs_test_release(&temporary, "nested-resolve");
    ramfs_test_pass("nested-resolve");
    ramfs_test_release(&level2, "nested-mkdir");
    ramfs_test_release(&level1, "nested-mkdir");
    ramfs_test_release(&deep, "nested-mkdir");

    serial_write_string("File semantics:\n");
    if (vfs_create_at(&root, "data", &data_file) != VFS_RESULT_OK) {
        ramfs_test_fail("create-file");
    }
    ramfs_test_pass("create-file");

    if ((vfs_create_at(&root, "empty", &empty_file) != VFS_RESULT_OK) ||
        !ramfs_test_read_path(&empty_file, ramfs_test_verify, 1U,
                              &transferred) ||
        (transferred != 0U)) {
        ramfs_test_fail("zero-length-create");
    }
    ramfs_test_pass("zero-length-create");

    ramfs_test_write_path(&data_file, ramfs_roundtrip_pattern,
                          sizeof(ramfs_roundtrip_pattern),
                          "write-roundtrip");
    transferred = 0U;
    if (!ramfs_test_read_path(&data_file, ramfs_test_verify,
                              sizeof(ramfs_roundtrip_pattern),
                              &transferred) ||
        (transferred != sizeof(ramfs_roundtrip_pattern)) ||
        !ramfs_test_bytes_equal(ramfs_test_verify, ramfs_roundtrip_pattern,
                                sizeof(ramfs_roundtrip_pattern))) {
        ramfs_test_fail("write-roundtrip");
    }
    ramfs_test_pass("write-roundtrip");

    ramfs_test_copy(expected_roundtrip, ramfs_roundtrip_pattern,
                    sizeof(ramfs_roundtrip_pattern));
    expected_roundtrip[4] = overwrite_bytes[0];
    expected_roundtrip[5] = overwrite_bytes[1];
    expected_roundtrip[6] = overwrite_bytes[2];
    transferred = 0U;
    if ((vfs_handle_open(&data_file,
                         VFS_ACCESS_READ | VFS_ACCESS_WRITE,
                         &handle) != VFS_RESULT_OK) ||
        (vfs_handle_read(&handle, ramfs_test_verify, 4U, &transferred) !=
         VFS_RESULT_OK) || (transferred != 4U) ||
        (vfs_handle_write(&handle, overwrite_bytes,
                          sizeof(overwrite_bytes), &transferred) !=
         VFS_RESULT_OK) || (transferred != sizeof(overwrite_bytes))) {
        ramfs_test_fail("overwrite");
    }
    ramfs_test_close(&handle, "overwrite");
    transferred = 0U;
    if (!ramfs_test_read_path(&data_file, ramfs_test_verify,
                              sizeof(expected_roundtrip), &transferred) ||
        (transferred != sizeof(expected_roundtrip)) ||
        !ramfs_test_bytes_equal(ramfs_test_verify, expected_roundtrip,
                                sizeof(expected_roundtrip))) {
        ramfs_test_fail("overwrite");
    }
    ramfs_test_pass("overwrite");

    if (vfs_create_at(&root, "gap", &gap_file) != VFS_RESULT_OK) {
        ramfs_test_fail("write-past-eof-zero-gap");
    }
    transferred = 0U;
    ramfs_test_io[0] = 'Z';
    if ((root_fs->operations->write(root_fs, gap_file.node, 5ULL,
                                    ramfs_test_io, 1U, &transferred) !=
         VFS_RESULT_OK) || (transferred != 1U) ||
        !ramfs_test_read_path(&gap_file, ramfs_test_verify, 6U,
                              &transferred) || (transferred != 6U) ||
        (ramfs_test_verify[0] != 0U) || (ramfs_test_verify[1] != 0U) ||
        (ramfs_test_verify[2] != 0U) || (ramfs_test_verify[3] != 0U) ||
        (ramfs_test_verify[4] != 0U) || (ramfs_test_verify[5] != 'Z')) {
        ramfs_test_fail("write-past-eof-zero-gap");
    }
    ramfs_test_pass("write-past-eof-zero-gap");

    transferred = 0U;
    if ((vfs_handle_open(&data_file, VFS_ACCESS_READ, &handle) !=
         VFS_RESULT_OK) ||
        (vfs_handle_read(&handle, ramfs_test_verify, 5U, &transferred) !=
         VFS_RESULT_OK) || (transferred != 5U) ||
        !ramfs_test_bytes_equal(ramfs_test_verify, expected_roundtrip, 5U)) {
        ramfs_test_fail("partial-read");
    }
    ramfs_test_close(&handle, "partial-read");
    ramfs_test_pass("partial-read");

    transferred = 0U;
    if ((vfs_handle_open(&data_file, VFS_ACCESS_READ, &handle) !=
         VFS_RESULT_OK) ||
        (vfs_handle_read(&handle, ramfs_test_verify,
                         sizeof(expected_roundtrip), &transferred) !=
         VFS_RESULT_OK) || (transferred != sizeof(expected_roundtrip)) ||
        (vfs_handle_read(&handle, ramfs_test_verify, 1U, &transferred) !=
         VFS_RESULT_OK) || (transferred != 0U) ||
        (root_fs->operations->read(root_fs, data_file.node, 100ULL,
                                   ramfs_test_verify, 1U, &transferred) !=
         VFS_RESULT_OK) || (transferred != 0U)) {
        ramfs_test_fail("eof-read");
    }
    ramfs_test_close(&handle, "eof-read");
    ramfs_test_pass("eof-read");

    if (vfs_create_at(&root, "limit", &limit_file) != VFS_RESULT_OK) {
        ramfs_test_fail("file-size-bound");
    }
    for (index = 0U; index < (size_t)VFS_IO_MAX; ++index) {
        ramfs_test_io[index] = (uint8_t)(index & 0xffU);
    }
    if (vfs_handle_open(&limit_file, VFS_ACCESS_WRITE, &handle) !=
        VFS_RESULT_OK) {
        ramfs_test_fail("file-size-bound");
    }
    transferred = 0U;
    if ((vfs_handle_write(&handle, ramfs_test_io, (size_t)VFS_IO_MAX,
                          &transferred) != VFS_RESULT_OK) ||
        (transferred != (size_t)VFS_IO_MAX) ||
        (vfs_handle_write(&handle, ramfs_test_io, (size_t)VFS_IO_MAX,
                          &transferred) != VFS_RESULT_OK) ||
        (transferred != (size_t)VFS_IO_MAX)) {
        ramfs_test_fail("file-size-bound");
    }
    transferred = 99U;
    if ((vfs_handle_write(&handle, ramfs_test_io, 1U, &transferred) !=
         VFS_RESULT_NO_SPACE) || (transferred != 0U)) {
        ramfs_test_fail("file-size-bound");
    }
    ramfs_test_close(&handle, "file-size-bound");
    ramfs_test_pass("file-size-bound");

    transferred = 0U;
    if (!ramfs_test_read_path(&limit_file, ramfs_test_verify,
                              (size_t)VFS_IO_MAX, &transferred) ||
        (transferred != (size_t)VFS_IO_MAX) ||
        !ramfs_test_bytes_equal(ramfs_test_verify, ramfs_test_io,
                                (size_t)VFS_IO_MAX)) {
        ramfs_test_fail("failed-growth-preserves-data");
    }
    ramfs_test_pass("failed-growth-preserves-data");
    if (vfs_truncate_path(&limit_file, 0ULL) != VFS_RESULT_OK) {
        ramfs_test_fail("file-size-bound");
    }

    serial_write_string("Truncate:\n");
    if (vfs_create_at(&root, "truncate", &truncate_file) != VFS_RESULT_OK) {
        ramfs_test_fail("truncate-shrink");
    }
    ramfs_test_write_path(&truncate_file, truncate_bytes,
                          sizeof(truncate_bytes), "truncate-shrink");
    if ((vfs_truncate_path(&truncate_file, 3ULL) != VFS_RESULT_OK) ||
        !ramfs_test_read_path(&truncate_file, ramfs_test_verify, 6U,
                              &transferred) || (transferred != 3U) ||
        !ramfs_test_bytes_equal(ramfs_test_verify, truncate_bytes, 3U)) {
        ramfs_test_fail("truncate-shrink");
    }
    ramfs_test_pass("truncate-shrink");

    if ((vfs_truncate_path(&truncate_file, 6ULL) != VFS_RESULT_OK) ||
        !ramfs_test_read_path(&truncate_file, ramfs_test_verify, 6U,
                              &transferred) || (transferred != 6U) ||
        (ramfs_test_verify[0] != 'a') || (ramfs_test_verify[1] != 'b') ||
        (ramfs_test_verify[2] != 'c')) {
        ramfs_test_fail("truncate-grow");
    }
    ramfs_test_pass("truncate-grow");
    if ((ramfs_test_verify[3] != 0U) || (ramfs_test_verify[4] != 0U) ||
        (ramfs_test_verify[5] != 0U)) {
        ramfs_test_fail("truncate-grow-zero-fill");
    }
    ramfs_test_pass("truncate-grow-zero-fill");

    if ((vfs_truncate_path(&truncate_file, 0ULL) != VFS_RESULT_OK) ||
        !ramfs_test_read_path(&truncate_file, ramfs_test_verify, 1U,
                              &transferred) || (transferred != 0U)) {
        ramfs_test_fail("truncate-zero");
    }
    ramfs_test_pass("truncate-zero");
    if (vfs_truncate_path(&truncate_file,
                          (uint64_t)RAMFS_FILE_MAX + 1ULL) !=
        VFS_RESULT_NO_SPACE) {
        ramfs_test_fail("truncate-bound-reject");
    }
    ramfs_test_pass("truncate-bound-reject");

    serial_write_string("Directory enumeration:\n");
    if ((vfs_create_at(&etc, "alpha", &temporary) != VFS_RESULT_OK)) {
        ramfs_test_fail("readdir-live-state");
    }
    ramfs_test_release(&temporary, "readdir-live-state");
    if ((vfs_create_at(&etc, "beta", &temporary) != VFS_RESULT_OK)) {
        ramfs_test_fail("readdir-live-state");
    }
    ramfs_test_release(&temporary, "readdir-live-state");
    if ((vfs_readdir_path(&etc, 0ULL, &entry) != VFS_RESULT_OK) ||
        !ramfs_test_dirent_name(&entry, "alpha", 5U) ||
        (vfs_readdir_path(&etc, 1ULL, &entry) != VFS_RESULT_OK) ||
        !ramfs_test_dirent_name(&entry, "beta", 4U)) {
        ramfs_test_fail("readdir-live-state");
    }
    ramfs_test_pass("readdir-live-state");

    if ((vfs_rename_at(&etc, "beta", &etc, "gamma") != VFS_RESULT_OK) ||
        (vfs_readdir_path(&etc, 0ULL, &entry) != VFS_RESULT_OK) ||
        !ramfs_test_dirent_name(&entry, "alpha", 5U) ||
        (vfs_readdir_path(&etc, 1ULL, &entry) != VFS_RESULT_OK) ||
        !ramfs_test_dirent_name(&entry, "gamma", 5U) ||
        (vfs_resolve(NULL, "/etc/beta", &temporary) !=
         VFS_RESULT_NOT_FOUND)) {
        ramfs_test_fail("readdir-after-rename");
    }
    ramfs_test_pass("readdir-after-rename");

    if ((vfs_unlink_at(&etc, "alpha") != VFS_RESULT_OK) ||
        (vfs_readdir_path(&etc, 0ULL, &entry) != VFS_RESULT_OK) ||
        !ramfs_test_dirent_name(&entry, "gamma", 5U)) {
        ramfs_test_fail("readdir-after-unlink");
    }
    ramfs_test_pass("readdir-after-unlink");
    if (vfs_readdir_path(&etc, 1ULL, &entry) != VFS_RESULT_NOT_FOUND) {
        ramfs_test_fail("readdir-end");
    }
    ramfs_test_pass("readdir-end");

    serial_write_string("Rename:\n");
    if (vfs_create_at(&root, "rename-old", &temporary) != VFS_RESULT_OK) {
        ramfs_test_fail("rename-file");
    }
    stable_id = temporary.node->id;
    ramfs_test_write_path(&temporary, rename_bytes, sizeof(rename_bytes),
                          "rename-file");
    ramfs_test_release(&temporary, "rename-file");
    if ((vfs_rename_at(&root, "rename-old", &root, "rename-new") !=
         VFS_RESULT_OK) ||
        (vfs_resolve(NULL, "/rename-old", &temporary) !=
         VFS_RESULT_NOT_FOUND) ||
        (vfs_resolve(NULL, "/rename-new", &temporary) != VFS_RESULT_OK) ||
        (temporary.node->id != stable_id)) {
        ramfs_test_fail("rename-file");
    }
    ramfs_test_pass("rename-file");
    if (!ramfs_test_read_path(&temporary, ramfs_test_verify,
                              sizeof(rename_bytes), &transferred) ||
        (transferred != sizeof(rename_bytes)) ||
        !ramfs_test_bytes_equal(ramfs_test_verify, rename_bytes,
                                sizeof(rename_bytes))) {
        ramfs_test_fail("preserve-file-content");
    }
    ramfs_test_pass("preserve-file-content");
    ramfs_test_release(&temporary, "rename-file");

    if ((vfs_mkdir_at(&root, "from", &from_dir) != VFS_RESULT_OK) ||
        (vfs_mkdir_at(&root, "to", &to_dir) != VFS_RESULT_OK) ||
        (vfs_create_at(&from_dir, "moving", &moving_file) != VFS_RESULT_OK)) {
        ramfs_test_fail("move-file");
    }
    stable_id = moving_file.node->id;
    ramfs_test_write_path(&moving_file, moving_bytes, sizeof(moving_bytes),
                          "move-file");
    ramfs_test_release(&moving_file, "move-file");
    if ((vfs_rename_at(&from_dir, "moving", &to_dir, "moved") !=
         VFS_RESULT_OK) ||
        (vfs_resolve(NULL, "/from/moving", &temporary) !=
         VFS_RESULT_NOT_FOUND) ||
        (vfs_resolve(NULL, "/to/moved", &temporary) != VFS_RESULT_OK) ||
        (temporary.node->id != stable_id) ||
        !ramfs_test_read_path(&temporary, ramfs_test_verify,
                              sizeof(moving_bytes), &transferred) ||
        (transferred != sizeof(moving_bytes)) ||
        !ramfs_test_bytes_equal(ramfs_test_verify, moving_bytes,
                                sizeof(moving_bytes))) {
        ramfs_test_fail("move-file");
    }
    ramfs_test_release(&temporary, "move-file");
    ramfs_test_pass("move-file");

    if ((vfs_mkdir_at(&root, "dir-old", &old_dir) != VFS_RESULT_OK) ||
        (vfs_create_at(&old_dir, "child", &old_dir_child) != VFS_RESULT_OK)) {
        ramfs_test_fail("rename-directory");
    }
    stable_id = old_dir.node->id;
    ramfs_test_release(&old_dir_child, "rename-directory");
    if ((vfs_rename_at(&root, "dir-old", &root, "dir-new") !=
         VFS_RESULT_OK) ||
        (vfs_resolve(NULL, "/dir-old", &temporary) !=
         VFS_RESULT_NOT_FOUND) ||
        (vfs_resolve(NULL, "/dir-new/child", &temporary) != VFS_RESULT_OK) ||
        (old_dir.node->id != stable_id)) {
        ramfs_test_fail("rename-directory");
    }
    ramfs_test_release(&temporary, "rename-directory");
    ramfs_test_pass("rename-directory");
    ramfs_test_release(&old_dir, "rename-directory");

    if ((vfs_mkdir_at(&root, "tree-a", &tree_a) != VFS_RESULT_OK) ||
        (vfs_mkdir_at(&root, "tree-b", &tree_b) != VFS_RESULT_OK) ||
        (vfs_mkdir_at(&tree_a, "inner", &inner) != VFS_RESULT_OK) ||
        (vfs_create_at(&inner, "leaf", &leaf) != VFS_RESULT_OK)) {
        ramfs_test_fail("move-directory");
    }
    stable_id = inner.node->id;
    ramfs_test_release(&leaf, "move-directory");
    if ((vfs_rename_at(&tree_a, "inner", &tree_b, "moved") !=
         VFS_RESULT_OK) ||
        (vfs_resolve(NULL, "/tree-a/inner", &temporary) !=
         VFS_RESULT_NOT_FOUND) ||
        (vfs_resolve(NULL, "/tree-b/moved/leaf", &temporary) !=
         VFS_RESULT_OK) ||
        (inner.node->id != stable_id)) {
        ramfs_test_fail("move-directory");
    }
    ramfs_test_release(&temporary, "move-directory");
    ramfs_test_pass("move-directory");
    ramfs_test_release(&inner, "move-directory");
    ramfs_test_release(&tree_a, "move-directory");
    ramfs_test_release(&tree_b, "move-directory");

    if ((vfs_mkdir_at(&root, "cycle", &cycle) != VFS_RESULT_OK) ||
        (vfs_mkdir_at(&cycle, "sub", &cycle_sub) != VFS_RESULT_OK) ||
        (vfs_rename_at(&root, "cycle", &cycle_sub, "cycle") !=
         VFS_RESULT_INVALID_ARGUMENT)) {
        ramfs_test_fail("descendant-cycle-reject");
    }
    ramfs_test_pass("descendant-cycle-reject");
    ramfs_test_release(&cycle_sub, "descendant-cycle-reject");
    ramfs_test_release(&cycle, "descendant-cycle-reject");

    if (vfs_create_at(&root, "collision-a", &temporary) != VFS_RESULT_OK) {
        ramfs_test_fail("collision-reject");
    }
    ramfs_test_release(&temporary, "collision-reject");
    if (vfs_create_at(&root, "collision-b", &temporary) != VFS_RESULT_OK) {
        ramfs_test_fail("collision-reject");
    }
    ramfs_test_release(&temporary, "collision-reject");
    if (vfs_rename_at(&root, "collision-a", &root, "collision-b") !=
        VFS_RESULT_ALREADY_EXISTS) {
        ramfs_test_fail("collision-reject");
    }
    ramfs_test_pass("collision-reject");
    if ((vfs_unlink_at(&root, "collision-a") != VFS_RESULT_OK) ||
        (vfs_unlink_at(&root, "collision-b") != VFS_RESULT_OK)) {
        ramfs_test_fail("collision-reject");
    }

    serial_write_string("Removal:\n");
    if (vfs_create_at(&root, "remove-me", &temporary) != VFS_RESULT_OK) {
        ramfs_test_fail("unlink-regular");
    }
    ramfs_test_release(&temporary, "unlink-regular");
    if ((vfs_unlink_at(&root, "remove-me") != VFS_RESULT_OK) ||
        (vfs_resolve(NULL, "/remove-me", &temporary) !=
         VFS_RESULT_NOT_FOUND)) {
        ramfs_test_fail("unlink-regular");
    }
    ramfs_test_pass("unlink-regular");

    if ((vfs_mkdir_at(&root, "unlink-dir", &temporary) != VFS_RESULT_OK) ||
        (vfs_unlink_at(&root, "unlink-dir") != VFS_RESULT_NOT_REGULAR)) {
        ramfs_test_fail("unlink-directory-reject");
    }
    ramfs_test_pass("unlink-directory-reject");
    ramfs_test_release(&temporary, "unlink-directory-reject");
    if (vfs_rmdir_at(&root, "unlink-dir") != VFS_RESULT_OK) {
        ramfs_test_fail("unlink-directory-reject");
    }

    if ((vfs_create_at(&root, "busy", &busy) != VFS_RESULT_OK) ||
        (vfs_unlink_at(&root, "busy") != VFS_RESULT_BUSY)) {
        ramfs_test_fail("unlink-busy-reject");
    }
    ramfs_test_pass("unlink-busy-reject");
    ramfs_test_release(&busy, "unlink-busy-reject");
    if ((vfs_unlink_at(&root, "busy") != VFS_RESULT_OK) ||
        (vfs_resolve(NULL, "/busy", &temporary) != VFS_RESULT_NOT_FOUND)) {
        ramfs_test_fail("unlink-busy-reject");
    }

    if ((vfs_mkdir_at(&root, "nonempty", &nonempty) != VFS_RESULT_OK) ||
        (vfs_create_at(&nonempty, "child", &nonempty_child) != VFS_RESULT_OK)) {
        ramfs_test_fail("rmdir-nonempty-reject");
    }
    ramfs_test_release(&nonempty_child, "rmdir-nonempty-reject");
    if (vfs_rmdir_at(&root, "nonempty") != VFS_RESULT_NOT_EMPTY) {
        ramfs_test_fail("rmdir-nonempty-reject");
    }
    ramfs_test_pass("rmdir-nonempty-reject");
    if (vfs_unlink_at(&nonempty, "child") != VFS_RESULT_OK) {
        ramfs_test_fail("rmdir-nonempty-reject");
    }
    ramfs_test_release(&nonempty, "rmdir-nonempty-reject");
    if (vfs_rmdir_at(&root, "nonempty") != VFS_RESULT_OK) {
        ramfs_test_fail("rmdir-empty");
    }
    ramfs_test_pass("rmdir-empty");

    if (vfs_mkdir_at(&root, "empty-dir", &temporary) != VFS_RESULT_OK) {
        ramfs_test_fail("rmdir-empty");
    }
    ramfs_test_release(&temporary, "rmdir-empty");
    if (vfs_rmdir_at(&root, "empty-dir") != VFS_RESULT_OK) {
        ramfs_test_fail("rmdir-empty");
    }

    if (vfs_create_at(&root, "reuse", &temporary) != VFS_RESULT_OK) {
        ramfs_test_fail("name-reuse");
    }
    ramfs_test_release(&temporary, "name-reuse");
    if ((vfs_unlink_at(&root, "reuse") != VFS_RESULT_OK) ||
        (vfs_create_at(&root, "reuse", &temporary) != VFS_RESULT_OK)) {
        ramfs_test_fail("name-reuse");
    }
    ramfs_test_release(&temporary, "name-reuse");
    if (vfs_unlink_at(&root, "reuse") != VFS_RESULT_OK) {
        ramfs_test_fail("name-reuse");
    }
    ramfs_test_pass("name-reuse");

    serial_write_string("CWD:\n");
    if ((vfs_mkdir_at(&root, "home", &home) != VFS_RESULT_OK) ||
        (vfs_mkdir_at(&home, "a", &home_a) != VFS_RESULT_OK) ||
        (vfs_mkdir_at(&home, "b", &home_b) != VFS_RESULT_OK) ||
        (vfs_create_at(&home_a, "item", &item_a) != VFS_RESULT_OK) ||
        (vfs_create_at(&home_b, "item", &item_b) != VFS_RESULT_OK)) {
        ramfs_test_fail("process-a-cwd");
    }
    ramfs_test_write_path(&item_a, cwd_a_bytes, sizeof(cwd_a_bytes),
                          "process-a-cwd");
    ramfs_test_write_path(&item_b, cwd_b_bytes, sizeof(cwd_b_bytes),
                          "process-b-cwd");
    ramfs_test_release(&item_a, "process-a-cwd");
    ramfs_test_release(&item_b, "process-b-cwd");
    if (!process_create(&process_a) || !process_set_cwd(process_a, &home_a)) {
        ramfs_test_fail("process-a-cwd");
    }
    ramfs_test_pass("process-a-cwd");
    if (!process_create(&process_b) || !process_set_cwd(process_b, &home_b)) {
        ramfs_test_fail("process-b-cwd");
    }
    ramfs_test_pass("process-b-cwd");

    if ((vfs_resolve_process(process_a, "item", &temporary) !=
         VFS_RESULT_OK) ||
        !ramfs_test_read_path(&temporary, ramfs_test_verify,
                              sizeof(cwd_a_bytes), &transferred) ||
        (transferred != sizeof(cwd_a_bytes)) ||
        !ramfs_test_bytes_equal(ramfs_test_verify, cwd_a_bytes,
                                sizeof(cwd_a_bytes))) {
        ramfs_test_fail("process-relative-distinct");
    }
    stable_id = temporary.node->id;
    ramfs_test_release(&temporary, "process-relative-distinct");
    if ((vfs_resolve_process(process_b, "item", &temporary) !=
         VFS_RESULT_OK) || (temporary.node->id == stable_id)) {
        ramfs_test_fail("process-relative-distinct");
    }
    ramfs_test_pass("process-relative-distinct");
    if (!ramfs_test_read_path(&temporary, ramfs_test_verify,
                              sizeof(cwd_b_bytes), &transferred) ||
        (transferred != sizeof(cwd_b_bytes)) ||
        !ramfs_test_bytes_equal(ramfs_test_verify, cwd_b_bytes,
                                sizeof(cwd_b_bytes))) {
        ramfs_test_fail("process-relative-content");
    }
    ramfs_test_release(&temporary, "process-relative-content");
    ramfs_test_pass("process-relative-content");

    if (!process_clear_cwd(process_a) || !process_clear_cwd(process_b) ||
        !process_mark_finished(process_a) || !process_destroy(process_a) ||
        !process_mark_finished(process_b) || !process_destroy(process_b)) {
        ramfs_test_fail("process-cwd-release");
    }
    process_a = NULL;
    process_b = NULL;
    ramfs_test_pass("process-cwd-release");
    ramfs_test_release(&home_a, "process-cwd-release");
    ramfs_test_release(&home_b, "process-cwd-release");
    ramfs_test_release(&home, "process-cwd-release");

    serial_write_string("Mount:\n");
    if ((ramfs_create_filesystem(200ULL, &child_ramfs) != VFS_RESULT_OK) ||
        (child_ramfs == NULL)) {
        ramfs_test_fail("second-ramfs-create");
    }
    ramfs_test_pass("second-ramfs-create");
    child_fs = ramfs_get_vfs(child_ramfs);
    if ((child_fs == NULL) ||
        (vfs_mkdir_at(&root, "mnt", &mountpoint) != VFS_RESULT_OK) ||
        (vfs_mount_filesystem(child_fs, &mountpoint) != VFS_RESULT_OK)) {
        ramfs_test_fail("child-mount");
    }
    ramfs_test_pass("child-mount");

    if ((vfs_resolve(NULL, "/mnt", &child_root) != VFS_RESULT_OK) ||
        (child_root.node != child_fs->root) ||
        (vfs_create_at(&child_root, "child-file", &child_file) !=
         VFS_RESULT_OK)) {
        ramfs_test_fail("child-resolve");
    }
    ramfs_test_write_path(&child_file, child_bytes, sizeof(child_bytes),
                          "child-resolve");
    ramfs_test_release(&child_file, "child-resolve");
    if ((vfs_resolve(NULL, "/mnt/child-file", &child_file) !=
         VFS_RESULT_OK) ||
        !ramfs_test_read_path(&child_file, ramfs_test_verify,
                              sizeof(child_bytes), &transferred) ||
        (transferred != sizeof(child_bytes)) ||
        !ramfs_test_bytes_equal(ramfs_test_verify, child_bytes,
                                sizeof(child_bytes))) {
        ramfs_test_fail("child-resolve");
    }
    ramfs_test_release(&child_file, "child-resolve");
    ramfs_test_pass("child-resolve");

    if ((vfs_resolve(NULL, "/mnt/..", &temporary) != VFS_RESULT_OK) ||
        !vfs_path_equal(&temporary, &root)) {
        ramfs_test_fail("mount-dotdot");
    }
    ramfs_test_release(&temporary, "mount-dotdot");
    ramfs_test_pass("mount-dotdot");

    if (vfs_create_at(&root, "cross", &temporary) != VFS_RESULT_OK) {
        ramfs_test_fail("cross-filesystem-rename");
    }
    ramfs_test_release(&temporary, "cross-filesystem-rename");
    if (vfs_rename_at(&root, "cross", &child_root, "cross") !=
        VFS_RESULT_CROSS_FILESYSTEM) {
        ramfs_test_fail("cross-filesystem-rename");
    }
    ramfs_test_pass("cross-filesystem-rename");
    if (vfs_unlink_at(&root, "cross") != VFS_RESULT_OK) {
        ramfs_test_fail("cross-filesystem-rename");
    }

    serial_write_string("Capacity / safety:\n");
    if ((vfs_truncate_path(&data_file,
                           (uint64_t)RAMFS_FILE_MAX + 1ULL) !=
         VFS_RESULT_NO_SPACE) ||
        !ramfs_test_read_path(&data_file, ramfs_test_verify,
                              sizeof(expected_roundtrip), &transferred) ||
        (transferred != sizeof(expected_roundtrip)) ||
        !ramfs_test_bytes_equal(ramfs_test_verify, expected_roundtrip,
                                sizeof(expected_roundtrip))) {
        ramfs_test_fail("node-or-data-capacity");
    }
    ramfs_test_pass("node-or-data-capacity");

    backend_node = NULL;
    if ((child_fs->operations->lookup(child_fs, root.node,
                                      "foreign", 7U, &backend_node) !=
         VFS_RESULT_CORRUPT) ||
        (root_fs->operations->create(root_fs, root.node,
                                     ".", 1U, &backend_node) !=
         VFS_RESULT_INVALID_ARGUMENT) ||
        (root_fs->operations->read(root_fs, root.node, 0ULL,
                                   ramfs_test_verify, 1U, &transferred) !=
         VFS_RESULT_NOT_REGULAR)) {
        ramfs_test_fail("invalid-object-reject");
    }
    ramfs_test_pass("invalid-object-reject");

    serial_write_string("Cleanup:\n");
    ramfs_test_release(&empty_file, "paths-released");
    ramfs_test_release(&data_file, "paths-released");
    ramfs_test_release(&gap_file, "paths-released");
    ramfs_test_release(&truncate_file, "paths-released");
    ramfs_test_release(&limit_file, "paths-released");
    ramfs_test_release(&etc, "paths-released");
    ramfs_test_release(&from_dir, "paths-released");
    ramfs_test_release(&to_dir, "paths-released");
    ramfs_test_release(&child_root, "paths-released");
    ramfs_test_release(&mountpoint, "paths-released");
    ramfs_test_pass("handles-closed");
    ramfs_test_release(&root, "paths-released");
    if (!vfs_get_stats(&vfs_stats) ||
        (vfs_stats.path_reference_count != 0ULL) ||
        (vfs_stats.open_handle_count != 0ULL)) {
        ramfs_test_fail("paths-released");
    }
    ramfs_test_pass("paths-released");

    if (vfs_shutdown() != VFS_RESULT_OK) {
        ramfs_test_fail("vfs-shutdown");
    }
    ramfs_test_pass("vfs-shutdown");
    if (ramfs_destroy_filesystem(child_ramfs) != VFS_RESULT_OK) {
        ramfs_test_fail("child-ramfs-destroy");
    }
    child_ramfs = NULL;
    ramfs_test_pass("child-ramfs-destroy");
    if (ramfs_destroy_filesystem(root_ramfs) != VFS_RESULT_OK) {
        ramfs_test_fail("root-ramfs-destroy");
    }
    root_ramfs = NULL;
    ramfs_test_pass("root-ramfs-destroy");

    if (!heap_get_stats(&heap_after) ||
        (heap_after.used_bytes != heap_before.used_bytes)) {
        ramfs_test_fail("heap-used-restored");
    }
    ramfs_test_pass("heap-used-restored");
    if (heap_after.allocation_count != heap_before.allocation_count) {
        ramfs_test_fail("heap-allocation-count-restored");
    }
    ramfs_test_pass("heap-allocation-count-restored");

    serial_write_string("RAMFS node max: 32\n");
    serial_write_string("RAMFS file max: 8192\n");
    serial_write_string("RAMFS total data max: 32768\n");
    serial_write_string("BoringKernel RAMFS test passed.\n");
    x86_64_halt_forever();
}
