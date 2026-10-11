/*
 * ngroute — helper di test per il full tunnel.
 *   ngroute get <ipv4>...     interfaccia di uscita (RTM_GET), GW=YES/NO
 *   ngroute resolve <nome>    primo IPv4 dal resolver di sistema
 *   ngroute delete <ipv4>     RTM_DELETE host route (test watchdog)
 *   ngroute gwcheck <ipv4>    bypass statico verso ipv4 e confronto con il
 *                             router del servizio fisico in configd
 * Non stampa gateway o server DNS (solo un tag di 16 bit per i confronti).
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include "NGNetConfig.h"
#include "NGDNSConfig.h"

static unsigned tag(struct in_addr address) {
    unsigned hash = 2166136261u;
    const unsigned char *bytes = (const unsigned char *)&address;
    for (int i = 0; i < 4; i++)
        hash = (hash ^ bytes[i]) * 16777619u;
    return (hash ^ (hash >> 16)) & 0xffffu;
}

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "get")) {
        int failed = 0;
        for (int i = 2; i < argc; i++) {
            struct in_addr destination, gateway;
            char ifname[IFNAMSIZ];
            int has_gateway = 0, static_host = 0;
            if (inet_pton(AF_INET, argv[i], &destination) != 1) {
                printf("ROUTE %s INVALID\n", argv[i]);
                failed = 1;
                continue;
            }
            int error = ng_route_lookup_flags(destination, ifname,
                                              &has_gateway, &gateway,
                                              &static_host);
            if (error) {
                printf("ROUTE %s ERROR=%d\n", argv[i], error);
                failed = 1;
            } else {
                printf("ROUTE %s IF=%s GW=%s STATIC_HOST=%s\n", argv[i],
                       ifname, has_gateway ? "YES" : "NO",
                       static_host ? "YES" : "NO");
            }
        }
        return failed;
    }
    if (argc == 3 && !strcmp(argv[1], "delete")) {
        struct in_addr destination;
        if (inet_pton(AF_INET, argv[2], &destination) != 1)
            return 2;
        int error = ng_route_delete_host(destination);
        printf("DELETE %s %s\n", argv[2], error ? "FAILED" : "OK");
        return error ? 1 : 0;
    }
    if (argc == 3 && !strcmp(argv[1], "gwcheck")) {
        struct in_addr destination, gateway, router;
        char ifname[IFNAMSIZ];
        int has_gateway = 0, static_host = 0;
        if (inet_pton(AF_INET, argv[2], &destination) != 1)
            return 2;
        int error = ng_route_lookup_flags(destination, ifname, &has_gateway,
                                          &gateway, &static_host);
        int have_router = ng_dns_physical_router(&router) == 0;
        printf("GWCHECK IF=%s BYPASS=%s GWTAG=%04x ROUTER=%s ROUTERTAG=%04x "
               "MATCH=%s\n", error ? "ERROR" : ifname,
               !error && static_host && has_gateway ? "YES" : "NO",
               !error && has_gateway ? tag(gateway) : 0u,
               have_router ? "YES" : "NO", have_router ? tag(router) : 0u,
               !error && static_host && has_gateway && have_router &&
               gateway.s_addr == router.s_addr ? "YES" : "NO");
        return error ? 1 : 0;
    }
    if (argc == 3 && !strcmp(argv[1], "resolve")) {
        struct addrinfo hints, *result = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        int error = getaddrinfo(argv[2], NULL, &hints, &result);
        if (error || !result) {
            printf("RESOLVE %s FAILED=%d\n", argv[2], error);
            return 1;
        }
        char text[INET_ADDRSTRLEN];
        struct sockaddr_in *sin = (struct sockaddr_in *)(void *)result->ai_addr;
        inet_ntop(AF_INET, &sin->sin_addr, text, sizeof(text));
        printf("RESOLVE %s A=%s\n", argv[2], text);
        freeaddrinfo(result);
        return 0;
    }
    fprintf(stderr, "uso: ngroute get <ipv4>... | resolve <nome> | "
                    "gwcheck <ipv4> | delete <ipv4>\n");
    return 2;
}
