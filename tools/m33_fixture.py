#!/usr/bin/env python3
from pathlib import Path


def rep(path: str, old: str, new: str, marker: str) -> None:
    p = Path(path)
    text = p.read_text()
    if marker in text:
        return
    if text.count(old) != 1:
        raise SystemExit(f"fixture anchor mismatch {path}: {text.count(old)} {old[:80]!r}")
    p.write_text(text.replace(old, new, 1))


p = "tests/boringfs-fixture.c"
rep(p, "#define M32_FIXTURE_BLOCKS 80U\n",
    "#define M32_FIXTURE_BLOCKS 80U\n#define M33_FIXTURE_BLOCKS 96U\n",
    "#define M33_FIXTURE_BLOCKS 96U")
rep(p, "#define MEMORY_TEST_BLOCK0 (INPUT_TEST_BLOCK0 + PROGRAM_SLOT_BLOCKS)\n",
    "#define MEMORY_TEST_BLOCK0 (INPUT_TEST_BLOCK0 + PROGRAM_SLOT_BLOCKS)\n#define IPC_TEST_BLOCK0 (MEMORY_TEST_BLOCK0 + PROGRAM_SLOT_BLOCKS)\n",
    "#define IPC_TEST_BLOCK0")
rep(p,
    '''                        const uint8_t *memory_test_bytes,
                        size_t memory_test_size) {''',
    '''                        const uint8_t *memory_test_bytes,
                        size_t memory_test_size,
                        const uint8_t *ipc_test_bytes,
                        size_t ipc_test_size) {''',
    "size_t ipc_test_size) {")
rep(p, "    uint32_t memory_test_blocks = 0U;\n",
    "    uint32_t memory_test_blocks = 0U;\n    uint32_t ipc_test_blocks = 0U;\n",
    "uint32_t ipc_test_blocks = 0U;")
rep(p, "    const bool have_memory_test = (memory_test_bytes != NULL);\n",
    "    const bool have_memory_test = (memory_test_bytes != NULL);\n    const bool have_ipc_test = (ipc_test_bytes != NULL);\n",
    "const bool have_ipc_test")
rep(p,
    '''    if ((have_cat && !have_boringfetch) ||
        (have_input_test && !have_cat) ||
        (have_memory_test && !have_input_test)) {''',
    '''    if ((have_cat && !have_boringfetch) ||
        (have_input_test && !have_cat) ||
        (have_memory_test && !have_input_test) ||
        (have_ipc_test && !have_memory_test)) {''',
    "(have_ipc_test && !have_memory_test)")
rep(p,
    '''    if (have_memory_test) {
        if (!fixture_program_blocks(memory_test_size, MEMORY_TEST_BLOCK0,
                                    &memory_test_blocks)) {
            return false;
        }
    } else if (memory_test_size != 0U) {
        return false;
    }
''',
    '''    if (have_memory_test) {
        if (!fixture_program_blocks(memory_test_size, MEMORY_TEST_BLOCK0,
                                    &memory_test_blocks)) {
            return false;
        }
    } else if (memory_test_size != 0U) {
        return false;
    }
    if (have_ipc_test) {
        if (!fixture_program_blocks(ipc_test_size, IPC_TEST_BLOCK0,
                                    &ipc_test_blocks)) {
            return false;
        }
    } else if (ipc_test_size != 0U) {
        return false;
    }
''',
    "fixture_program_blocks(ipc_test_size")
rep(p,
    '''    if (have_memory_test) {
        for (block = MEMORY_TEST_BLOCK0;
             block < MEMORY_TEST_BLOCK0 + memory_test_blocks; ++block) {
            bitmap_set(volume, block, true);
        }
    }
''',
    '''    if (have_memory_test) {
        for (block = MEMORY_TEST_BLOCK0;
             block < MEMORY_TEST_BLOCK0 + memory_test_blocks; ++block) {
            bitmap_set(volume, block, true);
        }
    }
    if (have_ipc_test) {
        for (block = IPC_TEST_BLOCK0;
             block < IPC_TEST_BLOCK0 + ipc_test_blocks; ++block) {
            bitmap_set(volume, block, true);
        }
    }
''',
    "block < IPC_TEST_BLOCK0 + ipc_test_blocks")
rep(p,
    '''                         (have_memory_test ? 4ULL :
                          (have_input_test ? 3ULL :
                           (have_cat ? 2ULL : 1ULL))) *''',
    '''                         (have_ipc_test ? 5ULL :
                          (have_memory_test ? 4ULL :
                           (have_input_test ? 3ULL :
                            (have_cat ? 2ULL : 1ULL)))) *''',
    "(have_ipc_test ? 5ULL")
rep(p,
    '''        if (have_memory_test) {
            extent[0].start_block = MEMORY_TEST_BLOCK0;
            extent[0].block_count = memory_test_blocks;
            if (!make_object(volume, volume_size, &superblock, 15U, 11U,
                             BORINGFS_TYPE_REGULAR,
                             (uint64_t)memory_test_size, extent, 1U)) {
                return false;
            }
        }
''',
    '''        if (have_memory_test) {
            extent[0].start_block = MEMORY_TEST_BLOCK0;
            extent[0].block_count = memory_test_blocks;
            if (!make_object(volume, volume_size, &superblock, 15U, 11U,
                             BORINGFS_TYPE_REGULAR,
                             (uint64_t)memory_test_size, extent, 1U)) {
                return false;
            }
        }
        if (have_ipc_test) {
            extent[0].start_block = IPC_TEST_BLOCK0;
            extent[0].block_count = ipc_test_blocks;
            if (!make_object(volume, volume_size, &superblock, 16U, 11U,
                             BORINGFS_TYPE_REGULAR,
                             (uint64_t)ipc_test_size, extent, 1U)) {
                return false;
            }
        }
''',
    "(uint64_t)ipc_test_size")
rep(p,
    '''         (have_memory_test &&
          !write_dirent(volume, volume_size, BIN_BLOCK, 3ULL, 15U,
                        BORINGFS_TYPE_REGULAR, "memory-test")))) {''',
    '''         (have_memory_test &&
          !write_dirent(volume, volume_size, BIN_BLOCK, 3ULL, 15U,
                        BORINGFS_TYPE_REGULAR, "memory-test")) ||
         (have_ipc_test &&
          !write_dirent(volume, volume_size, BIN_BLOCK, 4ULL, 16U,
                        BORINGFS_TYPE_REGULAR, "ipc-test")))) {''',
    'BORINGFS_TYPE_REGULAR, "ipc-test"')
rep(p,
    '''    if (have_memory_test) {
        (void)memcpy(&volume[(size_t)MEMORY_TEST_BLOCK0 * BORINGFS_BLOCK_SIZE],
                     memory_test_bytes, memory_test_size);
    }
''',
    '''    if (have_memory_test) {
        (void)memcpy(&volume[(size_t)MEMORY_TEST_BLOCK0 * BORINGFS_BLOCK_SIZE],
                     memory_test_bytes, memory_test_size);
    }
    if (have_ipc_test) {
        (void)memcpy(&volume[(size_t)IPC_TEST_BLOCK0 * BORINGFS_BLOCK_SIZE],
                     ipc_test_bytes, ipc_test_size);
    }
''',
    "ipc_test_bytes, ipc_test_size")
rep(p, "    uint32_t block_owner[M32_FIXTURE_BLOCKS];\n",
    "    uint32_t block_owner[M33_FIXTURE_BLOCKS];\n",
    "block_owner[M33_FIXTURE_BLOCKS]")
rep(p,
    '''        (size_t)((argc == 7) ? M32_FIXTURE_BLOCKS :
                 HISTORICAL_FIXTURE_BLOCKS) *''',
    '''        (size_t)((argc == 8) ? M33_FIXTURE_BLOCKS :
                 ((argc == 7) ? M32_FIXTURE_BLOCKS :
                                HISTORICAL_FIXTURE_BLOCKS)) *''',
    "(argc == 8) ? M33_FIXTURE_BLOCKS")
rep(p, "    uint8_t *memory_test_bytes = NULL;\n",
    "    uint8_t *memory_test_bytes = NULL;\n    uint8_t *ipc_test_bytes = NULL;\n",
    "uint8_t *ipc_test_bytes = NULL")
rep(p, "    size_t memory_test_size = 0U;\n",
    "    size_t memory_test_size = 0U;\n    size_t ipc_test_size = 0U;\n",
    "size_t ipc_test_size = 0U")
rep(p, "    if ((argc < 3) || (argc > 7)) {\n",
    "    if ((argc < 3) || (argc > 8)) {\n",
    "argc > 8")
rep(p,
    ' [boringfetch-elf [cat-elf [input-test-elf [memory-test-elf]]]]\\n",\n',
    ' [boringfetch-elf [cat-elf [input-test-elf [memory-test-elf [ipc-test-elf]]]]]\\n",\n',
    "ipc-test-elf")
rep(p,
    '''    fixture_blocks = (argc == 7) ? M32_FIXTURE_BLOCKS :
                                   HISTORICAL_FIXTURE_BLOCKS;''',
    '''    fixture_blocks = (argc == 8) ? M33_FIXTURE_BLOCKS :
                     ((argc == 7) ? M32_FIXTURE_BLOCKS :
                                    HISTORICAL_FIXTURE_BLOCKS);''',
    "fixture_blocks = (argc == 8)")
rep(p,
    '''    if ((argc == 7) &&
        !read_program(argv[6], &memory_test_bytes, &memory_test_size)) {''',
    '''    if ((argc >= 7) &&
        !read_program(argv[6], &memory_test_bytes, &memory_test_size)) {''',
    "if ((argc >= 7)")
rep(p,
    '''        return 2;
    }
    volume = (uint8_t *)malloc(volume_size);''',
    '''        return 2;
    }
    if ((argc == 8) &&
        !read_program(argv[7], &ipc_test_bytes, &ipc_test_size)) {
        free(memory_test_bytes);
        free(input_test_bytes);
        free(cat_bytes);
        free(boringfetch_bytes);
        (void)fprintf(stderr, "cannot read bounded ipc-test ELF: %s\\n", argv[7]);
        return 2;
    }
    volume = (uint8_t *)malloc(volume_size);''',
    "cannot read bounded ipc-test ELF")
rep(p,
    '''                     input_test_bytes, input_test_size,
                     memory_test_bytes, memory_test_size)) {''',
    '''                     input_test_bytes, input_test_size,
                     memory_test_bytes, memory_test_size,
                     ipc_test_bytes, ipc_test_size)) {''',
    "ipc_test_bytes, ipc_test_size))")
rep(p,
    '''        free(memory_test_bytes);
        free(input_test_bytes);''',
    '''        free(ipc_test_bytes);
        free(memory_test_bytes);
        free(input_test_bytes);''',
    "free(ipc_test_bytes);\n        free(memory_test_bytes);")
rep(p,
    '''    free(memory_test_bytes);
    free(input_test_bytes);''',
    '''    free(ipc_test_bytes);
    free(memory_test_bytes);
    free(input_test_bytes);''',
    "free(ipc_test_bytes);\n    free(memory_test_bytes);")

# QEMU bundle gets /bin/ipc-test while M32/historical fixtures keep old sizes.
p = "Makefile"
rep(p,
    '''\t$(MAKE) user-memory-test
\tmkdir -p $(BUILD_DIR)/boringos-qemu-x86_64''',
    '''\t$(MAKE) user-memory-test
\t$(MAKE) user-ipc-test
\tmkdir -p $(BUILD_DIR)/boringos-qemu-x86_64''',
    "\t$(MAKE) user-ipc-test\n\tmkdir -p $(BUILD_DIR)/boringos-qemu-x86_64")
rep(p,
    '''\t$(BORINGFS_FIXTURE) $(BUILD_DIR)/boringos-qemu-x86_64/boringos-root.img valid $(BORINGFETCH_ELF) $(CAT_ELF) $(INPUT_TEST_ELF) $(MEMORY_TEST_ELF)
''',
    '''\t$(BORINGFS_FIXTURE) $(BUILD_DIR)/boringos-qemu-x86_64/boringos-root.img valid $(BORINGFETCH_ELF) $(CAT_ELF) $(INPUT_TEST_ELF) $(MEMORY_TEST_ELF) $(IPC_TEST_ELF)
''',
    "$(MEMORY_TEST_ELF) $(IPC_TEST_ELF)")
