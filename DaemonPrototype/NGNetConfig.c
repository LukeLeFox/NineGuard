#include "NGNetConfig.h"
#include "NGDaemonPaths.h"
#include "NGNetWatch.h"

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * <net/route.h> non e' presente nell'SDK pubblico iPhoneOS 9.x.
 * Dichiarazioni locali dall'ABI XNU xnu-3248 (bsd/net/route.h), coerenti
 * con le misure gia' verificate sul dispositivo (rt_msghdr2=92,
 * offsetof(rtm_addrs)=12, allineamento sockaddr 4 byte).
 */
struct ng_rt_metrics {
    uint32_t rmx_locks;
    uint32_t rmx_mtu;
    uint32_t rmx_hopcount;
    int32_t rmx_expire;
    uint32_t rmx_recvpipe;
    uint32_t rmx_sendpipe;
    uint32_t rmx_ssthresh;
    uint32_t rmx_rtt;
    uint32_t rmx_rttvar;
    uint32_t rmx_pksent;
    uint32_t rmx_state;
    uint32_t rmx_filler[3];
};

struct ng_rt_msghdr {
    unsigned short rtm_msglen;
    unsigned char rtm_version;
    unsigned char rtm_type;
    unsigned short rtm_index;
    int rtm_flags;
    int rtm_addrs;
    pid_t rtm_pid;
    int rtm_seq;
    int rtm_errno;
    int rtm_use;
    uint32_t rtm_inits;
    struct ng_rt_metrics rtm_rmx;
};

_Static_assert(sizeof(struct ng_rt_metrics) == 56, "rt_metrics ABI");
_Static_assert(sizeof(struct ng_rt_msghdr) == 92, "rt_msghdr ABI");
_Static_assert(offsetof(struct ng_rt_msghdr, rtm_addrs) == 12, "rtm_addrs");
_Static_assert(offsetof(struct ng_rt_msghdr, rtm_rmx) == 36, "rtm_rmx");

#define NG_RTM_VERSION 5
#define NG_RTM_ADD 0x1
#define NG_RTM_DELETE 0x2
#define NG_RTM_GET 0x4
#define NG_RTF_UP 0x1
#define NG_RTF_GATEWAY 0x2
#define NG_RTF_HOST 0x4
#define NG_RTF_STATIC 0x800
#define NG_RTA_DST 0x1
#define NG_RTA_GATEWAY 0x2
#define NG_RTA_NETMASK 0x4
#define NG_RTA_IFP 0x10
#define NG_RTAX_MAX 8

#define NG_ROUNDUP32(a) \
    ((a) > 0 ? (1 + (((a) - 1) | (sizeof(uint32_t) - 1))) : sizeof(uint32_t))

#ifndef NG_NET_STATE_PATH
#define NG_NET_STATE_PATH NG_CONTROL_DIR "/net.state"
#endif

#define NG_MAX_APPLIED (NG_MAX_HOST_ROUTES + NG_MAX_NET_ROUTES + 2 + NG_MAX_PEERS)

typedef struct {
    struct in_addr destination;
    unsigned prefix;            /* 32 = host route */
    int via_gateway;            /* bypass endpoint (gateway IPv4) */
    struct in_addr gateway;     /* solo bypass */
} ng_applied_route;

/* Endpoint coperti dalle route del tunnel: il watchdog ne segue il bypass. */
typedef struct {
    struct in_addr address;
    int foreign;                /* host route preesistente non nostra */
} ng_watched_endpoint;

static char g_ifname[IFNAMSIZ];
static struct in_addr g_address;
static int g_address_applied;
static int g_full;
static unsigned g_applied_count;
static ng_applied_route g_applied[NG_MAX_APPLIED];
static int g_seq;
static int g_mtu_restore;       /* MTU originale da ripristinare, 0 = no */
static ng_watched_endpoint g_endpoints[NG_MAX_PEERS];
static unsigned g_endpoint_count;

int ng_net_is_up(void) {
    return g_address_applied || g_applied_count > 0;
}

int ng_net_is_full(void) {
    return g_full;
}

unsigned ng_net_route_count(void) {
    unsigned count = 0;
    for (unsigned i = 0; i < g_applied_count; i++)
        if (!g_applied[i].via_gateway)
            count++;
    return count;
}

static void fill_sin(struct sockaddr_in *sin, struct in_addr address) {
    memset(sin, 0, sizeof(*sin));
    sin->sin_len = sizeof(*sin);
    sin->sin_family = AF_INET;
    sin->sin_addr = address;
}

static struct in_addr prefix_mask(unsigned prefix) {
    struct in_addr mask;
    mask.s_addr = prefix == 0 ? 0 : htonl(0xffffffffu << (32 - prefix));
    return mask;
}

static int link_address(const char *ifname, unsigned char *out,
                        size_t capacity, size_t *length) {
    struct ifaddrs *head = NULL;
    int found = -1;
    if (getifaddrs(&head) != 0)
        return -1;
    for (struct ifaddrs *p = head; p; p = p->ifa_next) {
        if (!p->ifa_name || !p->ifa_addr || strcmp(p->ifa_name, ifname) != 0 ||
            p->ifa_addr->sa_family != AF_LINK)
            continue;
        size_t sa_len = p->ifa_addr->sa_len;
        if (sa_len < 8 || sa_len > capacity)
            break;
        memset(out, 0, capacity);
        memcpy(out, p->ifa_addr, sa_len);
        *length = sa_len;
        found = 0;
        break;
    }
    freeifaddrs(head);
    return found;
}

/* RTM_ADD/RTM_DELETE. gateway: sockaddr_dl (route verso interfaccia) o
 * sockaddr_in (RTF_GATEWAY); NULL per DELETE. */
static int route_message(int type, struct in_addr destination,
                         unsigned prefix, const void *gateway,
                         size_t gateway_length) {
    unsigned char buffer[512];
    struct ng_rt_msghdr *header = (struct ng_rt_msghdr *)(void *)buffer;
    size_t offset = sizeof(*header);
    struct sockaddr_in dst, mask;

    memset(buffer, 0, sizeof(buffer));
    header->rtm_version = NG_RTM_VERSION;
    header->rtm_type = (unsigned char)type;
    header->rtm_flags = NG_RTF_UP | NG_RTF_STATIC;
    if (prefix == 32)
        header->rtm_flags |= NG_RTF_HOST;
    header->rtm_addrs = NG_RTA_DST;
    header->rtm_pid = getpid();
    header->rtm_seq = ++g_seq;

    fill_sin(&dst, destination);
    memcpy(buffer + offset, &dst, sizeof(dst));
    offset += NG_ROUNDUP32(sizeof(dst));

    if (gateway && gateway_length) {
        const struct sockaddr *sa = gateway;
        if (offset + NG_ROUNDUP32(gateway_length) > sizeof(buffer))
            return EINVAL;
        memcpy(buffer + offset, gateway, gateway_length);
        offset += NG_ROUNDUP32(gateway_length);
        header->rtm_addrs |= NG_RTA_GATEWAY;
        if (sa->sa_family == AF_INET)
            header->rtm_flags |= NG_RTF_GATEWAY;
    }
    if (prefix < 32) {
        fill_sin(&mask, prefix_mask(prefix));
        memcpy(buffer + offset, &mask, sizeof(mask));
        offset += NG_ROUNDUP32(sizeof(mask));
        header->rtm_addrs |= NG_RTA_NETMASK;
    }
    header->rtm_msglen = (unsigned short)offset;

    int fd = socket(PF_ROUTE, SOCK_RAW, AF_INET);
    if (fd < 0)
        return errno ? errno : EIO;
    int error = 0;
    ssize_t written = write(fd, buffer, offset);
    if (written < 0)
        error = errno ? errno : EIO;
    else if ((size_t)written != offset)
        error = EIO;
    close(fd);
    return error;
}

int ng_route_lookup(struct in_addr destination, char ifname[IFNAMSIZ],
                    int *has_gateway, struct in_addr *gateway) {
    int static_host;
    return ng_route_lookup_flags(destination, ifname, has_gateway, gateway,
                                 &static_host);
}

int ng_route_lookup_flags(struct in_addr destination, char ifname[IFNAMSIZ],
                          int *has_gateway, struct in_addr *gateway,
                          int *static_host) {
    unsigned char buffer[1024];
    struct ng_rt_msghdr *header = (struct ng_rt_msghdr *)(void *)buffer;
    size_t offset = sizeof(*header);
    struct sockaddr_in dst;
    struct sockaddr_dl ifp;

    if (!ifname || !has_gateway || !gateway || !static_host)
        return EINVAL;
    *static_host = 0;
    ifname[0] = '\0';
    *has_gateway = 0;
    memset(gateway, 0, sizeof(*gateway));

    memset(buffer, 0, sizeof(buffer));
    header->rtm_version = NG_RTM_VERSION;
    header->rtm_type = NG_RTM_GET;
    header->rtm_flags = NG_RTF_UP | NG_RTF_HOST;
    header->rtm_addrs = NG_RTA_DST | NG_RTA_IFP;
    header->rtm_pid = getpid();
    int seq = header->rtm_seq = ++g_seq;
    fill_sin(&dst, destination);
    memcpy(buffer + offset, &dst, sizeof(dst));
    offset += NG_ROUNDUP32(sizeof(dst));
    memset(&ifp, 0, sizeof(ifp));
    ifp.sdl_len = sizeof(ifp);
    ifp.sdl_family = AF_LINK;
    memcpy(buffer + offset, &ifp, sizeof(ifp));
    offset += NG_ROUNDUP32(sizeof(ifp));
    header->rtm_msglen = (unsigned short)offset;

    int fd = socket(PF_ROUTE, SOCK_RAW, AF_INET);
    if (fd < 0)
        return errno ? errno : EIO;
    struct timeval timeout = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (write(fd, buffer, offset) != (ssize_t)offset) {
        int error = errno ? errno : EIO;
        close(fd);
        return error;
    }
    for (int attempt = 0; attempt < 32; attempt++) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0) {
            int error = errno ? errno : EIO;
            close(fd);
            return error;
        }
        if ((size_t)n < sizeof(*header) || header->rtm_type != NG_RTM_GET ||
            header->rtm_seq != seq || header->rtm_pid != getpid())
            continue;
        close(fd);
        if (header->rtm_errno)
            return header->rtm_errno;
        *static_host = (header->rtm_flags & (NG_RTF_HOST | NG_RTF_STATIC)) ==
                       (NG_RTF_HOST | NG_RTF_STATIC);
        size_t end = header->rtm_msglen < (size_t)n ? header->rtm_msglen
                                                    : (size_t)n;
        size_t at = sizeof(*header);
        for (int bit = 0; bit < NG_RTAX_MAX; bit++) {
            if (!(header->rtm_addrs & (1 << bit)))
                continue;
            if (at + 2 > end)
                break;
            const struct sockaddr *sa =
                (const struct sockaddr *)(const void *)(buffer + at);
            size_t sa_len = sa->sa_len;
            if (at + sa_len > end)
                break;
            if ((1 << bit) == NG_RTA_GATEWAY && sa->sa_family == AF_INET &&
                sa_len >= 8) {
                struct sockaddr_in sin;
                memset(&sin, 0, sizeof(sin));
                memcpy(&sin, sa, sa_len < sizeof(sin) ? sa_len : sizeof(sin));
                *gateway = sin.sin_addr;
                *has_gateway = (header->rtm_flags & NG_RTF_GATEWAY) != 0;
            } else if ((1 << bit) == NG_RTA_IFP && sa->sa_family == AF_LINK &&
                       sa_len >= 8) {
                struct sockaddr_dl sdl;
                memset(&sdl, 0, sizeof(sdl));
                memcpy(&sdl, sa, sa_len < sizeof(sdl) ? sa_len : sizeof(sdl));
                if (sdl.sdl_nlen > 0 && sdl.sdl_nlen < IFNAMSIZ &&
                    sdl.sdl_nlen <= sizeof(sdl.sdl_data)) {
                    memcpy(ifname, sdl.sdl_data, sdl.sdl_nlen);
                    ifname[sdl.sdl_nlen] = '\0';
                }
            }
            at += NG_ROUNDUP32(sa_len);
        }
        if (!ifname[0] && header->rtm_index > 0) {
            char name[IFNAMSIZ];
            if (if_indextoname(header->rtm_index, name))
                snprintf(ifname, IFNAMSIZ, "%s", name);
        }
        return ifname[0] ? 0 : ENXIO;
    }
    close(fd);
    return ETIMEDOUT;
}

static int address_add(const char *ifname, struct in_addr address) {
    struct ifaliasreq request;
    struct sockaddr_in sin, mask;

    memset(&request, 0, sizeof(request));
    snprintf(request.ifra_name, sizeof(request.ifra_name), "%s", ifname);
    fill_sin(&sin, address);
    fill_sin(&mask, prefix_mask(32));
    memcpy(&request.ifra_addr, &sin, sizeof(sin));
    memcpy(&request.ifra_broadaddr, &sin, sizeof(sin)); /* dst p2p */
    memcpy(&request.ifra_mask, &mask, sizeof(mask));

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return errno ? errno : EIO;
    int error = ioctl(fd, SIOCAIFADDR, &request) == 0 ? 0 : errno;
    close(fd);
    return error;
}

static int address_delete(const char *ifname, struct in_addr address) {
    struct ifreq request;
    struct sockaddr_in sin;

    memset(&request, 0, sizeof(request));
    snprintf(request.ifr_name, sizeof(request.ifr_name), "%s", ifname);
    fill_sin(&sin, address);
    memcpy(&request.ifr_addr, &sin, sizeof(sin));

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return errno ? errno : EIO;
    int error = ioctl(fd, SIOCDIFADDR, &request) == 0 ? 0 : errno;
    close(fd);
    return error;
}

static int interface_mtu(const char *ifname, int set, int *mtu) {
    struct ifreq request;
    memset(&request, 0, sizeof(request));
    snprintf(request.ifr_name, sizeof(request.ifr_name), "%s", ifname);
    request.ifr_mtu = set ? *mtu : 0;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return errno ? errno : EIO;
    int error = ioctl(fd, set ? SIOCSIFMTU : SIOCGIFMTU, &request) == 0 ?
                0 : errno;
    close(fd);
    if (!error && !set)
        *mtu = request.ifr_mtu;
    return error;
}

/* Subnet IPv4 direttamente connesse (interfacce up, no loopback/utun). */
typedef struct {
    uint32_t network;
    uint32_t mask;
} ng_subnet;

static unsigned connected_subnets(ng_subnet *out, unsigned capacity) {
    struct ifaddrs *head = NULL;
    unsigned count = 0;
    if (getifaddrs(&head) != 0)
        return 0;
    for (struct ifaddrs *p = head; p && count < capacity; p = p->ifa_next) {
        if (!p->ifa_name || !p->ifa_addr || !p->ifa_netmask ||
            p->ifa_addr->sa_family != AF_INET || !(p->ifa_flags & IFF_UP) ||
            (p->ifa_flags & IFF_LOOPBACK) || !strncmp(p->ifa_name, "utun", 4))
            continue;
        struct sockaddr_in address, mask;
        memcpy(&address, p->ifa_addr, sizeof(address));
        memcpy(&mask, p->ifa_netmask, sizeof(mask));
        uint32_t m = ntohl(mask.sin_addr.s_addr);
        if (m == 0)
            continue;
        out[count].mask = m;
        out[count].network = ntohl(address.sin_addr.s_addr) & m;
        count++;
    }
    freeifaddrs(head);
    return count;
}

/* La route (network/prefix) e' uguale o piu' specifica di una LAN? */
static int inside_lan(const ng_subnet *lans, unsigned lan_count,
                      uint32_t network, unsigned prefix) {
    uint32_t mask = ntohl(prefix_mask(prefix).s_addr);
    for (unsigned i = 0; i < lan_count; i++)
        if ((mask & lans[i].mask) == lans[i].mask &&
            (network & lans[i].mask) == lans[i].network)
            return 1;
    return 0;
}

static int contains(uint32_t network, unsigned prefix, uint32_t host) {
    uint32_t mask = ntohl(prefix_mask(prefix).s_addr);
    return (host & mask) == (network & mask);
}

static void write_state(void) {
    char text[NG_MAX_PEERS * 32 + 1];
    size_t length = 0;
    text[0] = '\0';
    for (unsigned i = 0; i < g_applied_count; i++) {
        char ip[NG_IPV4_TEXT];
        if (!g_applied[i].via_gateway ||
            !inet_ntop(AF_INET, &g_applied[i].destination, ip, sizeof(ip)))
            continue;
        int n = snprintf(text + length, sizeof(text) - length, "BYPASS %s\n",
                         ip);
        if (n < 0 || (size_t)n >= sizeof(text) - length)
            break;
        length += (size_t)n;
    }
    unlink(NG_NET_STATE_PATH);
    if (length == 0)
        return;
    int fd = open(NG_NET_STATE_PATH, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return;
    if (write(fd, text, length) != (ssize_t)length)
        unlink(NG_NET_STATE_PATH);
    close(fd);
}

void ng_net_recover(void) {
    char text[NG_MAX_PEERS * 32 + 1];
    int fd = open(NG_NET_STATE_PATH, O_RDONLY);
    if (fd < 0)
        return;
    ssize_t n = read(fd, text, sizeof(text) - 1);
    close(fd);
    unlink(NG_NET_STATE_PATH);
    if (n <= 0)
        return;
    text[n] = '\0';
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        struct in_addr address;
        if (strncmp(line, "BYPASS ", 7) == 0 &&
            inet_pton(AF_INET, line + 7, &address) == 1)
            route_message(NG_RTM_DELETE, address, 32, NULL, 0);
    }
}

int ng_net_down(void) {
    int failed = 0;
    while (g_applied_count > 0) {
        ng_applied_route *route = &g_applied[g_applied_count - 1];
        int error = route_message(NG_RTM_DELETE, route->destination,
                                  route->prefix, NULL, 0);
        if (error && error != ESRCH && error != ENXIO)
            failed = 1;
        g_applied_count--;
    }
    if (g_address_applied) {
        int error = address_delete(g_ifname, g_address);
        if (error && error != EADDRNOTAVAIL && error != ENXIO &&
            error != ESRCH)
            failed = 1;
        g_address_applied = 0;
    }
    if (g_mtu_restore) {
        int mtu = g_mtu_restore;
        int error = interface_mtu(g_ifname, 1, &mtu);
        if (error && error != ENXIO)
            failed = 1;
        g_mtu_restore = 0;
    }
    g_full = 0;
    g_endpoint_count = 0;
    memset(g_endpoints, 0, sizeof(g_endpoints));
    memset(g_applied, 0, sizeof(g_applied));
    memset(&g_address, 0, sizeof(g_address));
    unlink(NG_NET_STATE_PATH);
    return failed ? NG_NET_ERR_ROUTE : NG_NET_OK;
}

static int add_route(struct in_addr destination, unsigned prefix,
                     const void *gateway, size_t gateway_length,
                     int via_gateway) {
    struct in_addr bypass_gateway;
    memset(&bypass_gateway, 0, sizeof(bypass_gateway));
    if (via_gateway && gateway_length >= sizeof(struct sockaddr_in))
        bypass_gateway = ((const struct sockaddr_in *)gateway)->sin_addr;
    if (g_applied_count >= NG_MAX_APPLIED)
        return ENOSPC;
    int error = route_message(NG_RTM_ADD, destination, prefix, gateway,
                              gateway_length);
    if (error)
        return error;
    g_applied[g_applied_count].destination = destination;
    g_applied[g_applied_count].prefix = prefix;
    g_applied[g_applied_count].via_gateway = via_gateway;
    g_applied[g_applied_count].gateway = bypass_gateway;
    g_applied_count++;
    return 0;
}

static int unsafe_destination(uint32_t host, uint32_t own) {
    return host == 0 || host == 0xffffffffu || (host >> 24) == 127 ||
           (host >> 28) == 0xe || host == own;
}

int ng_net_up(const char *ifname, const ng_profile_plan *plan,
              ng_net_result *result) {
    unsigned char link[128];
    size_t link_length = 0;
    struct in_addr address;
    ng_subnet lans[16];
    ng_net_result local;

    if (!result) result = &local;
    memset(result, 0, sizeof(*result));
    if (!ifname || !plan || strlen(ifname) >= sizeof(g_ifname) ||
        plan->host_route_count > NG_MAX_HOST_ROUTES ||
        plan->net_route_count > NG_MAX_NET_ROUTES ||
        plan->endpoint_count > NG_MAX_PEERS)
        return NG_NET_ERR_ARGS;
    if (ng_net_is_up())
        return NG_NET_ERR_BUSY;
    if (!plan->ipv4_address[0] ||
        inet_pton(AF_INET, plan->ipv4_address, &address) != 1)
        return NG_NET_ERR_ADDRESS;
    if (if_nametoindex(ifname) == 0)
        return NG_NET_ERR_NO_INTERFACE;
    uint32_t own = ntohl(address.s_addr);

    /* Validazione completa prima di toccare il kernel. */
    for (unsigned i = 0; i < plan->host_route_count; i++) {
        struct in_addr route;
        if (inet_pton(AF_INET, plan->host_routes[i], &route) != 1 ||
            unsafe_destination(ntohl(route.s_addr), own))
            return NG_NET_ERR_ARGS;
    }
    for (unsigned i = 0; i < plan->net_route_count; i++) {
        struct in_addr route;
        unsigned prefix = plan->net_prefixes[i];
        if (inet_pton(AF_INET, plan->net_routes[i], &route) != 1 ||
            prefix < 1 || prefix > 31)
            return NG_NET_ERR_ARGS;
        uint32_t net = ntohl(route.s_addr);
        if ((net >> 24) == 127 || (net >> 28) >= 0xe)
            return NG_NET_ERR_ARGS;
    }
    unsigned lan_count = connected_subnets(lans, 16);

    /* Bypass: gateway attuale di ogni endpoint coperto, prima delle route. */
    struct in_addr bypass_dst[NG_MAX_PEERS], bypass_gw[NG_MAX_PEERS];
    struct in_addr covered_dst[NG_MAX_PEERS];
    unsigned bypass_count = 0, covered_count = 0;
    for (unsigned i = 0; i < plan->endpoint_count; i++) {
        struct in_addr endpoint;
        if (inet_pton(AF_INET, plan->endpoints_ipv4[i], &endpoint) != 1)
            return NG_NET_ERR_ARGS;
        uint32_t host = ntohl(endpoint.s_addr);
        int covered = plan->full_ipv4;
        for (unsigned j = 0; !covered && j < plan->net_route_count; j++) {
            struct in_addr route;
            inet_pton(AF_INET, plan->net_routes[j], &route);
            covered = contains(ntohl(route.s_addr), plan->net_prefixes[j],
                               host);
        }
        if (!covered)
            continue;
        int seen = 0;
        for (unsigned j = 0; j < covered_count; j++)
            seen |= covered_dst[j].s_addr == endpoint.s_addr;
        if (!seen)
            covered_dst[covered_count++] = endpoint;
        if (inside_lan(lans, lan_count, host, 32))
            continue;
        char via[IFNAMSIZ];
        int has_gateway = 0;
        struct in_addr gateway;
        if (ng_route_lookup(endpoint, via, &has_gateway, &gateway) != 0)
            return NG_NET_ERR_NO_GATEWAY;
        if (!strncmp(via, "utun", 4))
            return NG_NET_ERR_BYPASS;
        if (!has_gateway)
            continue; /* on-link */
        int duplicate = 0;
        for (unsigned j = 0; j < bypass_count; j++)
            duplicate |= bypass_dst[j].s_addr == endpoint.s_addr;
        if (!duplicate) {
            bypass_dst[bypass_count] = endpoint;
            bypass_gw[bypass_count++] = gateway;
        }
    }

    snprintf(g_ifname, sizeof(g_ifname), "%s", ifname);
    if (plan->mtu) {
        int previous = 0, mtu = (int)plan->mtu;
        if (plan->mtu < NG_MTU_MIN || plan->mtu > NG_MTU_MAX ||
            interface_mtu(ifname, 0, &previous) != 0)
            return NG_NET_ERR_ARGS;
        if (previous != mtu) {
            if (interface_mtu(ifname, 1, &mtu) != 0)
                return NG_NET_ERR_MTU;
            g_mtu_restore = previous;
        }
    }
    if (address_add(ifname, address) != 0) {
        if (g_mtu_restore) {
            int mtu = g_mtu_restore;
            interface_mtu(ifname, 1, &mtu);
            g_mtu_restore = 0;
        }
        memset(g_ifname, 0, sizeof(g_ifname));
        return NG_NET_ERR_ADDRESS;
    }
    g_address = address;
    g_address_applied = 1;
    for (unsigned i = 0; i < covered_count; i++)
        g_endpoints[i].address = covered_dst[i];
    g_endpoint_count = covered_count;

    for (unsigned i = 0; i < bypass_count; i++) {
        struct sockaddr_in gw;
        fill_sin(&gw, bypass_gw[i]);
        int error = add_route(bypass_dst[i], 32, &gw, sizeof(gw), 1);
        if (error == EEXIST) {
            /* gia' presente e non nostra: non la rimuoveremo */
            for (unsigned j = 0; j < g_endpoint_count; j++)
                if (g_endpoints[j].address.s_addr == bypass_dst[i].s_addr)
                    g_endpoints[j].foreign = 1;
            continue;
        }
        if (error) {
            ng_net_down();
            return NG_NET_ERR_BYPASS;
        }
        result->bypass++;
    }
    write_state();

    if (link_address(ifname, link, sizeof(link), &link_length) != 0) {
        ng_net_down();
        return NG_NET_ERR_NO_LINK;
    }
    for (unsigned i = 0; i < plan->host_route_count; i++) {
        struct in_addr route;
        inet_pton(AF_INET, plan->host_routes[i], &route);
        if (inside_lan(lans, lan_count, ntohl(route.s_addr), 32)) {
            result->lan_skipped++;
            continue;
        }
        if (add_route(route, 32, link, link_length, 0) != 0) {
            ng_net_down();
            return NG_NET_ERR_ROUTE;
        }
        result->routes++;
    }
    for (unsigned i = 0; i < plan->net_route_count; i++) {
        struct in_addr route;
        unsigned prefix = plan->net_prefixes[i];
        inet_pton(AF_INET, plan->net_routes[i], &route);
        if (inside_lan(lans, lan_count, ntohl(route.s_addr), prefix)) {
            result->lan_skipped++;
            continue;
        }
        if (add_route(route, prefix, link, link_length, 0) != 0) {
            ng_net_down();
            return NG_NET_ERR_ROUTE;
        }
        result->routes++;
    }
    if (plan->full_ipv4) {
        struct in_addr low, high;
        low.s_addr = htonl(0x00000000u);
        high.s_addr = htonl(0x80000000u);
        if (add_route(low, 1, link, link_length, 0) != 0 ||
            add_route(high, 1, link, link_length, 0) != 0) {
            ng_net_down();
            return NG_NET_ERR_ROUTE;
        }
        result->routes += 2;
        g_full = 1;
        result->full = 1;
    }
    int mtu = 0;
    if (interface_mtu(ifname, 0, &mtu) == 0 && mtu > 0)
        result->mtu = (unsigned)mtu;
    return NG_NET_OK;
}

unsigned ng_net_watch_count(void) {
    return ng_net_is_up() ? g_endpoint_count : 0;
}

static int find_bypass(struct in_addr endpoint) {
    for (unsigned i = 0; i < g_applied_count; i++)
        if (g_applied[i].via_gateway &&
            g_applied[i].destination.s_addr == endpoint.s_addr)
            return (int)i;
    return -1;
}

static void forget_applied(unsigned index) {
    for (unsigned i = index; i + 1 < g_applied_count; i++)
        g_applied[i] = g_applied[i + 1];
    g_applied_count--;
    memset(&g_applied[g_applied_count], 0, sizeof(g_applied[0]));
}

int ng_net_watch(int have_router, struct in_addr router,
                 ng_net_watch_result *result) {
    ng_net_watch_result local;
    ng_subnet lans[16];

    if (!result) result = &local;
    memset(result, 0, sizeof(*result));
    if (!ng_net_watch_count())
        return NG_NET_OK;
    unsigned lan_count = connected_subnets(lans, 16);
    /* Un router fuori dalle subnet connesse e' configd in transizione. */
    if (have_router &&
        !inside_lan(lans, lan_count, ntohl(router.s_addr), 32))
        have_router = 0;

    int failed = 0, changed = 0;
    for (unsigned i = 0; i < g_endpoint_count; i++) {
        if (g_endpoints[i].foreign)
            continue;
        struct in_addr endpoint = g_endpoints[i].address;
        int index = find_bypass(endpoint);
        ng_watch_input input;
        memset(&input, 0, sizeof(input));
        input.onlink = inside_lan(lans, lan_count, ntohl(endpoint.s_addr), 32);
        input.have_router = have_router;
        input.router = ntohl(router.s_addr);
        input.recorded = index >= 0;
        if (index >= 0) {
            char via[IFNAMSIZ];
            int has_gateway = 0, static_host = 0;
            struct in_addr gateway;
            input.recorded_gateway = ntohl(g_applied[index].gateway.s_addr);
            input.kernel_matches =
                ng_route_lookup_flags(endpoint, via, &has_gateway, &gateway,
                                      &static_host) == 0 &&
                static_host && has_gateway && strncmp(via, "utun", 4) &&
                gateway.s_addr == g_applied[index].gateway.s_addr;
        }
        int action = ng_watch_decide(&input);
        if (action == NG_WATCH_NO_NETWORK) {
            result->waiting++;
            continue;
        }
        if (action == NG_WATCH_KEEP)
            continue;
        if (index >= 0) {
            route_message(NG_RTM_DELETE, endpoint, 32, NULL, 0);
            forget_applied((unsigned)index);
            changed = 1;
        }
        if (action == NG_WATCH_REMOVE) {
            result->removed++;
            continue;
        }
        struct sockaddr_in gw;
        fill_sin(&gw, router);
        int error = add_route(endpoint, 32, &gw, sizeof(gw), 1);
        if (error == EEXIST) {
            g_endpoints[i].foreign = 1;
        } else if (error) {
            result->failed++;
            failed = 1;
        } else {
            result->replaced++;
            changed = 1;
        }
    }
    if (changed)
        write_state();
    if (failed)
        return NG_NET_ERR_BYPASS;
    return result->waiting ? NG_NET_ERR_NO_GATEWAY : NG_NET_OK;
}

int ng_route_delete_host(struct in_addr destination) {
    return route_message(NG_RTM_DELETE, destination, 32, NULL, 0);
}
