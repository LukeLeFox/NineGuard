#include "NGProfileUAPI.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ng_secure_zero(void *buffer, size_t length) {
    volatile unsigned char *cursor = buffer;
    while (cursor && length--)
        *cursor++ = 0;
}

const char *ng_profile_error_name(int code) {
    switch (code) {
    case NG_PROFILE_OK: return "OK";
    case NG_PROFILE_ERR_ARGS: return "ARGS";
    case NG_PROFILE_ERR_KEY: return "KEY";
    case NG_PROFILE_ERR_PORT: return "LISTEN_PORT";
    case NG_PROFILE_ERR_KEEPALIVE: return "KEEPALIVE";
    case NG_PROFILE_ERR_ALLOWED_IP: return "ALLOWED_IP";
    case NG_PROFILE_ERR_ENDPOINT: return "ENDPOINT";
    case NG_PROFILE_ERR_ADDRESS: return "ADDRESS";
    case NG_PROFILE_ERR_SPACE: return "SPACE";
    case NG_PROFILE_ERR_MTU: return "MTU";
    default: return "UNKNOWN";
    }
}

static int b64_value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int ng_b64key_to_hex(const char *b64, char hex[65]) {
    static const char digits[] = "0123456789abcdef";
    unsigned char bytes[33];
    size_t produced = 0;

    if (!b64 || !hex || strlen(b64) != 44 || b64[43] != '=' || b64[42] == '=')
        return NG_PROFILE_ERR_KEY;

    /* 10 gruppi completi da 4 caratteri + gruppo finale "xxx=". */
    for (size_t i = 0; i < 44; i += 4) {
        int v[4];
        for (size_t j = 0; j < 4; j++) {
            unsigned char c = (unsigned char)b64[i + j];
            v[j] = (i == 40 && j == 3) ? 0 : b64_value(c);
            if (v[j] < 0) {
                ng_secure_zero(bytes, sizeof(bytes));
                return NG_PROFILE_ERR_KEY;
            }
        }
        unsigned long triple = ((unsigned long)v[0] << 18) |
                               ((unsigned long)v[1] << 12) |
                               ((unsigned long)v[2] << 6) |
                               (unsigned long)v[3];
        bytes[produced++] = (unsigned char)(triple >> 16);
        bytes[produced++] = (unsigned char)(triple >> 8);
        if (i != 40)
            bytes[produced++] = (unsigned char)triple;
        else if (v[2] & 0x3) {
            /* bit residui non nulli: codifica non canonica */
            ng_secure_zero(bytes, sizeof(bytes));
            return NG_PROFILE_ERR_KEY;
        }
    }
    if (produced != 32) {
        ng_secure_zero(bytes, sizeof(bytes));
        return NG_PROFILE_ERR_KEY;
    }
    for (size_t i = 0; i < 32; i++) {
        hex[i * 2] = digits[bytes[i] >> 4];
        hex[i * 2 + 1] = digits[bytes[i] & 0xf];
    }
    hex[64] = '\0';
    ng_secure_zero(bytes, sizeof(bytes));
    return NG_PROFILE_OK;
}

static int parse_u16(const char *text, unsigned *value) {
    if (!text || !*text || strlen(text) > 5)
        return -1;
    unsigned long result = 0;
    for (const char *p = text; *p; p++) {
        if (!isdigit((unsigned char)*p))
            return -1;
        result = result * 10 + (unsigned long)(*p - '0');
    }
    if (result > 65535)
        return -1;
    *value = (unsigned)result;
    return 0;
}

/* Copia un elemento di una lista separata da virgole, senza spazi. */
static const char *next_item(const char *cursor, char *item, size_t capacity,
                             int *overflow) {
    *overflow = 0;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == ',')
        cursor++;
    if (!*cursor)
        return NULL;
    const char *start = cursor;
    while (*cursor && *cursor != ',')
        cursor++;
    const char *end = cursor;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
        end--;
    size_t length = (size_t)(end - start);
    if (length == 0 || length >= capacity) {
        *overflow = 1;
        return cursor;
    }
    memcpy(item, start, length);
    item[length] = '\0';
    return cursor;
}

int ng_normalize_cidr(const char *text, char *out, size_t capacity,
                      int *family_out, unsigned *prefix_out) {
    char address[64];
    const char *slash;
    unsigned prefix = 0;
    int has_prefix = 0;

    if (!text || !out || capacity == 0)
        return NG_PROFILE_ERR_ARGS;
    slash = strchr(text, '/');
    size_t length = slash ? (size_t)(slash - text) : strlen(text);
    if (length == 0 || length >= sizeof(address))
        return NG_PROFILE_ERR_ALLOWED_IP;
    memcpy(address, text, length);
    address[length] = '\0';
    if (slash) {
        if (parse_u16(slash + 1, &prefix) != 0)
            return NG_PROFILE_ERR_ALLOWED_IP;
        has_prefix = 1;
    }

    unsigned char binary[16];
    char canonical[INET6_ADDRSTRLEN];
    int family;
    if (inet_pton(AF_INET, address, binary) == 1) {
        family = 4;
        if (!has_prefix)
            prefix = 32;
        if (prefix > 32 ||
            !inet_ntop(AF_INET, binary, canonical, sizeof(canonical)))
            return NG_PROFILE_ERR_ALLOWED_IP;
    } else if (inet_pton(AF_INET6, address, binary) == 1) {
        family = 6;
        if (!has_prefix)
            prefix = 128;
        if (prefix > 128 ||
            !inet_ntop(AF_INET6, binary, canonical, sizeof(canonical)))
            return NG_PROFILE_ERR_ALLOWED_IP;
    } else {
        return NG_PROFILE_ERR_ALLOWED_IP;
    }
    int written = snprintf(out, capacity, "%s/%u", canonical, prefix);
    if (written < 0 || (size_t)written >= capacity)
        return NG_PROFILE_ERR_SPACE;
    if (family_out) *family_out = family;
    if (prefix_out) *prefix_out = prefix;
    return NG_PROFILE_OK;
}

int ng_resolve_endpoint(const char *text, char *out, size_t capacity,
                        char ipv4_out[NG_IPV4_TEXT]) {
    char host[256], port[8];
    const char *colon;

    if (ipv4_out) ipv4_out[0] = '\0';
    if (!text || !out || capacity == 0)
        return NG_PROFILE_ERR_ARGS;
    if (text[0] == '[') {
        const char *close = strchr(text, ']');
        if (!close || close[1] != ':' || close == text + 1 ||
            (size_t)(close - text - 1) >= sizeof(host))
            return NG_PROFILE_ERR_ENDPOINT;
        memcpy(host, text + 1, (size_t)(close - text - 1));
        host[close - text - 1] = '\0';
        colon = close + 1;
    } else {
        colon = strrchr(text, ':');
        if (!colon || colon == text || memchr(text, ':', (size_t)(colon - text)) ||
            (size_t)(colon - text) >= sizeof(host))
            return NG_PROFILE_ERR_ENDPOINT;
        memcpy(host, text, (size_t)(colon - text));
        host[colon - text] = '\0';
    }
    unsigned port_value;
    if (parse_u16(colon + 1, &port_value) != 0 || port_value == 0)
        return NG_PROFILE_ERR_ENDPOINT;
    snprintf(port, sizeof(port), "%u", port_value);
    for (const char *p = host; *p; p++) {
        if (*p == '=' || *p == '\n' || *p == '\r' || *p == ' ')
            return NG_PROFILE_ERR_ENDPOINT;
    }

    struct addrinfo hints, *head = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICSERV;
    if (getaddrinfo(host, port, &hints, &head) != 0 || !head)
        return NG_PROFILE_ERR_ENDPOINT;

    const struct addrinfo *chosen = NULL;
    for (const struct addrinfo *p = head; p; p = p->ai_next) {
        if (p->ai_family == AF_INET) { chosen = p; break; }
        if (!chosen && p->ai_family == AF_INET6) chosen = p;
    }
    int result = NG_PROFILE_ERR_ENDPOINT;
    char numeric[INET6_ADDRSTRLEN];
    if (chosen && chosen->ai_family == AF_INET) {
        const struct sockaddr_in *sin = (const void *)chosen->ai_addr;
        if (inet_ntop(AF_INET, &sin->sin_addr, numeric, sizeof(numeric))) {
            int n = snprintf(out, capacity, "%s:%u", numeric, port_value);
            if (n > 0 && (size_t)n < capacity) {
                if (ipv4_out)
                    snprintf(ipv4_out, NG_IPV4_TEXT, "%s", numeric);
                result = NG_PROFILE_OK;
            }
        }
    } else if (chosen && chosen->ai_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const void *)chosen->ai_addr;
        if (inet_ntop(AF_INET6, &sin6->sin6_addr, numeric, sizeof(numeric))) {
            int n = snprintf(out, capacity, "[%s]:%u", numeric, port_value);
            if (n > 0 && (size_t)n < capacity)
                result = NG_PROFILE_OK;
        }
    }
    freeaddrinfo(head);
    return result;
}

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    int failed;
} ng_writer;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void writer_line(ng_writer *w, const char *format, ...) {
    if (w->failed)
        return;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(w->buffer + w->length, w->capacity - w->length,
                      format, args);
    va_end(args);
    if (n < 0 || (size_t)n + 2 > w->capacity - w->length) {
        w->failed = 1;
        return;
    }
    w->length += (size_t)n;
    w->buffer[w->length++] = '\n';
    w->buffer[w->length] = '\0';
}

static int same_route(const ng_profile_plan *plan, const char *ipv4) {
    for (unsigned i = 0; i < plan->host_route_count; i++)
        if (!strcmp(plan->host_routes[i], ipv4))
            return 1;
    return 0;
}

static void plan_add_net_route(ng_profile_plan *plan, const char *ipv4,
                               unsigned prefix) {
    struct in_addr address;
    char network[NG_IPV4_TEXT];
    if (inet_pton(AF_INET, ipv4, &address) != 1 || prefix < 1 || prefix > 31) {
        plan->skipped_allowed_ips++;
        return;
    }
    address.s_addr &= htonl(0xffffffffu << (32 - prefix));
    if (!inet_ntop(AF_INET, &address, network, sizeof(network))) {
        plan->skipped_allowed_ips++;
        return;
    }
    for (unsigned i = 0; i < plan->net_route_count; i++)
        if (plan->net_prefixes[i] == prefix &&
            !strcmp(plan->net_routes[i], network))
            return;
    if (plan->net_route_count >= NG_MAX_NET_ROUTES) {
        plan->skipped_allowed_ips++;
        return;
    }
    snprintf(plan->net_routes[plan->net_route_count], NG_IPV4_TEXT, "%s",
             network);
    plan->net_prefixes[plan->net_route_count++] = (unsigned char)prefix;
}

static int dns_domain_valid(const char *text) {
    size_t length = strlen(text);
    if (length == 0 || length >= NG_DNS_DOMAIN_TEXT || text[0] == '.' ||
        text[0] == '-')
        return 0;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.'))
            return 0;
    }
    return 1;
}

/* DNS: server IPv4 (max NG_MAX_DNS), domini come SearchDomains; IPv6 e
 * voci non valide vengono ignorate senza far fallire il profilo. */
static void plan_parse_dns(ng_profile_plan *plan, const char *dns) {
    char item[96];
    int overflow;
    const char *cursor = dns;
    while (cursor && (cursor = next_item(cursor, item, sizeof(item),
                                         &overflow))) {
        struct in_addr address;
        if (overflow)
            continue;
        if (inet_pton(AF_INET, item, &address) == 1) {
            if (plan->dns_count < NG_MAX_DNS)
                inet_ntop(AF_INET, &address,
                          plan->dns_servers[plan->dns_count++], NG_IPV4_TEXT);
        } else if (!strchr(item, ':') && dns_domain_valid(item) &&
                   plan->search_count < NG_MAX_DNS) {
            snprintf(plan->search_domains[plan->search_count++],
                     NG_DNS_DOMAIN_TEXT, "%s", item);
        }
    }
}

static int is_endpoint(const ng_profile_plan *plan, const char *ipv4) {
    for (unsigned i = 0; i < plan->endpoint_count; i++)
        if (!strcmp(plan->endpoints_ipv4[i], ipv4))
            return 1;
    return 0;
}

int ng_profile_build_uapi(const ng_profile_input *input,
                          char *uapi, size_t capacity,
                          ng_profile_plan *plan) {
    char hex[65], item[96], normalized[96], endpoint[96];
    ng_writer w = {uapi, capacity, 0, 0};
    int result = NG_PROFILE_OK;
    unsigned value;

    if (!input || !uapi || capacity < 64 || !plan || !input->peers ||
        input->peer_count == 0 || input->peer_count > NG_MAX_PEERS)
        return NG_PROFILE_ERR_ARGS;
    memset(plan, 0, sizeof(*plan));
    uapi[0] = '\0';

    /* Indirizzo: tutti gli elementi devono essere validi; si usa il primo
     * IPv4 per l'interfaccia point-to-point /32. */
    if (input->address) {
        const char *cursor = input->address;
        int overflow;
        while ((cursor = next_item(cursor, item, sizeof(item), &overflow))) {
            int family;
            if (overflow ||
                ng_normalize_cidr(item, normalized, sizeof(normalized),
                                  &family, NULL) != NG_PROFILE_OK)
                return NG_PROFILE_ERR_ADDRESS;
            if (family == 4 && !plan->ipv4_address[0]) {
                char *slash = strchr(normalized, '/');
                if (slash) *slash = '\0';
                snprintf(plan->ipv4_address, sizeof(plan->ipv4_address),
                         "%s", normalized);
            }
        }
    }

    /* Endpoint risolti prima di scrivere qualunque riga. */
    for (unsigned i = 0; i < input->peer_count; i++) {
        const char *ep = input->peers[i].endpoint;
        if (ep && *ep) {
            char ipv4[NG_IPV4_TEXT];
            if (ng_resolve_endpoint(ep, endpoint, sizeof(endpoint), ipv4) !=
                NG_PROFILE_OK)
                return NG_PROFILE_ERR_ENDPOINT;
            if (ipv4[0] && plan->endpoint_count < NG_MAX_PEERS)
                snprintf(plan->endpoints_ipv4[plan->endpoint_count++],
                         NG_IPV4_TEXT, "%s", ipv4);
        }
    }

    writer_line(&w, "set=1");
    if (ng_b64key_to_hex(input->private_key, hex) != NG_PROFILE_OK) {
        result = NG_PROFILE_ERR_KEY;
        goto fail;
    }
    writer_line(&w, "private_key=%s", hex);
    if (input->listen_port && *input->listen_port) {
        if (parse_u16(input->listen_port, &value) != 0) {
            result = NG_PROFILE_ERR_PORT;
            goto fail;
        }
        writer_line(&w, "listen_port=%u", value);
    }
    if (input->mtu && *input->mtu) {
        unsigned mtu = 0;
        if (parse_u16(input->mtu, &mtu) != 0 || mtu < NG_MTU_MIN ||
            mtu > NG_MTU_MAX) {
            result = NG_PROFILE_ERR_MTU;
            goto fail;
        }
        plan->mtu = mtu; /* applicata da NET_UP sulla utun, non via UAPI */
    }
    writer_line(&w, "replace_peers=true");

    for (unsigned i = 0; i < input->peer_count; i++) {
        const ng_peer_input *peer = &input->peers[i];
        if (ng_b64key_to_hex(peer->public_key, hex) != NG_PROFILE_OK) {
            result = NG_PROFILE_ERR_KEY;
            goto fail;
        }
        writer_line(&w, "public_key=%s", hex);
        if (peer->preshared_key && *peer->preshared_key) {
            if (ng_b64key_to_hex(peer->preshared_key, hex) != NG_PROFILE_OK) {
                result = NG_PROFILE_ERR_KEY;
                goto fail;
            }
            writer_line(&w, "preshared_key=%s", hex);
        }
        if (peer->endpoint && *peer->endpoint) {
            if (ng_resolve_endpoint(peer->endpoint, endpoint,
                                    sizeof(endpoint), NULL) != NG_PROFILE_OK) {
                result = NG_PROFILE_ERR_ENDPOINT;
                goto fail;
            }
            writer_line(&w, "endpoint=%s", endpoint);
        }
        if (peer->keepalive && *peer->keepalive &&
            strcmp(peer->keepalive, "off") != 0) {
            if (parse_u16(peer->keepalive, &value) != 0) {
                result = NG_PROFILE_ERR_KEEPALIVE;
                goto fail;
            }
            writer_line(&w, "persistent_keepalive_interval=%u", value);
        }
        writer_line(&w, "replace_allowed_ips=true");

        if (!peer->allowed_ips || !*peer->allowed_ips) {
            result = NG_PROFILE_ERR_ALLOWED_IP;
            goto fail;
        }
        const char *cursor = peer->allowed_ips;
        int overflow, count = 0;
        while ((cursor = next_item(cursor, item, sizeof(item), &overflow))) {
            int family;
            unsigned prefix;
            if (overflow ||
                ng_normalize_cidr(item, normalized, sizeof(normalized),
                                  &family, &prefix) != NG_PROFILE_OK) {
                result = NG_PROFILE_ERR_ALLOWED_IP;
                goto fail;
            }
            writer_line(&w, "allowed_ip=%s", normalized);
            count++;
            if (family == 4 && prefix == 0) {
                plan->full_ipv4 = 1;
            } else if (family == 4 && prefix < 32) {
                char *slash = strchr(normalized, '/');
                if (slash) *slash = '\0';
                plan_add_net_route(plan, normalized, prefix);
            } else if (family == 4 && prefix == 32) {
                char *slash = strchr(normalized, '/');
                if (slash) *slash = '\0';
                if (is_endpoint(plan, normalized) ||
                    plan->host_route_count >= NG_MAX_HOST_ROUTES)
                    plan->skipped_allowed_ips++;
                else if (!same_route(plan, normalized))
                    snprintf(plan->host_routes[plan->host_route_count++],
                             NG_IPV4_TEXT, "%s", normalized);
            } else {
                plan->skipped_allowed_ips++;
            }
        }
        if (count == 0) {
            result = NG_PROFILE_ERR_ALLOWED_IP;
            goto fail;
        }
    }
    /* Riga vuota finale: termina IpcSetOperation. */
    if (!w.failed && w.length + 1 < w.capacity) {
        w.buffer[w.length++] = '\n';
        w.buffer[w.length] = '\0';
    } else {
        w.failed = 1;
    }
    if (w.failed) {
        result = NG_PROFILE_ERR_SPACE;
        goto fail;
    }
    plan->peer_count = input->peer_count;
    plan_parse_dns(plan, input->dns);
    ng_secure_zero(hex, sizeof(hex));
    return NG_PROFILE_OK;

fail:
    ng_secure_zero(hex, sizeof(hex));
    ng_secure_zero(uapi, capacity);
    memset(plan, 0, sizeof(*plan));
    return result;
}
