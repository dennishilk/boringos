#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/ahci.h>
#include <boring/cpu.h>
#include <boring/pci.h>
#include <boring/pci_inventory.h>
#include <boring/vmm.h>

#define AHCI_PCI_COMMAND_MEMORY (1U << 1)
#define AHCI_ABAR_INDEX 5U
#define AHCI_CAP_OFFSET 0x00U
#define AHCI_GHC_OFFSET 0x04U
#define AHCI_PI_OFFSET 0x0cU
#define AHCI_VS_OFFSET 0x10U
#define AHCI_CAP2_OFFSET 0x24U
#define AHCI_BOHC_OFFSET 0x28U
#define AHCI_GHC_HR (1U << 0)
#define AHCI_GHC_AE (1U << 31)
#define AHCI_CAP2_BOH (1U << 0)
#define AHCI_BOHC_BOS (1U << 0)
#define AHCI_BOHC_OOS (1U << 1)
#define AHCI_PORT_BASE 0x100U
#define AHCI_PORT_STRIDE 0x80U
#define AHCI_PXCMD_OFFSET 0x18U
#define AHCI_PXTFD_OFFSET 0x20U
#define AHCI_PXSIG_OFFSET 0x24U
#define AHCI_PXSSTS_OFFSET 0x28U
#define AHCI_PXCMD_ST (1U << 0)
#define AHCI_PXCMD_FRE (1U << 4)
#define AHCI_PXCMD_FR (1U << 14)
#define AHCI_PXCMD_CR (1U << 15)

struct ahci_runtime_state {
    volatile uint8_t *mmio;
    uint16_t original_pci_command;
    uint32_t original_ghc;
    uint32_t original_bohc;
    bool pci_command_changed;
    bool ghc_changed;
    bool bohc_changed;
};

static struct ahci_runtime_state runtime_state;
static struct ahci_state active_state;

static void bytes_zero(void *buffer, size_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    size_t index;
    for (index = 0U; index < length; ++index) {
        bytes[index] = 0U;
    }
}

static uint32_t mmio_read32(const volatile uint8_t *base, uint32_t offset) {
    return *(const volatile uint32_t *)(const volatile void *)(base + offset);
}

static void mmio_write32(volatile uint8_t *base, uint32_t offset,
                         uint32_t value) {
    *(volatile uint32_t *)(volatile void *)(base + offset) = value;
}

static bool poll_mmio32(void *context, uint32_t offset, uint32_t *value) {
    const volatile uint8_t *base = (const volatile uint8_t *)context;
    if ((base == NULL) || (value == NULL) ||
        (offset > (AHCI_MMIO_WINDOW_SIZE - sizeof(uint32_t)))) {
        return false;
    }
    x86_64_pause();
    *value = mmio_read32(base, offset);
    return true;
}

static bool pci_enable_memory(const struct pci_device *device) {
    uint16_t command;
    uint16_t verify;

    if ((device == NULL) ||
        !pci_config_read16(device->bdf, 0x04U, &command)) {
        return false;
    }
    runtime_state.original_pci_command = command;
    if ((command & AHCI_PCI_COMMAND_MEMORY) != 0U) {
        return true;
    }
    if (!pci_config_write16(device->bdf, 0x04U,
                            (uint16_t)(command | AHCI_PCI_COMMAND_MEMORY)) ||
        !pci_config_read16(device->bdf, 0x04U, &verify) ||
        ((verify & AHCI_PCI_COMMAND_MEMORY) == 0U)) {
        return false;
    }
    runtime_state.pci_command_changed = true;
    return true;
}

static bool bios_handoff(volatile uint8_t *base, struct ahci_state *state) {
    uint32_t last;

    state->bohc = mmio_read32(base, AHCI_BOHC_OFFSET);
    if ((state->cap2 & AHCI_CAP2_BOH) == 0U) {
        state->bios_handoff_complete = true;
        return true;
    }

    runtime_state.original_bohc = state->bohc;
    if ((state->bohc & AHCI_BOHC_OOS) == 0U) {
        mmio_write32(base, AHCI_BOHC_OFFSET, state->bohc | AHCI_BOHC_OOS);
        runtime_state.bohc_changed = true;
    }
    if (!ahci_wait_mask_bounded(poll_mmio32, (void *)base,
                                AHCI_BOHC_OFFSET, AHCI_BOHC_BOS, false,
                                AHCI_WAIT_LIMIT, &last)) {
        state->bohc = last;
        return false;
    }
    state->bohc = last;
    state->bios_handoff_complete = (last & AHCI_BOHC_OOS) != 0U;
    return state->bios_handoff_complete;
}

static bool enable_ahci_mode(volatile uint8_t *base, struct ahci_state *state) {
    uint32_t last;
    uint32_t ghc = mmio_read32(base, AHCI_GHC_OFFSET);

    runtime_state.original_ghc = ghc;
    if ((ghc & AHCI_GHC_HR) != 0U) {
        if (!ahci_wait_mask_bounded(poll_mmio32, (void *)base,
                                    AHCI_GHC_OFFSET, AHCI_GHC_HR, false,
                                    AHCI_WAIT_LIMIT, &last)) {
            return false;
        }
        ghc = last;
        runtime_state.original_ghc = ghc;
    }
    if ((ghc & AHCI_GHC_AE) == 0U) {
        mmio_write32(base, AHCI_GHC_OFFSET, ghc | AHCI_GHC_AE);
        runtime_state.ghc_changed = true;
    }
    state->ghc = mmio_read32(base, AHCI_GHC_OFFSET);
    state->ahci_enabled = (state->ghc & AHCI_GHC_AE) != 0U;
    return state->ahci_enabled && ((state->ghc & AHCI_GHC_HR) == 0U);
}

static bool inspect_ports(volatile uint8_t *base, struct ahci_state *state) {
    uint32_t implemented_mask;
    uint8_t port;

    if (!ahci_bounded_ports(state->cap, state->pi,
                            &state->hardware_ports,
                            &state->inspected_ports,
                            &implemented_mask,
                            &state->ports_truncated)) {
        return false;
    }

    for (port = 0U; port < state->inspected_ports; ++port) {
        struct ahci_port_state *port_state = &state->ports[port];
        const uint32_t port_base = AHCI_PORT_BASE +
            ((uint32_t)port * AHCI_PORT_STRIDE);

        port_state->index = port;
        if ((implemented_mask & (1U << port)) == 0U) {
            continue;
        }
        if (port_base > (AHCI_MMIO_WINDOW_SIZE - AHCI_PORT_STRIDE)) {
            return false;
        }

        port_state->implemented = true;
        port_state->cmd = mmio_read32(base, port_base + AHCI_PXCMD_OFFSET);
        port_state->tfd = mmio_read32(base, port_base + AHCI_PXTFD_OFFSET);
        port_state->sig = mmio_read32(base, port_base + AHCI_PXSIG_OFFSET);
        port_state->ssts = mmio_read32(base, port_base + AHCI_PXSSTS_OFFSET);
        port_state->engine_active =
            (port_state->cmd & (AHCI_PXCMD_ST | AHCI_PXCMD_FRE |
                                AHCI_PXCMD_FR | AHCI_PXCMD_CR)) != 0U;
        if (!ahci_decode_port_status(port_state->ssts, port_state->sig,
                                     &port_state->facts)) {
            return false;
        }
        ++state->implemented_ports;
        if (port_state->facts.present) {
            ++state->present_ports;
        }
        if (port_state->facts.sata) {
            ++state->sata_ports;
        }
    }
    return true;
}

static bool rollback(void) {
    bool ok = true;

    if (runtime_state.mmio != NULL) {
        if (runtime_state.ghc_changed) {
            mmio_write32(runtime_state.mmio, AHCI_GHC_OFFSET,
                         runtime_state.original_ghc);
        }
        if (runtime_state.bohc_changed) {
            mmio_write32(runtime_state.mmio, AHCI_BOHC_OFFSET,
                         runtime_state.original_bohc);
        }
        if (!vmm_unmap_mmio_region(runtime_state.mmio,
                                   AHCI_MMIO_WINDOW_SIZE)) {
            ok = false;
        }
    }
    if (runtime_state.pci_command_changed) {
        if (!pci_config_write16(active_state.device.bdf, 0x04U,
                                runtime_state.original_pci_command)) {
            ok = false;
        }
    }
    bytes_zero(&runtime_state, sizeof(runtime_state));
    return ok;
}

bool ahci_init(struct ahci_state *state) {
    struct pci_bar bar;
    volatile void *mapping = NULL;
    const struct boring_pci_inventory *inventory = boring_pci_inventory_get();
    bool ok = false;

    if (state == NULL) {
        return false;
    }
    bytes_zero(state, sizeof(*state));
    bytes_zero(&active_state, sizeof(active_state));
    bytes_zero(&runtime_state, sizeof(runtime_state));

    if (!ahci_select_controller(inventory, &active_state.device,
                                &active_state.prog_if) ||
        !pci_get_bar(&active_state.device, AHCI_ABAR_INDEX, &bar) ||
        !ahci_validate_abar(&bar, AHCI_MMIO_WINDOW_SIZE) ||
        !pci_enable_memory(&active_state.device) ||
        !vmm_map_mmio_region(bar.base, AHCI_MMIO_WINDOW_SIZE, &mapping)) {
        goto out;
    }

    runtime_state.mmio = (volatile uint8_t *)mapping;
    active_state.abar_physical = bar.base;
    active_state.cap = mmio_read32(runtime_state.mmio, AHCI_CAP_OFFSET);
    active_state.cap2 = mmio_read32(runtime_state.mmio, AHCI_CAP2_OFFSET);
    active_state.vs = mmio_read32(runtime_state.mmio, AHCI_VS_OFFSET);
    active_state.pi = mmio_read32(runtime_state.mmio, AHCI_PI_OFFSET);

    if ((active_state.vs == 0U) ||
        !bios_handoff(runtime_state.mmio, &active_state) ||
        !enable_ahci_mode(runtime_state.mmio, &active_state) ||
        !inspect_ports(runtime_state.mmio, &active_state)) {
        goto out;
    }

    active_state.initialized = true;
    *state = active_state;
    ok = true;

out:
    if (!ok) {
        (void)rollback();
        bytes_zero(&active_state, sizeof(active_state));
        bytes_zero(state, sizeof(*state));
    }
    return ok;
}

bool ahci_shutdown(void) {
    const bool ok = rollback();
    bytes_zero(&active_state, sizeof(active_state));
    return ok;
}

const struct ahci_state *ahci_get_state(void) {
    return &active_state;
}
