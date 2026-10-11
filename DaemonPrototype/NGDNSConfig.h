#ifndef NGDNSCONFIG_H
#define NGDNSCONFIG_H

#include <netinet/in.h>
#include "NGProfileUAPI.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NG_DNS_OK = 0,
    NG_DNS_SKIPPED = 1,          /* nessun server DNS nel profilo */
    NG_DNS_ERR_ARGS = -50,
    NG_DNS_ERR_STORE = -51,
    NG_DNS_ERR_WRITE = -52
};

/*
 * Pubblica in configd, come valori TEMPORANEI legati al processo del
 * daemon (rimossi da configd anche se il daemon muore), un servizio
 * "dev.luke.nineguard" con IPv4 { InterfaceName, Addresses, Router,
 * OverridePrimary=1 } e DNS { ServerAddresses, SearchDomains }: IPMonitor
 * lo rende primario e ne pubblica il DNS come resolver di default.
 */
int ng_dns_apply(const char *ifname, const char *ipv4_address,
                 const ng_profile_plan *plan);

/* Rimuove le chiavi pubblicate. Idempotente. */
void ng_dns_remove(void);

int ng_dns_is_applied(void);

/*
 * Router IPv4 del servizio fisico sottostante (Wi-Fi), letto da
 * State:/Network/Service/<id>/IPv4 ignorando il servizio NineGuard e le
 * interfacce utun/ppp/ipsec: con OverridePrimary Global/IPv4 riporta utun0.
 * Preferisce en*. 0 se trovato, -1 altrimenti (rete assente o configd).
 */
int ng_dns_physical_router(struct in_addr *router);

#ifdef __cplusplus
}
#endif

#endif
