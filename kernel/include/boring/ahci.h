#ifndef BORING_AHCI_H
#define BORING_AHCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/block_device.h>
#include <boring/pci.h>
#include <boring/pci_inventory.h>

#define AHCI_BORING_MAX_PORTS 16U
#define AHCI_MMIO_WINDOW_SIZE 0x900U
#define AHCI_PCI_CLASS_MASS_STORAGE 0x01U
#define AHCI_PCI_SUBCLASS_SATA 0x06U
#define AHCI_PCI_PROG_IF 0x01U
#define AHCI_SIGNATURE_ATA 0x00000101U
#define AHCI_WAIT_LIMIT 1000000U
#define AHCI_COMMAND_WAIT_LIMIT 50000000U
#define AHCI_IDENTIFY_WORDS 256U
#define AHCI_M56_DMA_BYTES 4096U

struct ahci_port_facts {
    uint8_t det;
    uint8_t ipm;
    bool present;
    bool sata;
};

struct ahci_port_state {
    uint32_t cmd;
    uint32_t ssts;
    uint32_t sig;
    uint32_t tfd;
    struct ahci_port_facts facts;
    uint8_t index;
    bool implemented;
    bool engine_active;
};

struct ahci_state {
    struct pci_device device;
    struct ahci_port_state ports[AHCI_BORING_MAX_PORTS];
    uint64_t abar_physical;
    uint32_t cap;
    uint32_t cap2;
    uint32_t vs;
    uint32_t ghc;
    uint32_t pi;
    uint32_t bohc;
    uint8_t prog_if;
    uint8_t hardware_ports;
    uint8_t inspected_ports;
    uint8_t implemented_ports;
    uint8_t present_ports;
    uint8_t sata_ports;
    bool ports_truncated;
    bool bios_handoff_complete;
    bool ahci_enabled;
    bool initialized;
};

struct ahci_identify_geometry {
    uint64_t logical_blocks;
    uint32_t logical_block_size;
    bool lba_supported;
    bool lba48_supported;
};

struct ahci_block_stats {
    uint64_t logical_blocks;
    uint64_t identify_commands;
    uint64_t read_commands;
    uint32_t logical_block_size;
    uint32_t max_blocks_per_command;
    uint32_t command_wait_limit;
    uint8_t port;
    uint8_t dma_frame_count;
    bool lba48;
    bool bus_master_enabled;
    bool command_engine_started;
    bool registered;
    bool read_only;
};

enum ahci_block_result {
    AHCI_BLOCK_RESULT_OK = 0,
    AHCI_BLOCK_RESULT_ALREADY_INITIALIZED,
    AHCI_BLOCK_RESULT_CONTROLLER_NOT_INITIALIZED,
    AHCI_BLOCK_RESULT_NO_SATA_DEVICE,
    AHCI_BLOCK_RESULT_PCI_COMMAND,
    AHCI_BLOCK_RESULT_DMA_ALLOCATION,
    AHCI_BLOCK_RESULT_DMA_ADDRESS,
    AHCI_BLOCK_RESULT_ENGINE_TIMEOUT,
    AHCI_BLOCK_RESULT_COMMAND_TIMEOUT,
    AHCI_BLOCK_RESULT_DEVICE_ERROR,
    AHCI_BLOCK_RESULT_IDENTIFY,
    AHCI_BLOCK_RESULT_UNSUPPORTED_DEVICE,
    AHCI_BLOCK_RESULT_BAD_GEOMETRY,
    AHCI_BLOCK_RESULT_REGISTRATION
};

typedef bool (*ahci_poll_reader)(void *context, uint32_t offset,
                                 uint32_t *value);

/* Pure bounded helpers, shared with host fixtures. */
bool ahci_select_controller(const struct boring_pci_inventory *inventory,
                            struct pci_device *device, uint8_t *prog_if);
bool ahci_validate_abar(const struct pci_bar *bar, size_t length);
bool ahci_bounded_ports(uint32_t cap, uint32_t pi,
                        uint8_t *hardware_ports,
                        uint8_t *inspected_ports,
                        uint32_t *implemented_mask,
                        bool *truncated);
bool ahci_decode_port_status(uint32_t ssts, uint32_t sig,
                             struct ahci_port_facts *facts);
bool ahci_wait_mask_bounded(ahci_poll_reader read, void *context,
                            uint32_t offset, uint32_t mask, bool want_set,
                            uint32_t limit, uint32_t *last_value);
bool ahci_parse_identify(const uint16_t words[AHCI_IDENTIFY_WORDS],
                         struct ahci_identify_geometry *geometry);
bool ahci_lba_range_valid(uint64_t logical_blocks, uint64_t first_block,
                          uint32_t block_count);
bool ahci_compute_transfer_bytes(uint32_t logical_block_size,
                                 uint32_t block_count,
                                 uint32_t transfer_limit,
                                 uint32_t *byte_count);

/* M55 hardware foundation: one segment-zero AHCI controller, no sector I/O. */
bool ahci_init(struct ahci_state *state);
bool ahci_shutdown(void);
const struct ahci_state *ahci_get_state(void);

/* M56 data path: one bounded synchronous read-only SATA backend. */
enum ahci_block_result ahci_block_init(void);
const struct block_device *ahci_block_device(void);
bool ahci_block_get_stats(struct ahci_block_stats *stats);
const char *ahci_block_result_name(enum ahci_block_result result);

#endif
