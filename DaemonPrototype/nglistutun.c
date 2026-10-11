#include <ifaddrs.h>
#include <sys/socket.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    struct ifaddrs *head = NULL;
    if (getifaddrs(&head) != 0) {
        perror("getifaddrs");
        return 1;
    }

    unsigned interfaces = 0;
    unsigned ipv4 = 0;
    char previous[IFNAMSIZ] = {0};
    for (struct ifaddrs *p = head; p; p = p->ifa_next) {
        if (!p->ifa_name || strncmp(p->ifa_name, "utun", 4) != 0)
            continue;
        if (strcmp(previous, p->ifa_name) != 0) {
            const struct if_data *data = p->ifa_addr &&
                p->ifa_addr->sa_family == AF_LINK ? p->ifa_data : NULL;
            if (data)
                printf("UTUN_INTERFACE=%s MTU=%u\n", p->ifa_name,
                       (unsigned)data->ifi_mtu);
            else
                printf("UTUN_INTERFACE=%s\n", p->ifa_name);
            snprintf(previous, sizeof(previous), "%s", p->ifa_name);
            interfaces++;
        }
        if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET)
            ipv4++;
    }
    freeifaddrs(head);
    printf("UTUN_INTERFACES=%u\n", interfaces);
    printf("UTUN_IPV4_ADDRESSES=%u\n", ipv4);
    return 0;
}
