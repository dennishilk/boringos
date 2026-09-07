#ifndef BORING_AHCI_H
#define BORING_AHCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/pci.h>
#include <boring/pci_inventory.h>

#define AHCI_BORING_MAX_PORTS 16U
#define AHCI_MMIO_WINDOW_SIZE 0x900U
#define AHCI_PCI_CLASS_MASS_STORAGE 0x01U
#define AHCI_PCI_SUBCLASS_SATA 0x06U
#define AHCI_PCI_PROG_IF 0x01U
#define AHCI_SIGNATURE_ATA 0x00000101U
#define AHCI_WAIT_LIMIT 1000000U

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

typedef bool (*ahci_poll_reader)(void *context, uint32_t offset,
                                 uint32_t *value);

/* Pure bounded M55 helpers, shared with host fixtures. */
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

/* Hardware path: one segment-zero AHCI controller, no sector I/O in M55. */
bool ahci_init(struct ahci_state *state);
bool ahci_shutdown(void);
const struct ahci_state *ahci_get_state(void);

#endif
