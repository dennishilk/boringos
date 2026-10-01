#ifndef BORING_NET_H
#define BORING_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/syscall_abi.h>

enum net_result {
    NET_RESULT_OK = 0,
    NET_RESULT_NO_DEVICE,
    NET_RESULT_UNSUPPORTED_DEVICE,
    NET_RESULT_DRIVER_ERROR,
    NET_RESULT_LINK_DOWN,
    NET_RESULT_DHCP_FAILED,
    NET_RESULT_DNS_FAILED,
    NET_RESULT_INVALID_HOST
};

enum net_result net_ping_host(const char *host, size_t host_length,
                              struct boring_net_ping_result *result);

/* Pure bounded parsers/checksums are public for host-side hostile-packet tests. */
uint16_t net_checksum(const uint8_t *data, size_t length);
bool net_parse_ipv4_literal(const char *text, size_t length, uint32_t *address);
bool net_dns_skip_name(const uint8_t *packet, size_t length, size_t offset,
                       size_t *consumed);

#endif
