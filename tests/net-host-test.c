#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <boring/cpu.h>
#include <boring/e1000.h>
#include <boring/net.h>
#include <boring/timer.h>

static unsigned failures;

static void check(bool ok, const char *name) {
    if (!ok) {
        (void)fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

void x86_64_pause(void) {}

enum e1000_result e1000_init(void) {
    return E1000_RESULT_NO_DEVICE;
}

bool e1000_send(const void *frame, size_t length) {
    (void)frame;
    (void)length;
    return false;
}

bool e1000_receive(void *frame, size_t capacity, size_t *length) {
    (void)frame;
    (void)capacity;
    if (length != NULL) {
        *length = 0U;
    }
    return true;
}

bool e1000_get_info(struct e1000_info *info) {
    if (info == NULL) {
        return false;
    }
    *info = (struct e1000_info){0};
    return true;
}

bool timer_get_stats(struct timer_stats *stats) {
    if (stats == NULL) {
        return false;
    }
    *stats = (struct timer_stats){1193182U, 100U, 11932U, 99998U};
    return true;
}

uint64_t timer_ticks(void) {
    return 1ULL;
}

int main(void) {
    uint32_t ip = 0U;
    const uint8_t iphdr[20] = {
        0x45U, 0U, 0U, 20U, 0U, 0U, 0U, 0U, 64U, 1U,
        0x7cU, 0xe7U, 0x7fU, 0U, 0U, 1U, 0x7fU, 0U, 0U, 1U
    };
    uint8_t dns[32] = {0};
    size_t consumed = 0U;

    check(net_parse_ipv4_literal("1.1.1.1", 7U, &ip) &&
          (ip == 0x01010101U), "ipv4 simple");
    check(net_parse_ipv4_literal("255.255.255.255", 15U, &ip) &&
          (ip == 0xffffffffU), "ipv4 max");
    check(!net_parse_ipv4_literal("256.1.1.1", 9U, &ip), "ipv4 overflow");
    check(!net_parse_ipv4_literal("1..1.1", 6U, &ip),
          "ipv4 empty octet");
    check(net_checksum(iphdr, sizeof(iphdr)) == 0U, "ipv4 checksum");

    dns[0] = 3U;
    dns[1] = 'w';
    dns[2] = 'w';
    dns[3] = 'w';
    dns[4] = 0U;
    check(net_dns_skip_name(dns, sizeof(dns), 0U, &consumed) &&
          (consumed == 5U), "dns plain name");
    dns[8] = 0xc0U;
    dns[9] = 0U;
    check(net_dns_skip_name(dns, sizeof(dns), 8U, &consumed) &&
          (consumed == 2U), "dns pointer");
    dns[12] = 0xc0U;
    dns[13] = 12U;
    check(!net_dns_skip_name(dns, sizeof(dns), 12U, &consumed),
          "dns pointer loop rejected");
    dns[16] = 64U;
    check(!net_dns_skip_name(dns, sizeof(dns), 16U, &consumed),
          "dns oversized label rejected");
    check(!net_dns_skip_name(dns, 1U, 1U, &consumed),
          "dns offset bounds");

    if (failures != 0U) {
        return EXIT_FAILURE;
    }
    (void)puts("net-host-test: PASS");
    return EXIT_SUCCESS;
}
