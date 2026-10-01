#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/e1000.h>
#include <boring/io.h>
#include <boring/net.h>
#include <boring/timer.h>

#define NET_ETH_HEADER 14U
#define NET_IPV4_HEADER 20U
#define NET_UDP_HEADER 8U
#define NET_ICMP_HEADER 8U
#define NET_FRAME_MAX 1518U
#define NET_ETHERTYPE_IPV4 0x0800U
#define NET_ETHERTYPE_ARP 0x0806U
#define NET_IP_PROTO_ICMP 1U
#define NET_IP_PROTO_UDP 17U
#define NET_ARP_BYTES 28U
#define NET_ARP_CACHE 8U
#define NET_DHCP_CLIENT_PORT 68U
#define NET_DHCP_SERVER_PORT 67U
#define NET_DNS_PORT 53U
#define NET_DHCP_FIXED 240U
#define NET_DHCP_MIN_MESSAGE 300U
#define NET_DHCP_MAGIC 0x63825363U
#define NET_DHCP_DISCOVER 1U
#define NET_DHCP_OFFER 2U
#define NET_DHCP_REQUEST 3U
#define NET_DHCP_ACK 5U
#define NET_DHCP_NAK 6U
#define NET_DHCP_OPTION_SUBNET 1U
#define NET_DHCP_OPTION_ROUTER 3U
#define NET_DHCP_OPTION_DNS 6U
#define NET_DHCP_OPTION_REQUESTED_IP 50U
#define NET_DHCP_OPTION_LEASE 51U
#define NET_DHCP_OPTION_MESSAGE_TYPE 53U
#define NET_DHCP_OPTION_SERVER_ID 54U
#define NET_DHCP_OPTION_PARAMETER_LIST 55U
#define NET_DHCP_OPTION_CLIENT_ID 61U
#define NET_DHCP_OPTION_END 255U
#define NET_DHCP_OPTION_PAD 0U
#define NET_DHCP_TIMEOUT_MS 3000U
#define NET_ARP_TIMEOUT_MS 1500U
#define NET_DNS_TIMEOUT_MS 2500U
#define NET_PING_TIMEOUT_MS 1500U
#define NET_POLL_SPIN_BUDGET 1024U
#define NET_DNS_MAX_POINTER_DEPTH 8U
#define NET_DNS_MAX_RECORDS 64U
#define NET_ICMP_PAYLOAD 56U
#define NET_IPV4_BROADCAST 0xffffffffU
#define NET_IPV4_ZERO 0U
#define NET_RTT_TIMEOUT UINT32_MAX
struct net_arp_entry {
    bool valid;
    uint32_t address;
    uint8_t mac[6];
};

struct net_config {
    bool configured;
    uint32_t address;
    uint32_t subnet_mask;
    uint32_t gateway;
    uint32_t dns;
    uint32_t server;
};

struct net_udp_view {
    uint32_t source_ip;
    uint32_t destination_ip;
    uint16_t source_port;
    uint16_t destination_port;
    const uint8_t *payload;
    size_t payload_length;
};

struct net_clock {
    uint64_t start_ticks;
    uint32_t frequency_millihz;
};

struct dhcp_offer {
    uint32_t address;
    uint32_t subnet_mask;
    uint32_t gateway;
    uint32_t dns;
    uint32_t server;
    uint8_t message_type;
};

static uint8_t frame_buffer[NET_FRAME_MAX];
static uint8_t transmit_buffer[NET_FRAME_MAX];
static struct net_arp_entry arp_cache[NET_ARP_CACHE];
static struct net_config config;
static struct e1000_info nic_info;
static size_t arp_replace;
static uint16_t ipv4_identification;
static uint16_t dns_sequence;
static uint32_t dhcp_sequence;
static bool network_initialized;

static uint16_t read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) |
           ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) |
           (uint32_t)bytes[3];
}

static void write_be16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8U);
    bytes[1] = (uint8_t)(value & 0xffU);
}

static void write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)((value >> 16U) & 0xffU);
    bytes[2] = (uint8_t)((value >> 8U) & 0xffU);
    bytes[3] = (uint8_t)(value & 0xffU);
}

static void bytes_zero(void *buffer, size_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    size_t index;
    for (index = 0U; index < length; ++index) {
        bytes[index] = 0U;
    }
}

static void bytes_copy(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    size_t index;
    for (index = 0U; index < length; ++index) {
        out[index] = in[index];
    }
}

static bool bytes_equal(const uint8_t *left, const uint8_t *right,
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

uint16_t net_checksum(const uint8_t *data, size_t length) {
    uint32_t sum = 0U;
    size_t offset = 0U;

    if ((data == NULL) && (length != 0U)) {
        return 0xffffU;
    }
    while ((length - offset) >= 2U) {
        sum += (uint32_t)read_be16(&data[offset]);
        offset += 2U;
    }
    if (offset < length) {
        sum += (uint32_t)data[offset] << 8U;
    }
    while ((sum >> 16U) != 0U) {
        sum = (sum & 0xffffU) + (sum >> 16U);
    }
    return (uint16_t)(~sum & 0xffffU);
}

static uint16_t udp_checksum(uint32_t source_ip, uint32_t destination_ip,
                             const uint8_t *udp, size_t udp_length) {
    uint32_t sum = 0U;
    size_t offset = 0U;

    sum += (source_ip >> 16U) & 0xffffU;
    sum += source_ip & 0xffffU;
    sum += (destination_ip >> 16U) & 0xffffU;
    sum += destination_ip & 0xffffU;
    sum += NET_IP_PROTO_UDP;
    sum += (uint32_t)udp_length;
    while ((udp_length - offset) >= 2U) {
        sum += (uint32_t)read_be16(&udp[offset]);
        offset += 2U;
    }
    if (offset < udp_length) {
        sum += (uint32_t)udp[offset] << 8U;
    }
    while ((sum >> 16U) != 0U) {
        sum = (sum & 0xffffU) + (sum >> 16U);
    }
    return (uint16_t)(~sum & 0xffffU);
}

bool net_parse_ipv4_literal(const char *text, size_t length, uint32_t *address) {
    uint32_t parts[4] = {0U, 0U, 0U, 0U};
    size_t part = 0U;
    size_t digits = 0U;
    size_t index;

    if ((text == NULL) || (address == NULL) || (length < 7U) ||
        (length > 15U)) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        const char character = text[index];
        if (character == '.') {
            if ((digits == 0U) || (part >= 3U)) {
                return false;
            }
            ++part;
            digits = 0U;
            continue;
        }
        if ((character < '0') || (character > '9') || (digits >= 3U)) {
            return false;
        }
        parts[part] = (parts[part] * 10U) +
                      (uint32_t)(character - '0');
        if (parts[part] > 255U) {
            return false;
        }
        ++digits;
    }
    if ((part != 3U) || (digits == 0U)) {
        return false;
    }
    *address = (parts[0] << 24U) | (parts[1] << 16U) |
               (parts[2] << 8U) | parts[3];
    return true;
}

static bool dns_validate_name(const uint8_t *packet, size_t length,
                              size_t offset, unsigned int depth) {
    size_t cursor = offset;

    if ((packet == NULL) || (depth > NET_DNS_MAX_POINTER_DEPTH) ||
        (offset >= length)) {
        return false;
    }
    for (;;) {
        uint8_t label;
        if (cursor >= length) {
            return false;
        }
        label = packet[cursor];
        if (label == 0U) {
            return true;
        }
        if ((label & 0xc0U) == 0xc0U) {
            uint16_t pointer;
            if ((length - cursor) < 2U) {
                return false;
            }
            pointer = (uint16_t)(((uint16_t)(label & 0x3fU) << 8U) |
                                 (uint16_t)packet[cursor + 1U]);
            if ((size_t)pointer >= length) {
                return false;
            }
            return dns_validate_name(packet, length, (size_t)pointer,
                                     depth + 1U);
        }
        if (((label & 0xc0U) != 0U) || (label > 63U)) {
            return false;
        }
        ++cursor;
        if ((size_t)label > (length - cursor)) {
            return false;
        }
        cursor += (size_t)label;
    }
}

bool net_dns_skip_name(const uint8_t *packet, size_t length, size_t offset,
                       size_t *consumed) {
    size_t cursor = offset;

    if ((packet == NULL) || (consumed == NULL) || (offset >= length)) {
        return false;
    }
    for (;;) {
        uint8_t label;
        if (cursor >= length) {
            return false;
        }
        label = packet[cursor];
        if (label == 0U) {
            *consumed = (cursor - offset) + 1U;
            return true;
        }
        if ((label & 0xc0U) == 0xc0U) {
            uint16_t pointer;
            if ((length - cursor) < 2U) {
                return false;
            }
            pointer = (uint16_t)(((uint16_t)(label & 0x3fU) << 8U) |
                                 (uint16_t)packet[cursor + 1U]);
            if (((size_t)pointer >= length) ||
                !dns_validate_name(packet, length, (size_t)pointer, 1U)) {
                return false;
            }
            *consumed = (cursor - offset) + 2U;
            return true;
        }
        if (((label & 0xc0U) != 0U) || (label > 63U)) {
            return false;
        }
        ++cursor;
        if ((size_t)label > (length - cursor)) {
            return false;
        }
        cursor += (size_t)label;
    }
}

static bool net_clock_start(struct net_clock *clock) {
    struct timer_stats stats;

    if ((clock == NULL) || !timer_get_stats(&stats) ||
        (stats.effective_frequency_millihz == 0U)) {
        return false;
    }
    clock->start_ticks = timer_ticks();
    clock->frequency_millihz = stats.effective_frequency_millihz;
    return true;
}

static uint64_t net_clock_elapsed_us(const struct net_clock *clock) {
    uint64_t elapsed_ticks;

    if ((clock == NULL) || (clock->frequency_millihz == 0U)) {
        return UINT64_MAX;
    }
    elapsed_ticks = timer_ticks() - clock->start_ticks;
    if (elapsed_ticks > (UINT64_MAX / 1000000ULL)) {
        return UINT64_MAX;
    }
    return (elapsed_ticks * 1000000ULL) /
           (uint64_t)clock->frequency_millihz;
}

static bool net_clock_expired(struct net_clock *clock, uint32_t milliseconds) {
    const uint64_t elapsed_us = net_clock_elapsed_us(clock);
    uint64_t target_us;

    if ((uint64_t)milliseconds > (UINT64_MAX / 1000ULL)) {
        return true;
    }
    target_us = (uint64_t)milliseconds * 1000ULL;
    return elapsed_us >= target_us;
}

static uint32_t net_clock_elapsed_ms(struct net_clock *clock) {
    const uint64_t elapsed_us = net_clock_elapsed_us(clock);
    const uint64_t elapsed_ms = elapsed_us / 1000ULL;

    return (elapsed_ms > (uint64_t)UINT32_MAX) ?
        UINT32_MAX : (uint32_t)elapsed_ms;
}

static void ethernet_header(uint8_t *frame, const uint8_t destination[6],
                            uint16_t ethertype) {
    bytes_copy(&frame[0], destination, 6U);
    bytes_copy(&frame[6], nic_info.mac, 6U);
    write_be16(&frame[12], ethertype);
}

static size_t ipv4_header(uint8_t *packet, uint16_t payload_length,
                          uint8_t protocol, uint32_t source,
                          uint32_t destination) {
    const uint16_t total = (uint16_t)(NET_IPV4_HEADER + payload_length);

    bytes_zero(packet, NET_IPV4_HEADER);
    packet[0] = 0x45U;
    write_be16(&packet[2], total);
    ++ipv4_identification;
    write_be16(&packet[4], ipv4_identification);
    write_be16(&packet[6], 0x4000U);
    packet[8] = 64U;
    packet[9] = protocol;
    write_be32(&packet[12], source);
    write_be32(&packet[16], destination);
    write_be16(&packet[10], net_checksum(packet, NET_IPV4_HEADER));
    return NET_IPV4_HEADER;
}

static bool mac_is_broadcast(const uint8_t mac[6]) {
    static const uint8_t broadcast[6] =
        {0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU};
    return bytes_equal(mac, broadcast, 6U);
}

static bool mac_for_us(const uint8_t mac[6]) {
    return bytes_equal(mac, nic_info.mac, 6U) || mac_is_broadcast(mac);
}

static void arp_cache_put(uint32_t address, const uint8_t mac[6]) {
    size_t index;

    if ((address == NET_IPV4_ZERO) || (address == NET_IPV4_BROADCAST) ||
        (mac == NULL) || ((mac[0] & 1U) != 0U)) {
        return;
    }
    for (index = 0U; index < NET_ARP_CACHE; ++index) {
        if (arp_cache[index].valid &&
            (arp_cache[index].address == address)) {
            bytes_copy(arp_cache[index].mac, mac, 6U);
            return;
        }
    }
    arp_cache[arp_replace].valid = true;
    arp_cache[arp_replace].address = address;
    bytes_copy(arp_cache[arp_replace].mac, mac, 6U);
    arp_replace = (arp_replace + 1U) % NET_ARP_CACHE;
}

static bool arp_cache_get(uint32_t address, uint8_t mac[6]) {
    size_t index;

    for (index = 0U; index < NET_ARP_CACHE; ++index) {
        if (arp_cache[index].valid &&
            (arp_cache[index].address == address)) {
            bytes_copy(mac, arp_cache[index].mac, 6U);
            return true;
        }
    }
    return false;
}

static bool send_arp(uint16_t operation, const uint8_t destination_mac[6],
                     uint32_t target_ip, const uint8_t target_mac[6]) {
    uint8_t *arp = &transmit_buffer[NET_ETH_HEADER];

    ethernet_header(transmit_buffer, destination_mac, NET_ETHERTYPE_ARP);
    write_be16(&arp[0], 1U);
    write_be16(&arp[2], NET_ETHERTYPE_IPV4);
    arp[4] = 6U;
    arp[5] = 4U;
    write_be16(&arp[6], operation);
    bytes_copy(&arp[8], nic_info.mac, 6U);
    write_be32(&arp[14], config.address);
    bytes_copy(&arp[18], target_mac, 6U);
    write_be32(&arp[24], target_ip);
    return e1000_send(transmit_buffer, NET_ETH_HEADER + NET_ARP_BYTES);
}

static void process_arp(const uint8_t *frame, size_t length) {
    static const uint8_t zero_mac[6] = {0U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t *arp;
    uint16_t operation;
    uint32_t sender_ip;
    uint32_t target_ip;

    if ((frame == NULL) || (length < (NET_ETH_HEADER + NET_ARP_BYTES)) ||
        !mac_for_us(&frame[0]) ||
        (read_be16(&frame[12]) != NET_ETHERTYPE_ARP)) {
        return;
    }
    arp = &frame[NET_ETH_HEADER];
    if ((read_be16(&arp[0]) != 1U) ||
        (read_be16(&arp[2]) != NET_ETHERTYPE_IPV4) ||
        (arp[4] != 6U) || (arp[5] != 4U)) {
        return;
    }
    operation = read_be16(&arp[6]);
    if ((operation != 1U) && (operation != 2U)) {
        return;
    }
    sender_ip = read_be32(&arp[14]);
    target_ip = read_be32(&arp[24]);
    if (operation == 2U) {
        if (!config.configured || (target_ip != config.address) ||
            !bytes_equal(&arp[18], nic_info.mac, 6U)) {
            return;
        }
        if ((sender_ip != NET_IPV4_ZERO) && ((arp[8] & 1U) == 0U)) {
            arp_cache_put(sender_ip, &arp[8]);
        }
        return;
    }
    if (config.configured && (target_ip == config.address) &&
        !bytes_equal(&arp[8], zero_mac, 6U)) {
        if ((sender_ip != NET_IPV4_ZERO) && ((arp[8] & 1U) == 0U)) {
            arp_cache_put(sender_ip, &arp[8]);
        }
        (void)send_arp(2U, &arp[8], sender_ip, &arp[8]);
    }
}

static bool poll_frame(size_t *length) {
    size_t spins;

    if (length == NULL) {
        return false;
    }
    for (spins = 0U; spins < NET_POLL_SPIN_BUDGET; ++spins) {
        size_t received = 0U;
        if (!e1000_receive(frame_buffer, sizeof(frame_buffer), &received)) {
            return false;
        }
        if (received != 0U) {
            *length = received;
            if ((received >= NET_ETH_HEADER) &&
                (read_be16(&frame_buffer[12]) == NET_ETHERTYPE_ARP)) {
                process_arp(frame_buffer, received);
            }
            return true;
        }
        x86_64_pause();
    }
    *length = 0U;
    return true;
}

static bool resolve_mac(uint32_t destination_ip, uint8_t mac[6]) {
    static const uint8_t broadcast[6] =
        {0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU};
    static const uint8_t zero_mac[6] = {0U, 0U, 0U, 0U, 0U, 0U};
    uint32_t next_hop;
    struct net_clock clock;

    if ((mac == NULL) || !config.configured || (config.address == 0U)) {
        return false;
    }
    if (destination_ip == NET_IPV4_BROADCAST) {
        bytes_copy(mac, broadcast, 6U);
        return true;
    }
    if ((destination_ip & config.subnet_mask) ==
        (config.address & config.subnet_mask)) {
        next_hop = destination_ip;
    } else {
        next_hop = config.gateway;
    }
    if ((next_hop == 0U) || (next_hop == NET_IPV4_BROADCAST)) {
        return false;
    }
    if (arp_cache_get(next_hop, mac)) {
        return true;
    }
    if (!send_arp(1U, broadcast, next_hop, zero_mac)) {
        return false;
    }
    if (!net_clock_start(&clock)) {
        return false;
    }
    while (!net_clock_expired(&clock, NET_ARP_TIMEOUT_MS)) {
        size_t length = 0U;
        if (!poll_frame(&length)) {
            return false;
        }
        if (arp_cache_get(next_hop, mac)) {
            return true;
        }
    }
    return false;
}

static bool parse_udp(const uint8_t *frame, size_t frame_length,
                      struct net_udp_view *view) {
    const uint8_t *ip;
    const uint8_t *udp;
    size_t ihl;
    uint16_t total_length;
    uint16_t fragment;
    uint16_t udp_length;
    uint16_t checksum;
    uint32_t source_ip;
    uint32_t destination_ip;

    if ((frame == NULL) || (view == NULL) ||
        (frame_length < (NET_ETH_HEADER + NET_IPV4_HEADER + NET_UDP_HEADER)) ||
        !mac_for_us(&frame[0]) ||
        (read_be16(&frame[12]) != NET_ETHERTYPE_IPV4)) {
        return false;
    }
    ip = &frame[NET_ETH_HEADER];
    if ((ip[0] >> 4U) != 4U) {
        return false;
    }
    ihl = (size_t)(ip[0] & 0x0fU) * 4U;
    if ((ihl < NET_IPV4_HEADER) ||
        (ihl > (frame_length - NET_ETH_HEADER))) {
        return false;
    }
    total_length = read_be16(&ip[2]);
    if (((size_t)total_length < (ihl + NET_UDP_HEADER)) ||
        ((size_t)total_length > (frame_length - NET_ETH_HEADER)) ||
        (net_checksum(ip, ihl) != 0U) ||
        (ip[9] != NET_IP_PROTO_UDP)) {
        return false;
    }
    fragment = read_be16(&ip[6]);
    if ((fragment & 0x3fffU) != 0U) {
        return false;
    }
    source_ip = read_be32(&ip[12]);
    destination_ip = read_be32(&ip[16]);
    udp = &ip[ihl];
    udp_length = read_be16(&udp[4]);
    if (((size_t)udp_length < NET_UDP_HEADER) ||
        ((size_t)udp_length > ((size_t)total_length - ihl))) {
        return false;
    }
    checksum = read_be16(&udp[6]);
    if ((checksum != 0U) &&
        (udp_checksum(source_ip, destination_ip, udp,
                      (size_t)udp_length) != 0U)) {
        return false;
    }
    view->source_ip = source_ip;
    view->destination_ip = destination_ip;
    view->source_port = read_be16(&udp[0]);
    view->destination_port = read_be16(&udp[2]);
    view->payload = &udp[NET_UDP_HEADER];
    view->payload_length = (size_t)udp_length - NET_UDP_HEADER;
    return true;
}

static bool send_udp(const uint8_t destination_mac[6], uint32_t source_ip,
                     uint32_t destination_ip, uint16_t source_port,
                     uint16_t destination_port, const uint8_t *payload,
                     size_t payload_length) {
    uint8_t *ip = &transmit_buffer[NET_ETH_HEADER];
    uint8_t *udp = &ip[NET_IPV4_HEADER];
    size_t total;
    uint16_t udp_length;
    uint16_t checksum;

    if ((destination_mac == NULL) ||
        ((payload == NULL) && (payload_length != 0U)) ||
        (payload_length > 1472U)) {
        return false;
    }
    total = NET_ETH_HEADER + NET_IPV4_HEADER + NET_UDP_HEADER + payload_length;
    if ((total > sizeof(transmit_buffer)) ||
        ((NET_UDP_HEADER + payload_length) > (size_t)UINT16_MAX)) {
        return false;
    }
    ethernet_header(transmit_buffer, destination_mac, NET_ETHERTYPE_IPV4);
    (void)ipv4_header(ip, (uint16_t)(NET_UDP_HEADER + payload_length),
                      NET_IP_PROTO_UDP, source_ip, destination_ip);
    udp_length = (uint16_t)(NET_UDP_HEADER + payload_length);
    write_be16(&udp[0], source_port);
    write_be16(&udp[2], destination_port);
    write_be16(&udp[4], udp_length);
    write_be16(&udp[6], 0U);
    if (payload_length != 0U) {
        bytes_copy(&udp[NET_UDP_HEADER], payload, payload_length);
    }
    checksum = udp_checksum(source_ip, destination_ip, udp, udp_length);
    if (checksum == 0U) {
        checksum = 0xffffU;
    }
    write_be16(&udp[6], checksum);
    return e1000_send(transmit_buffer, total);
}

static bool dhcp_options_well_formed(const uint8_t *options, size_t length) {
    size_t offset = 0U;

    if (options == NULL) {
        return false;
    }
    while (offset < length) {
        const uint8_t code = options[offset];
        if (code == NET_DHCP_OPTION_END) {
            return true;
        }
        if (code == NET_DHCP_OPTION_PAD) {
            ++offset;
            continue;
        }
        if ((length - offset) < 2U) {
            return false;
        }
        if ((size_t)options[offset + 1U] > (length - offset - 2U)) {
            return false;
        }
        offset += 2U + (size_t)options[offset + 1U];
    }
    return false;
}

static bool dhcp_option(const uint8_t *options, size_t length, uint8_t wanted,
                        const uint8_t **value, size_t *value_length) {
    size_t offset = 0U;

    if ((options == NULL) || (value == NULL) || (value_length == NULL)) {
        return false;
    }
    while (offset < length) {
        uint8_t code = options[offset];
        if (code == NET_DHCP_OPTION_END) {
            return false;
        }
        if (code == NET_DHCP_OPTION_PAD) {
            ++offset;
            continue;
        }
        if ((length - offset) < 2U) {
            return false;
        }
        if ((size_t)options[offset + 1U] > (length - offset - 2U)) {
            return false;
        }
        if (code == wanted) {
            *value = &options[offset + 2U];
            *value_length = (size_t)options[offset + 1U];
            return true;
        }
        offset += 2U + (size_t)options[offset + 1U];
    }
    return false;
}

static bool parse_dhcp(const struct net_udp_view *udp, uint32_t xid,
                       struct dhcp_offer *offer) {
    const uint8_t *packet;
    const uint8_t *value;
    size_t value_length;

    if ((udp == NULL) || (offer == NULL) ||
        (udp->source_port != NET_DHCP_SERVER_PORT) ||
        (udp->destination_port != NET_DHCP_CLIENT_PORT) ||
        (udp->payload_length < NET_DHCP_FIXED)) {
        return false;
    }
    packet = udp->payload;
    if ((packet[0] != 2U) || (packet[1] != 1U) || (packet[2] != 6U) ||
        (read_be32(&packet[4]) != xid) ||
        !bytes_equal(&packet[28], nic_info.mac, 6U) ||
        (read_be32(&packet[236]) != NET_DHCP_MAGIC) ||
        !dhcp_options_well_formed(&packet[NET_DHCP_FIXED],
            udp->payload_length - NET_DHCP_FIXED)) {
        return false;
    }
    bytes_zero(offer, sizeof(*offer));
    offer->address = read_be32(&packet[16]);
    if (!dhcp_option(&packet[NET_DHCP_FIXED],
                     udp->payload_length - NET_DHCP_FIXED,
                     NET_DHCP_OPTION_MESSAGE_TYPE, &value, &value_length) ||
        (value_length != 1U)) {
        return false;
    }
    offer->message_type = value[0];
    if (dhcp_option(&packet[NET_DHCP_FIXED],
                    udp->payload_length - NET_DHCP_FIXED,
                    NET_DHCP_OPTION_SUBNET, &value, &value_length) &&
        (value_length == 4U)) {
        offer->subnet_mask = read_be32(value);
    }
    if (dhcp_option(&packet[NET_DHCP_FIXED],
                    udp->payload_length - NET_DHCP_FIXED,
                    NET_DHCP_OPTION_ROUTER, &value, &value_length) &&
        (value_length >= 4U)) {
        offer->gateway = read_be32(value);
    }
    if (dhcp_option(&packet[NET_DHCP_FIXED],
                    udp->payload_length - NET_DHCP_FIXED,
                    NET_DHCP_OPTION_DNS, &value, &value_length) &&
        (value_length >= 4U)) {
        offer->dns = read_be32(value);
    }
    if (dhcp_option(&packet[NET_DHCP_FIXED],
                    udp->payload_length - NET_DHCP_FIXED,
                    NET_DHCP_OPTION_SERVER_ID, &value, &value_length) &&
        (value_length == 4U)) {
        offer->server = read_be32(value);
    }
    return true;
}

static size_t build_dhcp(uint8_t *packet, size_t capacity, uint32_t xid,
                         uint8_t message_type, uint32_t requested_ip,
                         uint32_t server_id) {
    size_t offset = NET_DHCP_FIXED;

    if ((packet == NULL) || (capacity < (NET_DHCP_FIXED + 32U))) {
        return 0U;
    }
    bytes_zero(packet, capacity);
    packet[0] = 1U;
    packet[1] = 1U;
    packet[2] = 6U;
    write_be32(&packet[4], xid);
    write_be16(&packet[10], 0x8000U);
    bytes_copy(&packet[28], nic_info.mac, 6U);
    write_be32(&packet[236], NET_DHCP_MAGIC);

    packet[offset++] = NET_DHCP_OPTION_MESSAGE_TYPE;
    packet[offset++] = 1U;
    packet[offset++] = message_type;
    packet[offset++] = NET_DHCP_OPTION_CLIENT_ID;
    packet[offset++] = 7U;
    packet[offset++] = 1U;
    bytes_copy(&packet[offset], nic_info.mac, 6U);
    offset += 6U;
    if (message_type == NET_DHCP_REQUEST) {
        packet[offset++] = NET_DHCP_OPTION_REQUESTED_IP;
        packet[offset++] = 4U;
        write_be32(&packet[offset], requested_ip);
        offset += 4U;
        packet[offset++] = NET_DHCP_OPTION_SERVER_ID;
        packet[offset++] = 4U;
        write_be32(&packet[offset], server_id);
        offset += 4U;
    }
    packet[offset++] = NET_DHCP_OPTION_PARAMETER_LIST;
    packet[offset++] = 4U;
    packet[offset++] = NET_DHCP_OPTION_SUBNET;
    packet[offset++] = NET_DHCP_OPTION_ROUTER;
    packet[offset++] = NET_DHCP_OPTION_DNS;
    packet[offset++] = NET_DHCP_OPTION_LEASE;
    packet[offset++] = NET_DHCP_OPTION_END;
    /*
     * Preserve the classic BOOTP/DHCP minimum message size. Some DHCP
     * servers, including QEMU user networking, validate the fixed BOOTP
     * packet footprint rather than accepting a short options tail.
     * The buffer was zeroed above, so the bounded remainder is padding.
     */
    if (offset < (size_t)NET_DHCP_MIN_MESSAGE) {
        offset = (size_t)NET_DHCP_MIN_MESSAGE;
    }
    return offset;
}

static bool wait_dhcp(uint32_t xid, uint8_t wanted_type,
                      struct dhcp_offer *offer) {
    struct net_clock clock;

    if ((offer == NULL) || !net_clock_start(&clock)) {
        return false;
    }
    while (!net_clock_expired(&clock, NET_DHCP_TIMEOUT_MS)) {
        struct net_udp_view udp;
        struct dhcp_offer candidate;
        size_t length = 0U;
        if (!poll_frame(&length)) {
            return false;
        }
        if ((length == 0U) ||
            !parse_udp(frame_buffer, length, &udp) ||
            !parse_dhcp(&udp, xid, &candidate) ||
            ((udp.destination_ip != NET_IPV4_BROADCAST) &&
             (udp.destination_ip != NET_IPV4_ZERO) &&
             (udp.destination_ip != candidate.address))) {
            continue;
        }
        if (candidate.message_type == NET_DHCP_NAK) {
            return false;
        }
        if (candidate.message_type != wanted_type) {
            continue;
        }
        *offer = candidate;
        return true;
    }
    return false;
}

static bool configure_dhcp(void) {
    static const uint8_t broadcast_mac[6] =
        {0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU};
    uint8_t packet[320];
    struct dhcp_offer offer;
    struct dhcp_offer ack;
    uint32_t xid;
    size_t length;

    if (config.configured) {
        return true;
    }
    ++dhcp_sequence;
    xid = 0x424f0000U ^ ((uint32_t)nic_info.mac[4] << 8U) ^
          (uint32_t)nic_info.mac[5] ^ dhcp_sequence;
    length = build_dhcp(packet, sizeof(packet), xid, NET_DHCP_DISCOVER,
                        0U, 0U);
    if ((length == 0U) ||
        !send_udp(broadcast_mac, NET_IPV4_ZERO, NET_IPV4_BROADCAST,
                  NET_DHCP_CLIENT_PORT, NET_DHCP_SERVER_PORT,
                  packet, length) ||
        !wait_dhcp(xid, NET_DHCP_OFFER, &offer) ||
        (offer.address == 0U) || (offer.server == 0U)) {
        return false;
    }
    length = build_dhcp(packet, sizeof(packet), xid, NET_DHCP_REQUEST,
                        offer.address, offer.server);
    if ((length == 0U) ||
        !send_udp(broadcast_mac, NET_IPV4_ZERO, NET_IPV4_BROADCAST,
                  NET_DHCP_CLIENT_PORT, NET_DHCP_SERVER_PORT,
                  packet, length) ||
        !wait_dhcp(xid, NET_DHCP_ACK, &ack)) {
        return false;
    }
    config.address = (ack.address != 0U) ? ack.address : offer.address;
    config.subnet_mask = (ack.subnet_mask != 0U) ?
        ack.subnet_mask : offer.subnet_mask;
    config.gateway = (ack.gateway != 0U) ? ack.gateway : offer.gateway;
    config.dns = (ack.dns != 0U) ? ack.dns : offer.dns;
    config.server = (ack.server != 0U) ? ack.server : offer.server;
    if ((config.address == 0U) || (config.subnet_mask == 0U) ||
        (config.gateway == 0U)) {
        bytes_zero(&config, sizeof(config));
        return false;
    }
    config.configured = true;
    return true;
}

static bool dns_encode_name(uint8_t *buffer, size_t capacity,
                            const char *name, size_t length,
                            size_t *encoded_length) {
    size_t source = 0U;
    size_t output = 0U;

    if ((buffer == NULL) || (name == NULL) || (encoded_length == NULL) ||
        (length == 0U) || (length > BORING_NET_HOST_MAX)) {
        return false;
    }
    while (source < length) {
        size_t label_start = source;
        size_t label_length = 0U;
        while ((source < length) && (name[source] != '.')) {
            const char c = name[source];
            const bool allowed = ((c >= 'a') && (c <= 'z')) ||
                                 ((c >= 'A') && (c <= 'Z')) ||
                                 ((c >= '0') && (c <= '9')) ||
                                 (c == '-') || (c == '_');
            if (!allowed || (label_length >= 63U)) {
                return false;
            }
            ++label_length;
            ++source;
        }
        if ((label_length == 0U) ||
            ((1U + label_length) > (capacity - output))) {
            return false;
        }
        buffer[output++] = (uint8_t)label_length;
        bytes_copy(&buffer[output], &name[label_start], label_length);
        output += label_length;
        if (source < length) {
            ++source;
            if (source == length) {
                return false;
            }
        }
    }
    if (output >= capacity) {
        return false;
    }
    buffer[output++] = 0U;
    *encoded_length = output;
    return true;
}

static bool parse_dns_answer(const uint8_t *packet, size_t length,
                             uint16_t expected_id, uint32_t *address) {
    uint16_t flags;
    uint16_t questions;
    uint16_t answers;
    uint32_t total_records;
    size_t offset = 12U;
    uint16_t index;

    if ((packet == NULL) || (address == NULL) || (length < 12U) ||
        (read_be16(&packet[0]) != expected_id)) {
        return false;
    }
    flags = read_be16(&packet[2]);
    questions = read_be16(&packet[4]);
    answers = read_be16(&packet[6]);
    total_records = (uint32_t)questions + (uint32_t)answers +
                    (uint32_t)read_be16(&packet[8]) +
                    (uint32_t)read_be16(&packet[10]);
    if (((flags & 0x8000U) == 0U) || ((flags & 0x7800U) != 0U) ||
        ((flags & 0x0200U) != 0U) || ((flags & 0x000fU) != 0U) ||
        (questions == 0U) || (answers == 0U) ||
        (total_records > NET_DNS_MAX_RECORDS)) {
        return false;
    }
    for (index = 0U; index < questions; ++index) {
        size_t consumed;
        if (!net_dns_skip_name(packet, length, offset, &consumed) ||
            (consumed > (length - offset))) {
            return false;
        }
        offset += consumed;
        if ((length - offset) < 4U ||
            (read_be16(&packet[offset]) != 1U) ||
            (read_be16(&packet[offset + 2U]) != 1U)) {
            return false;
        }
        offset += 4U;
    }
    for (index = 0U; index < answers; ++index) {
        size_t consumed;
        uint16_t type;
        uint16_t class_code;
        uint16_t data_length;
        if (!net_dns_skip_name(packet, length, offset, &consumed) ||
            (consumed > (length - offset))) {
            return false;
        }
        offset += consumed;
        if ((length - offset) < 10U) {
            return false;
        }
        type = read_be16(&packet[offset]);
        class_code = read_be16(&packet[offset + 2U]);
        data_length = read_be16(&packet[offset + 8U]);
        offset += 10U;
        if ((size_t)data_length > (length - offset)) {
            return false;
        }
        if ((type == 1U) && (class_code == 1U) && (data_length == 4U)) {
            *address = read_be32(&packet[offset]);
            return *address != 0U;
        }
        offset += (size_t)data_length;
    }
    return false;
}

static bool resolve_dns(const char *host, size_t host_length,
                        uint32_t *address) {
    uint8_t query[512];
    uint8_t destination_mac[6];
    size_t encoded;
    size_t query_length;
    uint16_t id;
    uint16_t source_port;
    struct net_clock clock;

    if ((config.dns == 0U) || (address == NULL) ||
        !resolve_mac(config.dns, destination_mac)) {
        return false;
    }
    bytes_zero(query, sizeof(query));
    ++dns_sequence;
    id = (uint16_t)(0xb000U ^ dns_sequence);
    write_be16(&query[0], id);
    write_be16(&query[2], 0x0100U);
    write_be16(&query[4], 1U);
    if (!dns_encode_name(&query[12], sizeof(query) - 16U,
                         host, host_length, &encoded)) {
        return false;
    }
    query_length = 12U + encoded;
    if ((query_length + 4U) > sizeof(query)) {
        return false;
    }
    write_be16(&query[query_length], 1U);
    write_be16(&query[query_length + 2U], 1U);
    query_length += 4U;
    source_port = (uint16_t)(49152U + (id & 0x3fffU));
    if (!send_udp(destination_mac, config.address, config.dns,
                  source_port, NET_DNS_PORT, query, query_length)) {
        return false;
    }
    if (!net_clock_start(&clock)) {
        return false;
    }
    while (!net_clock_expired(&clock, NET_DNS_TIMEOUT_MS)) {
        struct net_udp_view udp;
        size_t length = 0U;
        if (!poll_frame(&length)) {
            return false;
        }
        if ((length == 0U) || !parse_udp(frame_buffer, length, &udp) ||
            (udp.source_ip != config.dns) ||
            (udp.destination_ip != config.address) ||
            (udp.source_port != NET_DNS_PORT) ||
            (udp.destination_port != source_port)) {
            continue;
        }
        if (parse_dns_answer(udp.payload, udp.payload_length, id, address)) {
            return true;
        }
    }
    return false;
}

static bool parse_icmp_reply(const uint8_t *frame, size_t frame_length,
                             uint32_t peer, uint16_t identifier,
                             uint16_t sequence) {
    const uint8_t *ip;
    const uint8_t *icmp;
    size_t ihl;
    uint16_t total_length;
    uint16_t fragment;
    size_t icmp_length;

    if ((frame == NULL) ||
        (frame_length < (NET_ETH_HEADER + NET_IPV4_HEADER + NET_ICMP_HEADER)) ||
        !bytes_equal(&frame[0], nic_info.mac, 6U) ||
        (read_be16(&frame[12]) != NET_ETHERTYPE_IPV4)) {
        return false;
    }
    ip = &frame[NET_ETH_HEADER];
    if ((ip[0] >> 4U) != 4U) {
        return false;
    }
    ihl = (size_t)(ip[0] & 0x0fU) * 4U;
    if ((ihl < NET_IPV4_HEADER) ||
        (ihl > (frame_length - NET_ETH_HEADER))) {
        return false;
    }
    total_length = read_be16(&ip[2]);
    fragment = read_be16(&ip[6]);
    if (((size_t)total_length < (ihl + NET_ICMP_HEADER)) ||
        ((size_t)total_length > (frame_length - NET_ETH_HEADER)) ||
        ((fragment & 0x3fffU) != 0U) ||
        (net_checksum(ip, ihl) != 0U) ||
        (ip[9] != NET_IP_PROTO_ICMP) ||
        (read_be32(&ip[12]) != peer) ||
        (read_be32(&ip[16]) != config.address)) {
        return false;
    }
    icmp = &ip[ihl];
    icmp_length = (size_t)total_length - ihl;
    if ((icmp[0] != 0U) || (icmp[1] != 0U) ||
        (net_checksum(icmp, icmp_length) != 0U) ||
        (read_be16(&icmp[4]) != identifier) ||
        (read_be16(&icmp[6]) != sequence)) {
        return false;
    }
    return true;
}

static bool ping_once(uint32_t peer, uint16_t sequence, uint32_t *rtt_ms) {
    uint8_t destination_mac[6];
    uint8_t *ip = &transmit_buffer[NET_ETH_HEADER];
    uint8_t *icmp = &ip[NET_IPV4_HEADER];
    const uint16_t identifier = 0xb052U;
    struct net_clock clock;
    size_t index;

    if ((rtt_ms == NULL) || !resolve_mac(peer, destination_mac)) {
        return false;
    }
    ethernet_header(transmit_buffer, destination_mac, NET_ETHERTYPE_IPV4);
    (void)ipv4_header(ip, (uint16_t)(NET_ICMP_HEADER + NET_ICMP_PAYLOAD),
                      NET_IP_PROTO_ICMP, config.address, peer);
    bytes_zero(icmp, NET_ICMP_HEADER + NET_ICMP_PAYLOAD);
    icmp[0] = 8U;
    write_be16(&icmp[4], identifier);
    write_be16(&icmp[6], sequence);
    for (index = 0U; index < NET_ICMP_PAYLOAD; ++index) {
        icmp[NET_ICMP_HEADER + index] = (uint8_t)(index + sequence);
    }
    write_be16(&icmp[2], net_checksum(icmp,
        NET_ICMP_HEADER + NET_ICMP_PAYLOAD));
    if (!net_clock_start(&clock)) {
        return false;
    }
    if (!e1000_send(transmit_buffer,
                    NET_ETH_HEADER + NET_IPV4_HEADER +
                    NET_ICMP_HEADER + NET_ICMP_PAYLOAD)) {
        return false;
    }
    while (!net_clock_expired(&clock, NET_PING_TIMEOUT_MS)) {
        size_t length = 0U;
        if (!poll_frame(&length)) {
            return false;
        }
        if ((length != 0U) &&
            parse_icmp_reply(frame_buffer, length, peer,
                             identifier, sequence)) {
            *rtt_ms = net_clock_elapsed_ms(&clock);
            if (*rtt_ms == UINT32_MAX) {
                return false;
            }
            return true;
        }
    }
    *rtt_ms = NET_RTT_TIMEOUT;
    return true;
}

static enum net_result initialize_network(void) {
    enum e1000_result driver_result;

    if (network_initialized) {
        return NET_RESULT_OK;
    }
    driver_result = e1000_init();
    (void)e1000_get_info(&nic_info);
    if (driver_result == E1000_RESULT_NO_DEVICE) {
        return NET_RESULT_NO_DEVICE;
    }
    if (driver_result == E1000_RESULT_UNSUPPORTED_DEVICE) {
        return NET_RESULT_UNSUPPORTED_DEVICE;
    }
    if (driver_result != E1000_RESULT_OK) {
        return NET_RESULT_DRIVER_ERROR;
    }
    if (!nic_info.link_up) {
        struct net_clock clock;
        if (net_clock_start(&clock)) {
            while (!net_clock_expired(&clock, 1000U)) {
                (void)e1000_get_info(&nic_info);
                if (nic_info.link_up) {
                    break;
                }
                x86_64_pause();
            }
        }
    }
    if (!nic_info.link_up) {
        return NET_RESULT_LINK_DOWN;
    }
    network_initialized = true;
    return NET_RESULT_OK;
}

enum net_result net_ping_host(const char *host, size_t host_length,
                              struct boring_net_ping_result *result) {
    enum net_result init_result;
    uint32_t address = 0U;
    bool used_dns = false;
    uint32_t index;

    if ((host == NULL) || (result == NULL) || (host_length == 0U) ||
        (host_length > BORING_NET_HOST_MAX)) {
        return NET_RESULT_INVALID_HOST;
    }
    bytes_zero(result, sizeof(*result));
    result->abi_version = BORING_NET_PING_ABI_VERSION;
    init_result = initialize_network();
    (void)e1000_get_info(&nic_info);
    result->nic_vendor_id = nic_info.vendor_id;
    result->nic_device_id = nic_info.device_id;
    result->nic_bus = nic_info.bus;
    result->nic_device = nic_info.device;
    result->nic_function = nic_info.function;
    bytes_copy(result->mac, nic_info.mac, 6U);
    if (init_result != NET_RESULT_OK) {
        return init_result;
    }
    if (!configure_dhcp()) {
        return NET_RESULT_DHCP_FAILED;
    }
    result->local_address = config.address;
    result->gateway = config.gateway;
    result->dns = config.dns;
    if (!net_parse_ipv4_literal(host, host_length, &address)) {
        used_dns = true;
        if (!resolve_dns(host, host_length, &address)) {
            return NET_RESULT_DNS_FAILED;
        }
    }
    result->address = address;
    result->used_dns = used_dns ? 1U : 0U;
    for (index = 0U; index < BORING_NET_PING_COUNT; ++index) {
        uint32_t rtt = NET_RTT_TIMEOUT;
        ++result->transmitted;
        if (!ping_once(address, (uint16_t)(index + 1U), &rtt)) {
            return NET_RESULT_DRIVER_ERROR;
        }
        result->rtt_ms[index] = rtt;
        if (rtt != NET_RTT_TIMEOUT) {
            ++result->received;
        }
    }
    return NET_RESULT_OK;
}
