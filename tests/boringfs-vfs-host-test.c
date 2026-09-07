#include <boring/block_device.h>
#include <boring/boringfs.h>
#include <boring/boringfs_vfs.h>
#include <boring/heap.h>
#include <boring/vfs.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_BLOCKS 64U
#define TEST_SECTORS ((uint64_t)TEST_BLOCKS * 8ULL)
#define TEST_BYTES ((size_t)TEST_BLOCKS * (size_t)BORINGFS_BLOCK_SIZE)
#define BASE_ALLOCATED_OBJECTS 10U
#define FIRST_FREE_OBJECT 11U
#define FIRST_FREE_BLOCK 15U

struct memory_device {
    uint8_t bytes[TEST_BYTES];
    uint64_t write_calls;
    uint64_t fail_write_call;
    bool out_of_range_seen;
};

static uint8_t fixture[TEST_BYTES];
static struct memory_device memory_device;
static struct block_device test_device;
static unsigned int checks;

static void fail(const char *message) {
    (void)fprintf(stderr, "boringfs VFS host test FAILED: %s\n", message);
    exit(EXIT_FAILURE);
}

static void check(bool condition, const char *message) {
    ++checks;
    if (!condition) {
        fail(message);
    }
}

void *kmalloc(size_t size) {
    return malloc(size);
}

bool kfree(void *pointer) {
    free(pointer);
    return pointer != NULL;
}

bool vfs_filesystem_prepare(struct vfs_filesystem *filesystem,
                            uint64_t id,
                            const struct vfs_operations *operations,
                            void *backend_context) {
    if ((filesystem == NULL) || (id == 0ULL) || (operations == NULL)) {
        return false;
    }
    filesystem->id = id;
    filesystem->operations = operations;
    filesystem->root = NULL;
    filesystem->backend_context = backend_context;
    filesystem->valid = true;
    return true;
}

bool vfs_node_prepare(struct vfs_node *node,
                      struct vfs_filesystem *filesystem,
                      uint64_t id,
                      enum vfs_node_type type,
                      struct vfs_node *parent,
                      void *backend_context) {
    if ((node == NULL) || (filesystem == NULL) || !filesystem->valid ||
        (id == 0ULL) ||
        ((type != VFS_NODE_DIRECTORY) && (type != VFS_NODE_REGULAR))) {
        return false;
    }
    if (parent != NULL) {
        if (!parent->valid || (parent->filesystem != filesystem) ||
            (parent->type != VFS_NODE_DIRECTORY) || (parent == node)) {
            return false;
        }
    } else if (type != VFS_NODE_DIRECTORY) {
        return false;
    }
    node->id = id;
    node->type = type;
    node->filesystem = filesystem;
    node->parent = parent;
    node->backend_context = backend_context;
    node->reference_count = 0U;
    node->valid = true;
    return true;
}

bool vfs_filesystem_set_root(struct vfs_filesystem *filesystem,
                             struct vfs_node *root) {
    if ((filesystem == NULL) || !filesystem->valid ||
        (filesystem->root != NULL) || (root == NULL) || !root->valid ||
        (root->filesystem != filesystem) ||
        (root->type != VFS_NODE_DIRECTORY) || (root->parent != NULL)) {
        return false;
    }
    root->parent = root;
    filesystem->root = root;
    return true;
}

static enum block_device_result memory_read(void *context,
                                            uint64_t first_block,
                                            uint32_t block_count,
                                            void *buffer) {
    struct memory_device *const memory = (struct memory_device *)context;
    const uint64_t byte_offset = first_block * 512ULL;
    const uint64_t byte_count = (uint64_t)block_count * 512ULL;

    if ((memory == NULL) || (buffer == NULL) ||
        (first_block >= TEST_SECTORS) ||
        ((uint64_t)block_count > TEST_SECTORS - first_block) ||
        (byte_offset > (uint64_t)TEST_BYTES) ||
        (byte_count > (uint64_t)TEST_BYTES - byte_offset)) {
        if (memory != NULL) {
            memory->out_of_range_seen = true;
        }
        return BLOCK_DEVICE_RESULT_OUT_OF_RANGE;
    }
    (void)memcpy(buffer, &memory->bytes[(size_t)byte_offset],
                 (size_t)byte_count);
    return BLOCK_DEVICE_RESULT_OK;
}

static enum block_device_result memory_write(void *context,
                                             uint64_t first_block,
                                             uint32_t block_count,
                                             const void *buffer) {
    struct memory_device *const memory = (struct memory_device *)context;
    const uint64_t byte_offset = first_block * 512ULL;
    const uint64_t byte_count = (uint64_t)block_count * 512ULL;

    if ((memory == NULL) || (buffer == NULL) ||
        (first_block >= TEST_SECTORS) ||
        ((uint64_t)block_count > TEST_SECTORS - first_block) ||
        (byte_offset > (uint64_t)TEST_BYTES) ||
        (byte_count > (uint64_t)TEST_BYTES - byte_offset)) {
        if (memory != NULL) {
            memory->out_of_range_seen = true;
        }
        return BLOCK_DEVICE_RESULT_OUT_OF_RANGE;
    }
    ++memory->write_calls;
    if ((memory->fail_write_call != 0ULL) &&
        (memory->write_calls == memory->fail_write_call)) {
        return BLOCK_DEVICE_RESULT_IO_ERROR;
    }
    (void)memcpy(&memory->bytes[(size_t)byte_offset], buffer,
                 (size_t)byte_count);
    return BLOCK_DEVICE_RESULT_OK;
}

static const struct block_device_ops memory_operations = {
    .read = memory_read,
    .write = memory_write
};

static void load_fixture(const char *path) {
    FILE *file;

    file = fopen(path, "rb");
    if (file == NULL) {
        fail("cannot open fixture");
    }
    if ((fread(fixture, 1U, sizeof(fixture), file) != sizeof(fixture)) ||
        (fgetc(file) != EOF) || (fclose(file) != 0)) {
        fail("fixture size/read mismatch");
    }
}

static struct vfs_filesystem *fresh_mount(bool read_only) {
    struct boringfs_vfs *boringfs = NULL;
    struct boringfs_validation_error error;
    enum vfs_result result;

    (void)memcpy(memory_device.bytes, fixture, sizeof(fixture));
    memory_device.write_calls = 0ULL;
    memory_device.fail_write_call = 0ULL;
    memory_device.out_of_range_seen = false;
    test_device.name = "host-memory";
    test_device.logical_block_size = 512U;
    test_device.block_count = TEST_SECTORS;
    test_device.read_only = false;
    test_device.context = &memory_device;
    test_device.ops = &memory_operations;
    block_device_init();
    check(block_device_register(&test_device) == BLOCK_DEVICE_RESULT_OK,
          "register memory block device");
    result = read_only ?
        boringfs_vfs_create_readonly(&test_device, 100ULL, &boringfs,
                                     &error) :
        boringfs_vfs_create_writable(&test_device, 100ULL, &boringfs,
                                     &error);
    check(result == VFS_RESULT_OK, "mount valid fixture");
    return boringfs_vfs_get_vfs(boringfs);
}

static void check_valid(const char *label) {
    uint32_t block_owner[TEST_BLOCKS];
    uint8_t references[BORINGFS_MIN_OBJECTS];
    struct boringfs_validation_workspace workspace;
    struct boringfs_validation_error error;
    enum boringfs_validation_result result;

    workspace.block_owner = block_owner;
    workspace.block_owner_count = TEST_BLOCKS;
    workspace.object_reference_count = references;
    workspace.object_reference_count_count = BORINGFS_MIN_OBJECTS;
    result = boringfs_validate_volume(memory_device.bytes,
                                      sizeof(memory_device.bytes),
                                      &workspace, &error);
    if (result != BORINGFS_VALIDATE_OK) {
        (void)fprintf(stderr, "%s: %s object=%u block=%u record=%llu\n",
                      label, boringfs_validation_result_name(result),
                      (unsigned int)error.object_id,
                      (unsigned int)error.block,
                      (unsigned long long)error.directory_record_index);
        fail("mutated volume failed structural validation");
    }
    check(!memory_device.out_of_range_seen, "no I/O beyond disk end");
}

static struct vfs_node *create_file(struct vfs_filesystem *filesystem,
                                    const char *name,
                                    enum vfs_result expected) {
    struct vfs_node *node = NULL;
    const enum vfs_result result = filesystem->operations->create(
        filesystem, filesystem->root, name, strlen(name), &node);

    check(result == expected, "file creation result");
    if (expected == VFS_RESULT_OK) {
        check(node != NULL, "created node returned");
    } else {
        check(node == NULL, "failed create returned no node");
    }
    return node;
}

static void write_file(struct vfs_filesystem *filesystem,
                       struct vfs_node *node,
                       const char *text,
                       enum vfs_result expected) {
    size_t transferred = 0U;
    const size_t length = strlen(text);
    const enum vfs_result result = filesystem->operations->write(
        filesystem, node, 0ULL, text, length, &transferred);

    check(result == expected, "file write result");
    check((expected != VFS_RESULT_OK) || (transferred == length),
          "complete synchronous write");
}

static void check_basic_mutations(void) {
    struct vfs_filesystem *filesystem = fresh_mount(false);
    struct vfs_node *file;
    struct vfs_node *reused;
    struct vfs_node *lookup = NULL;
    char read_buffer[32];
    size_t transferred = 0U;
    struct boringfs_object object;
    const size_t object_offset =
        (2U * (size_t)BORINGFS_BLOCK_SIZE) +
        ((FIRST_FREE_OBJECT - 1U) * (size_t)BORINGFS_OBJECT_RECORD_SIZE);

    check(filesystem != NULL, "writable filesystem exposed");
    file = create_file(filesystem, "hello.txt", VFS_RESULT_OK);
    check(file->id == (uint64_t)FIRST_FREE_OBJECT,
          "lowest free object allocated");
    (void)create_file(filesystem, "hello.txt", VFS_RESULT_ALREADY_EXISTS);
    write_file(filesystem, file, "BoringOS-persistence-test", VFS_RESULT_OK);
    check_valid("basic create/write");
    check(filesystem->operations->read(
              filesystem, file, 0ULL, read_buffer,
              sizeof("BoringOS-persistence-test") - 1U,
              &transferred) == VFS_RESULT_OK,
          "read written file");
    check((transferred == sizeof("BoringOS-persistence-test") - 1U) &&
          (memcmp(read_buffer, "BoringOS-persistence-test",
                  transferred) == 0),
          "written bytes round trip");
    check(filesystem->operations->unlink(
              filesystem, filesystem->root, "hello.txt",
              sizeof("hello.txt") - 1U) == VFS_RESULT_OK,
          "unlink regular file");
    check_valid("basic remove");
    check(filesystem->operations->lookup(
              filesystem, filesystem->root, "hello.txt",
              sizeof("hello.txt") - 1U, &lookup) == VFS_RESULT_NOT_FOUND,
          "removed file not found");
    reused = create_file(filesystem, "reused.txt", VFS_RESULT_OK);
    check(reused->id == (uint64_t)FIRST_FREE_OBJECT,
          "deleted object id reused");
    write_file(filesystem, reused, "reuse", VFS_RESULT_OK);
    check(boringfs_decode_object(&memory_device.bytes[object_offset],
                                 BORINGFS_OBJECT_RECORD_SIZE, &object) &&
          (object.extent_count == 1U) &&
          (object.extents[0].start_block == FIRST_FREE_BLOCK),
          "deleted data block reused");
    check_valid("basic reuse");
    check(memcmp(&fixture[0], &memory_device.bytes[0],
                 BORINGFS_BLOCK_SIZE) == 0,
          "superblock neighbor preserved");
    check(memcmp(&fixture[3U * BORINGFS_BLOCK_SIZE],
                 &memory_device.bytes[3U * BORINGFS_BLOCK_SIZE],
                 BORINGFS_BLOCK_SIZE) == 0,
          "unused object-table neighbor preserved");
    check(memcmp(&fixture[5U * BORINGFS_BLOCK_SIZE],
                 &memory_device.bytes[5U * BORINGFS_BLOCK_SIZE],
                 10U * BORINGFS_BLOCK_SIZE) == 0,
          "preexisting file data neighbors preserved");
}

static void check_names_and_directories(void) {
    struct vfs_filesystem *filesystem = fresh_mount(false);
    struct vfs_node *directory = NULL;
    struct vfs_node *file = NULL;
    char maximum[BORINGFS_MAX_FILENAME + 1U];
    char too_long[BORINGFS_MAX_FILENAME + 2U];
    size_t index;

    for (index = 0U; index < (size_t)BORINGFS_MAX_FILENAME; ++index) {
        maximum[index] = 'm';
        too_long[index] = 'x';
    }
    maximum[BORINGFS_MAX_FILENAME] = '\0';
    too_long[BORINGFS_MAX_FILENAME] = 'x';
    too_long[BORINGFS_MAX_FILENAME + 1U] = '\0';
    check(filesystem->operations->create(
              filesystem, filesystem->root, maximum,
              BORINGFS_MAX_FILENAME, &file) == VFS_RESULT_OK,
          "maximum BoringFS filename accepted");
    file = NULL;
    check(filesystem->operations->create(
              filesystem, filesystem->root, maximum,
              BORINGFS_MAX_FILENAME, &file) == VFS_RESULT_ALREADY_EXISTS,
          "duplicate maximum filename rejected");
    check(filesystem->operations->create(
              filesystem, filesystem->root, too_long,
              BORINGFS_MAX_FILENAME + 1U, &file) ==
          VFS_RESULT_NAME_TOO_LONG,
          "overlong BoringFS filename rejected");
    check(filesystem->operations->mkdir(
              filesystem, filesystem->root, "empty", sizeof("empty") - 1U,
              &directory) == VFS_RESULT_OK,
          "directory create");
    check(filesystem->operations->rmdir(
              filesystem, filesystem->root, "empty", sizeof("empty") - 1U) ==
          VFS_RESULT_OK,
          "empty directory remove");
    check_valid("names and directories");
}

static void check_full_object_table(void) {
    struct vfs_filesystem *filesystem = fresh_mount(false);
    uint32_t index;

    for (index = 0U;
         index < BORINGFS_MIN_OBJECTS - BASE_ALLOCATED_OBJECTS; ++index) {
        char name[8];

        (void)snprintf(name, sizeof(name), "o%02u", (unsigned int)index);
        (void)create_file(filesystem, name, VFS_RESULT_OK);
    }
    (void)create_file(filesystem, "overflow", VFS_RESULT_NO_SPACE);
    check_valid("full object table");
}

static void check_full_volume(void) {
    struct vfs_filesystem *filesystem = fresh_mount(false);
    bool full = false;
    uint32_t index;

    for (index = 0U;
         index < BORINGFS_MIN_OBJECTS - BASE_ALLOCATED_OBJECTS; ++index) {
        char name[8];
        struct vfs_node *file;
        size_t transferred = 0U;
        enum vfs_result result;

        (void)snprintf(name, sizeof(name), "v%02u", (unsigned int)index);
        file = create_file(filesystem, name, VFS_RESULT_OK);
        result = filesystem->operations->write(
            filesystem, file, 0ULL, "x", 1U, &transferred);
        if (result == VFS_RESULT_NO_SPACE) {
            full = true;
            break;
        }
        check((result == VFS_RESULT_OK) && (transferred == 1U),
              "volume fill write");
    }
    check(full, "full data volume reported no space");
    check_valid("full data volume");
}

static void fill_root_capacity(struct vfs_filesystem *filesystem) {
    uint32_t index;

    for (index = 0U; index < 12U; ++index) {
        char name[8];

        (void)snprintf(name, sizeof(name), "c%02u", (unsigned int)index);
        (void)create_file(filesystem, name, VFS_RESULT_OK);
    }
}

static void check_failed_create_steps(void) {
    uint64_t failure;

    for (failure = 1ULL; failure <= 3ULL; ++failure) {
        struct vfs_filesystem *filesystem = fresh_mount(false);

        memory_device.fail_write_call = failure;
        (void)create_file(filesystem, "failed", VFS_RESULT_CORRUPT);
        memory_device.fail_write_call = 0ULL;
        check_valid("failed create rollback");
    }
    for (failure = 1ULL; failure <= 5ULL; ++failure) {
        struct vfs_filesystem *filesystem = fresh_mount(false);

        fill_root_capacity(filesystem);
        memory_device.write_calls = 0ULL;
        memory_device.fail_write_call = failure;
        (void)create_file(filesystem, "extend", VFS_RESULT_CORRUPT);
        memory_device.fail_write_call = 0ULL;
        check_valid("failed directory extension rollback");
    }
}

static void check_failed_write_steps(void) {
    uint64_t failure;

    for (failure = 1ULL; failure <= 3ULL; ++failure) {
        struct vfs_filesystem *filesystem = fresh_mount(false);
        struct vfs_node *file = create_file(filesystem, "write", VFS_RESULT_OK);

        memory_device.write_calls = 0ULL;
        memory_device.fail_write_call = failure;
        write_file(filesystem, file, "data", VFS_RESULT_CORRUPT);
        memory_device.fail_write_call = 0ULL;
        check_valid("failed write rollback");
    }
}

static void check_failed_remove_steps(void) {
    uint64_t failure;

    for (failure = 1ULL; failure <= 3ULL; ++failure) {
        struct vfs_filesystem *filesystem = fresh_mount(false);
        struct vfs_node *file = create_file(filesystem, "remove", VFS_RESULT_OK);
        enum vfs_result result;

        write_file(filesystem, file, "data", VFS_RESULT_OK);
        memory_device.write_calls = 0ULL;
        memory_device.fail_write_call = failure;
        result = filesystem->operations->unlink(
            filesystem, filesystem->root, "remove", sizeof("remove") - 1U);
        check(result == VFS_RESULT_CORRUPT, "failed remove result");
        memory_device.fail_write_call = 0ULL;
        check_valid("failed remove rollback");
    }
}

static void check_read_only_contract(void) {
    struct vfs_filesystem *filesystem = fresh_mount(true);
    struct vfs_node *node = NULL;
    size_t transferred = 0U;

    check(filesystem->operations->create(
              filesystem, filesystem->root, "denied", sizeof("denied") - 1U,
              &node) == VFS_RESULT_ACCESS_DENIED,
          "read-only create denied");
    check(filesystem->operations->write(
              filesystem, filesystem->root, 0ULL, "x", 1U, &transferred) ==
          VFS_RESULT_ACCESS_DENIED,
          "read-only write denied before node dispatch");
    check(memory_device.write_calls == 0ULL,
          "read-only mount issued no device writes");
    test_device.read_only = true;
    {
        struct boringfs_vfs *boringfs = NULL;
        struct boringfs_validation_error error;

        check(boringfs_vfs_create_writable(
                  &test_device, 101ULL, &boringfs, &error) ==
              VFS_RESULT_INVALID_ARGUMENT,
              "writable mount rejects read-only device");
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        (void)fprintf(stderr, "Usage: %s <valid-fixture>\n", argv[0]);
        return EXIT_FAILURE;
    }
    load_fixture(argv[1]);
    check_basic_mutations();
    check_names_and_directories();
    check_full_object_table();
    check_full_volume();
    check_failed_create_steps();
    check_failed_write_steps();
    check_failed_remove_steps();
    check_read_only_contract();
    (void)printf("BoringFS writable VFS host tests: %u checks PASS\n", checks);
    return EXIT_SUCCESS;
}
